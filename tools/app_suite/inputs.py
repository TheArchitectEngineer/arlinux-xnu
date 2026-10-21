"""Resolve original app inputs on the runner; never upload Apple app bundles."""
import hashlib
import json
import os
import plistlib
import shutil
import struct
import subprocess
import zipfile
from pathlib import Path
from .context import ROOT,sha


def prepare_firmware_app(app,root=None,firmware=None,destination=None,platform_library=True):
    root=root if root is not None else ROOT/'build/ios-input/filesystem/root'
    firmware=firmware if firmware is not None else json.loads((ROOT/'build/results/apple-input.json').read_text())
    matches=[]
    # Removable system apps can be staged for installation on first device
    # boot. Resolve their real bundle metadata too; do not require SpringBoard
    # installation or infer a path from the app's display name.
    app['inputSearch']=[]
    for pattern in ('Applications/*.app/Info.plist','System/Applications/*.app/Info.plist',
                    'private/var/staged_system_apps/*.app/Info.plist'):
        for path in root.glob(pattern):
            info=plistlib.loads(path.read_bytes())
            app['inputSearch'].append({'path':path.relative_to(root).as_posix(),
                                       'bundleIdentifier':info.get('CFBundleIdentifier')})
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
    expected=app['input'].get('binarySHA256')
    if expected and app['originalInput']['binarySHA256']!=expected:
        raise RuntimeError('Original firmware app executable SHA-256 mismatch')
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
    bundle=(destination if destination is not None else ROOT/'build/ios-input/apps')/original.name
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
    if platform_library:shutil.copy2(ROOT/'build/apps/libarlinux-ios.dylib',bundle/'libarlinux-ios.dylib')
    if sha(bundle/executable)!=app['originalInput']['binarySHA256']:
        raise RuntimeError('Original executable changed during preparation')
    app.update(binary=str((bundle/executable).relative_to(ROOT)),
               binarySHA256=sha(binary),build='passed')


def prepare_reference_firmware(apps):
    """Native macOS read-only mount of the same verified source, never an upload."""
    selected=[app for app in apps if app.get('build')=='pending-firmware']
    if not selected:return
    source=json.loads((ROOT/'sources.lock.json').read_text())['iosUserland']
    directory=ROOT/'build/reference-firmware';directory.mkdir(parents=True,exist_ok=False)
    archive=directory/'firmware.ipsw'
    subprocess.run(['curl','--fail','--location','--retry','3','--output',str(archive),source['url']],check=True)
    if archive.stat().st_size!=source['bytes'] or sha(archive)!=source['sha256']:
        raise RuntimeError('Apple reference firmware size/SHA-256 mismatch')
    with zipfile.ZipFile(archive) as package:
        manifest=plistlib.loads(package.read('BuildManifest.plist'))
        if (manifest.get('ProductVersion'),manifest.get('ProductBuildVersion'))!=(source['version'],source['build']) or source['device'] not in manifest.get('SupportedProductTypes',[]):
            raise RuntimeError('Apple reference firmware identity mismatch')
        members={identity['Manifest']['OS']['Info']['Path'] for identity in manifest['BuildIdentities'] if 'OS' in identity['Manifest']}
        if len(members)!=1:raise RuntimeError('Ambiguous reference OS image')
        member=members.pop();image=directory/'system.dmg'
        with package.open(member) as incoming,image.open('wb') as outgoing:shutil.copyfileobj(incoming,outgoing)
    archive.unlink()
    volumes=directory/'volumes';volumes.mkdir()
    mounted=plistlib.loads(subprocess.check_output(['hdiutil','attach','-readonly','-nobrowse','-plist','-mountroot',str(volumes),str(image)]))
    entities=mounted.get('system-entities',[])
    device=next((entry['dev-entry'] for entry in entities if entry.get('dev-entry')),None)
    try:
        roots=[]
        for entity in entities:
            if 'mount-point' not in entity:continue
            mount=Path(entity['mount-point'])
            if not mount.resolve().is_relative_to(volumes.resolve()):raise RuntimeError('Unexpected reference mount outside owned directory')
            roots.extend([mount,mount/'root'])
        for app in selected:
            matches=[]
            for candidate in roots:
                for pattern in ('Applications/*.app/Info.plist','System/Applications/*.app/Info.plist','private/var/staged_system_apps/*.app/Info.plist'):
                    if any(plistlib.loads(path.read_bytes()).get('CFBundleIdentifier')==app['input']['bundleIdentifier'] for path in candidate.glob(pattern)):
                        matches.append(candidate)
            if len(matches)!=1:raise RuntimeError('No unique original app root on reference: '+str(matches))
            prepare_firmware_app(app,root=matches[0],firmware={'firmware':source,'verifiedFirmwareSHA256':source['sha256']},
                                 destination=ROOT/'build/reference-originals',platform_library=False)
    finally:
        if device:subprocess.run(['hdiutil','detach',device],check=True)


def prepare_inputs(apps,reference):
    for app in apps.values():
        if app.get('build')!='pending-firmware':continue
        try:
            if reference:
                prepared=json.loads((ROOT/'build/reference-products.json').read_text())
                if prepared['source']!=os.environ['GITHUB_SHA'] or prepared['runId']!=os.environ['GITHUB_RUN_ID']:
                    raise RuntimeError('Reference products source/run mismatch')
                record=next(item for item in prepared['apps'] if item['id']==app['id'])
                app.update(record)
            else:prepare_firmware_app(app)
        except (OSError,ValueError,KeyError,RuntimeError,struct.error) as error:
            app.update(build='failed',error=str(error))
        # Metadata only. Bundles remain outside build/results and build/apps.
        (ROOT/'build/results'/('input-'+app['id']+'.json')).write_text(json.dumps(app,indent=2)+'\n')
        print('APP_ORIGINAL_INPUT '+json.dumps(app),flush=True)
