"""Resolve original app inputs on the runner; never upload Apple app bundles."""
import hashlib
import json
import plistlib
import shutil
import struct
from pathlib import Path
from .context import ROOT,sha


def prepare_firmware_app(app):
    root=ROOT/'build/ios-input/filesystem/root'
    firmware=json.loads((ROOT/'build/results/apple-input.json').read_text())
    matches=[]
    for pattern in ('Applications/*.app/Info.plist','System/Applications/*.app/Info.plist'):
        for path in root.glob(pattern):
            info=plistlib.loads(path.read_bytes())
            if info.get('CFBundleIdentifier')==app['input']['bundleIdentifier']:
                matches.append((path.parent,info))
    if len(matches)!=1:
        raise RuntimeError('No unique firmware app for '+app['input']['bundleIdentifier']+
                           '; matches='+str([str(path.relative_to(root)) for path,_ in matches]))
    original,info=matches[0]
    executable=info['CFBundleExecutable']
    if not isinstance(executable,str) or Path(executable).name!=executable or executable in ('.','..'):
        raise RuntimeError('Unsafe firmware executable name')
    binary=original/executable
    app['originalInput']={'firmware':firmware['firmware'],
        'verifiedFirmwareSHA256':firmware['verifiedFirmwareSHA256'],
        'bundlePath':original.relative_to(root).as_posix(),
        'bundleIdentifier':info['CFBundleIdentifier'],
        'version':info.get('CFBundleShortVersionString'),'build':info.get('CFBundleVersion'),
        'binarySHA256':sha(binary)}
    # Validate the actual executable's declared ISA/encryption, rather than
    # inferring compatibility from the phone model or app name.
    with binary.open('rb') as stream:
        header=stream.read(32)
        if len(header)!=32:raise RuntimeError('Truncated original Mach-O header')
        magic,cpu,subtype,kind,ncmds,command_bytes,_,_=struct.unpack('<8I',header)
        metadata={'magic':hex(magic),'cpu':hex(cpu),'subtype':hex(subtype),
                  'filetype':kind,'encrypted':False,'dependencies':[]}
        app['originalMachO']=metadata
        if magic!=0xfeedfacf or cpu!=0x100000c or (subtype&0xffffff)!=0 or kind!=2:
            raise RuntimeError('Original executable is not supported thin ARM64: '+json.dumps(metadata))
        if ncmds>16384 or command_bytes>16*1024*1024:
            raise RuntimeError('Unreasonable original Mach-O command table')
        commands=stream.read(command_bytes)
    if len(commands)!=command_bytes:raise RuntimeError('Truncated original Mach-O commands')
    offset=0
    for _ in range(ncmds):
        if offset+8>len(commands):raise RuntimeError('Truncated original Mach-O command')
        command,size=struct.unpack_from('<II',commands,offset)
        if size<8 or offset+size>len(commands):raise RuntimeError('Invalid original Mach-O command size')
        block=commands[offset:offset+size]
        if command in (0x21,0x2c):
            if size<20:raise RuntimeError('Invalid original encryption command')
            metadata['encrypted']|=bool(struct.unpack_from('<I',block,16)[0])
        if command in (0xc,0x18|0x80000000,0x1f|0x80000000,0x23|0x80000000):
            if size<24:raise RuntimeError('Invalid original dylib command')
            start=struct.unpack_from('<I',block,8)[0]
            if not 24<=start<size:raise RuntimeError('Invalid original dylib name offset')
            metadata['dependencies'].append(block[start:].split(b'\0',1)[0].decode('utf-8'))
        offset+=size
    if offset!=len(commands):raise RuntimeError('Original Mach-O command table size mismatch')
    if metadata['encrypted']:raise RuntimeError('Original executable is encrypted')
    bundle=ROOT/'build/ios-input/apps'/original.name
    bundle.parent.mkdir(parents=True,exist_ok=True)
    # Preserve every original file and resource; reject escaped links. This is
    # an input boundary, not a library/feature existence checklist.
    files=[]
    for path in sorted(original.rglob('*')):
        if not path.resolve().is_relative_to(root.resolve()):
            raise RuntimeError('Firmware app resource escapes the verified root')
        if path.is_file():files.append({'path':path.relative_to(original).as_posix(),'sha256':sha(path)})
    app['originalInput'].update(files=files,
        bundleManifestSHA256=hashlib.sha256(json.dumps(files,sort_keys=True).encode()).hexdigest())
    shutil.copytree(original,bundle)
    shutil.copy2(ROOT/'build/apps/libarlinux-ios.dylib',bundle/'libarlinux-ios.dylib')
    if sha(bundle/executable)!=app['originalInput']['binarySHA256']:
        raise RuntimeError('Original executable changed during preparation')
    app.update(binary=str((bundle/executable).relative_to(ROOT)),
               binarySHA256=sha(binary),build='passed')


def prepare_inputs(apps,reference):
    for app in apps.values():
        if app.get('build')!='pending-firmware':continue
        try:
            if reference:raise RuntimeError('Firmware apps are not available on the Apple-host reference')
            prepare_firmware_app(app)
        except (OSError,ValueError,KeyError,RuntimeError,struct.error) as error:
            app.update(build='failed',error=str(error))
        # Metadata only. Bundles remain outside build/results and build/apps.
        (ROOT/'build/results'/('input-'+app['id']+'.json')).write_text(json.dumps(app,indent=2)+'\n')
        print('APP_ORIGINAL_INPUT '+json.dumps(app),flush=True)
