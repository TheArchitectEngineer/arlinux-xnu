"""Runner-internal PlayCover conversion for the same app collection."""
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import struct
from app_suite.inputs import prepare_reference_firmware,prepare_ipa_app
from app_suite.context import sha
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/reference-tools';OUT.mkdir(parents=True,exist_ok=False)
def run(*args):subprocess.run(list(map(str,args)),check=True)
source=json.loads((ROOT/'sources.lock.json').read_text())['playcoverReference']
upstream=ROOT/'build/reference-source';upstream.mkdir()
run('git','-C',upstream,'init');run('git','-C',upstream,'fetch','--depth=1','https://github.com/'+source['repository']+'.git',source['commit'])
run('git','-C',upstream,'checkout','--detach','FETCH_HEAD')
# Retain corresponding GPL converter source and its license in safe evidence.
corresponding=ROOT/'build/results/converter-source';corresponding.mkdir(parents=True)
for name in ['PlayCover/Utils/Macho.swift','PlayCover/Utils/Extensions/DataExtensions.swift','PlayCover/PlayCoverError.swift','LICENSE']:
    shutil.copyfile(upstream/name,corresponding/Path(name).name)
for name in ['playcover-convert.swift','playcover-mach-bridge.h']:
    shutil.copyfile(ROOT/'tools'/name,corresponding/name)
run('xcrun','swiftc','-O','-import-objc-header',corresponding/'playcover-mach-bridge.h',corresponding/'Macho.swift',corresponding/'DataExtensions.swift',corresponding/'PlayCoverError.swift',corresponding/'playcover-convert.swift','-o',OUT/'convert')
run('xcrun','swiftc',ROOT/'tools/desktop-events.swift','-o',OUT/'desktop-events')
products=json.loads((ROOT/'build/apps/products.json').read_text())
prepare_reference_firmware(products['apps'])
for app in products['apps']:
    if app['build']=='pending-ipa':
        prepare_ipa_app(app,destination=ROOT/'build/reference-originals',platform_library=False)
observer=OUT/'libarlinux-inspection.dylib'
shutil.copy2(ROOT/'build/apps/libarlinux-inspection.dylib',observer)
run(OUT/'convert',observer);run('codesign','--force','--sign','-',observer)
for app in products['apps']:
    if app['build']!='passed':continue
    original=(ROOT/app['binary']).parent;bundle=ROOT/'build/reference-apps'/original.name
    shutil.copytree(original,bundle)
    platform=bundle/'libarlinux-ios.dylib'
    if platform.exists():platform.unlink()
    # Original IPAs carry their own frameworks and extensions. Convert every
    # actual Mach-O in the copied bundle, leaving source bytes and resources
    # untouched. The pinned PlayCover converter rejects encrypted inputs.
    converted=[];unavailable_extensions=[]
    for binary in sorted(bundle.rglob('*')):
        if not binary.is_file():continue
        with binary.open('rb') as stream:magic=stream.read(4)
        if magic not in (b'\xcf\xfa\xed\xfe',b'\xce\xfa\xed\xfe',b'\xca\xfe\xba\xbe',b'\xbe\xba\xfe\xca'):continue
        original_hash=sha(binary)
        # A decrypted main IPA can still contain encrypted optional extension
        # executables. Keep their encrypted code and mark it unavailable;
        # do not treat them as runnable or let them prevent main-app diagnosis.
        # Required helpers and all ordinary dependencies still fail conversion.
        relative=binary.relative_to(bundle)
        optional_extension=any(part.endswith('.appex') for part in relative.parts) and not app.get('helpers')
        encrypted=False
        if magic==b'\xcf\xfa\xed\xfe' and optional_extension:
            with binary.open('rb') as stream:
                header=stream.read(32);fields=struct.unpack('<8I',header)
                if fields[4]>16384 or fields[5]>16*1024*1024:raise RuntimeError('Invalid extension Mach-O commands')
                commands=stream.read(fields[5]);offset=0
                if len(commands)!=fields[5]:raise RuntimeError('Truncated extension Mach-O commands')
                for _ in range(fields[4]):
                    if offset+8>len(commands):raise RuntimeError('Truncated extension Mach-O command')
                    command,size=struct.unpack_from('<II',commands,offset)
                    if size<8 or offset+size>len(commands):raise RuntimeError('Invalid extension Mach-O command')
                    if command in (0x21,0x2c):
                        if size<20:raise RuntimeError('Invalid extension encryption command')
                        encrypted|=bool(struct.unpack_from('<I',commands,offset+16)[0])
                    offset+=size
                if offset!=len(commands):raise RuntimeError('Extension Mach-O command size mismatch')
        if encrypted:
            unavailable_extensions.append({'path':relative.as_posix(),'originalSHA256':original_hash,
                'reason':'Encrypted optional extension; not converted or certified runnable'})
            continue
        run(OUT/'convert',binary);binary.chmod(0o755)
        # Match PlayCover's per-image signing: --deep on the outer app alone
        # left converted embedded frameworks with invalid old signatures.
        run('codesign','--force','--sign','-',binary)
        converted.append({'path':binary.relative_to(bundle).as_posix(),
            'originalSHA256':original_hash})
    info=plistlib.loads((bundle/'Info.plist').read_bytes());info['LSMinimumSystemVersion']='11.0'
    (bundle/'Info.plist').write_bytes(plistlib.dumps(info))
    run('codesign','--force','--deep','--sign','-',bundle)
    for image in converted:image['convertedSignedSHA256']=sha(bundle/image['path'])
    for image in unavailable_extensions:image['signedSHA256']=sha(bundle/image['path'])
    app['referenceBinary']=str((bundle/app['name']).relative_to(ROOT))
    app['referenceConversion']={'converterCommit':source['commit'],'images':converted,
        'unavailableExtensions':unavailable_extensions,
        'note':'Converted for the Apple host; original input hashes remain separate'}
(ROOT/'build/reference-products.json').write_text(json.dumps(products,indent=2)+'\n')
