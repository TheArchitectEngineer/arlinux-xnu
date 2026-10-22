#!/usr/bin/env python3
"""Runner-only input preparation; called by the root build.sh workflow.

Apple firmware stays transient on the runner. Upload only provenance/logs, not
the IPSW, disk image, extracted dyld or frameworks. No workstation compilation.
"""
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import struct
import time
import argparse
import signal
from concurrent.futures import ThreadPoolExecutor

root = Path(__file__).resolve().parents[1]
source = json.loads((root / 'sources.lock.json').read_text())['iosUserland']
output = root / 'build/ios-input'
evidence = root / 'build/results'
output.mkdir(parents=True, exist_ok=True)
evidence.mkdir(parents=True, exist_ok=True)


def run(*args):
    print('>', *args, flush=True)
    subprocess.run(list(map(str, args)), check=True)


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(8 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


reader = json.loads((root / 'sources.lock.json').read_text())['apfsReader']
reader_source = output / 'apfs-source'
reader_build = output / 'apfs-build'
archive = output / 'firmware.ipsw'


def prepare_reader():
    started=time.monotonic()
    reader_source.mkdir(exist_ok=True)
    run('git', 'init', reader_source)
    run('git', '-C', reader_source, 'fetch', '--depth', '1',
        'https://github.com/' + reader['repository'] + '.git', reader['commit'])
    run('git', '-C', reader_source, 'checkout', '--detach', 'FETCH_HEAD')
    run('git', '-C', reader_source, 'submodule', 'update', '--init', '--recursive')
    for directory, expected in [(reader_source, reader['commit']),
                                (reader_source / '3rdparty/lzfse', reader['lzfseCommit'])]:
        actual_source = subprocess.check_output(['git', '-C', str(directory), 'rev-parse', 'HEAD'], text=True).strip()
        if actual_source != expected:
            raise RuntimeError('APFS reader source mismatch: ' + actual_source)
    run('cmake', '-S', reader_source, '-B', reader_build, '-DUSE_FUSE3=ON',
        '-DCMAKE_C_COMPILER_LAUNCHER=ccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=ccache')
    run('cmake', '--build', reader_build, '--target', 'apfs-fuse', '-j2')
    return time.monotonic()-started


def download_firmware():
    started=time.monotonic()
    arguments=['curl','--fail','--location','--connect-timeout','15',
               '--max-time','180','--retry','1','--retry-max-time','220',
               '--output',str(archive),source['url']]
    print('>',*arguments,flush=True)
    try:
        # subprocess.run kills and reaps the actual curl on timeout or a
        # Python exception, including the cancellation signal handler below.
        subprocess.run(arguments,check=True,timeout=240)
    except BaseException:
        archive.unlink(missing_ok=True)
        print('APPLE_DOWNLOAD_FAILED '+json.dumps({'seconds':round(time.monotonic()-started,3),
            'deadlineSeconds':240,'partialRemoved':True}),flush=True)
        raise
    return time.monotonic()-started


parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--download-only',action='store_true',
    help='Workflow-internal: fetch the declared firmware while apt installs tools; validation remains mandatory before extraction')
args=parser.parse_args()
def cancelled(signum,frame):
    # Do not leave a curl writing evidence after the workflow takes its receipt.
    raise SystemExit(128+signum)
signal.signal(signal.SIGTERM,cancelled)
if args.download_only:
    seconds=download_firmware()
    print('APPLE_DOWNLOAD_TIMING '+json.dumps({'seconds':round(seconds,3),
        'bytes':archive.stat().st_size,'validated':False}),flush=True)
    raise SystemExit(0)


# The source-owned APFS reader and pinned firmware download are independent.
# Join both real commands before validation/extraction; neither a cached image
# nor a detached background download can become an input after a failure.
started=time.monotonic()
downloaded_before_preparation=archive.is_file()
if downloaded_before_preparation:
    # The workflow has joined its download process. Existing bytes are still
    # untrusted: the size and SHA-256 checks below run before any extraction.
    reader_seconds=prepare_reader()
    firmware_seconds=0
else:
    with ThreadPoolExecutor(max_workers=2) as preparation:
        reader_task=preparation.submit(prepare_reader)
        firmware_task=preparation.submit(download_firmware)
        reader_seconds=reader_task.result()
        firmware_seconds=firmware_task.result()
print('APPLE_PREPARATION_TIMING '+json.dumps({'readerSeconds':round(reader_seconds,3),
    'downloadSeconds':round(firmware_seconds,3),'parallelSeconds':round(time.monotonic()-started,3),
    'downloadedBeforePreparation':downloaded_before_preparation}),flush=True)
if source.get('bytes') and archive.stat().st_size!=source['bytes']:
    raise SystemExit('Apple firmware byte size mismatch')
actual = sha(archive)
if actual != source['sha256']:
    raise SystemExit('Apple firmware SHA-256 mismatch: ' + actual)
print('APPLE_INPUT_SHA256', actual, flush=True)
unzip = shutil.which('7zz') or shutil.which('7z')
if not unzip:
    raise SystemExit('7-Zip is required on the runner')
run(unzip, 'e', '-y', '-o' + str(output), archive, 'BuildManifest.plist')
manifest = plistlib.loads((output / 'BuildManifest.plist').read_bytes())
if (manifest.get('ProductVersion'),manifest.get('ProductBuildVersion'))!=(source['version'],source['build']):
    raise SystemExit('Firmware BuildManifest version/build mismatch')
if source['device'] not in manifest.get('SupportedProductTypes',[]):
    raise SystemExit('Requested device is not in this firmware BuildManifest')
components={}
for identity in manifest['BuildIdentities']:
    for role in ('OS','Cryptex1,SystemOS'):
        if role in identity['Manifest']:
            components.setdefault(role,set()).add(identity['Manifest'][role]['Info']['Path'])
if any(len(paths)!=1 for paths in components.values()):
    raise SystemExit('Ambiguous device-specific OS images; select the requested board explicitly')
images={role:paths.pop() for role,paths in components.items()}
(evidence/'firmware-layout.json').write_text(json.dumps({'product':source['device'],'components':images},indent=2)+'\n')
if len({Path(member).name for member in images.values()})!=len(images):
    raise SystemExit('OS image basename collision')
for member in images.values():
    relative=Path(member)
    if relative.is_absolute() or '..' in relative.parts or not member.endswith('.dmg'):
        raise SystemExit('Unsafe or unsupported OS image member: '+member)
    run(unzip,'e','-y','-o'+str(output),archive,member)
archive.unlink()
mounts=[]
for role,member in images.items():
    mount=output/('os-volume' if role=='OS' else 'system-volume');mount.mkdir(exist_ok=True)
    run(reader_build/'apfs-fuse','-o','ro',output/Path(member).name,mount)
    mounts.append(mount)
filesystem=output/'filesystem';filesystem.mkdir(exist_ok=True)
merged=filesystem/'root';merged.mkdir(exist_ok=True)
# A real read-only union of the two actual firmware volumes, not copied stubs.
branches=':'.join(str(mount/'root')+'=RO' for mount in reversed(mounts))
run('unionfs','-o','ro',branches,merged)
loader = output / 'dyld'
try:
    # Record the real mounted layout before assuming an OS-volume path.
    # A missing file must distinguish volume/path errors from decompression.
    for relative in ['.', 'root', 'root/usr', 'root/usr/lib', 'root/usr/lib/dyld',
                     'root/usr/lib/libSystem.B.dylib', 'root/usr/lib/system',
                     'root/System/Library/Caches/com.apple.dyld']:
        path = filesystem / relative
        try:
            status = path.lstat()
            entry = {'path': relative, 'mode': oct(status.st_mode),
                     'bytes': status.st_size}
            if path.is_symlink():
                entry['symlink'] = str(path.readlink())
            elif path.is_dir():
                entry['children'] = sorted(item.name for item in path.iterdir())[:60]
            print('APFS_LAYOUT ' + json.dumps(entry), flush=True)
        except OSError as error:
            print('APFS_LAYOUT ' + json.dumps({'path': relative, 'error': str(error)}), flush=True)
    # apfs-fuse exposes APFS's root-parent inode at the mount point.
    # Like ios-sweet/tools/mkiosroot.sh, enter the real volume's root directory.
    shutil.copyfile(filesystem / 'root/usr/lib/dyld', loader)
    candidates=[filesystem/'root'/directory/'dyld_shared_cache_arm64' for directory in
        ('System/Library/Caches/com.apple.dyld','System/Library/dyld')]
    caches=[path for path in candidates if path.is_file()]
    if len(caches)!=1:
        raise SystemExit('No unique ARM64 cache in actual OS layout: '+str(caches))
    cache=caches[0]
    from apple_cache import describe_cache
    cache_record=describe_cache(cache,sha)
    (evidence / 'cache-input.json').write_text(json.dumps(cache_record, indent=2)+'\n')
    print('APPLE_CACHE_INPUT '+json.dumps(cache_record), flush=True)
    # Register actual bundle metadata once while installing the firmware view.
    # This is operational service lookup data, not a feature/existence test.
    # Runtime launch revalidates the selected bundle and executable in this root.
    started=time.monotonic();services={}
    for pattern in ('System/Library/Frameworks/*.framework/XPCServices/*.xpc/Info.plist',
                    'System/Library/PrivateFrameworks/*.framework/XPCServices/*.xpc/Info.plist',
                    'System/Library/XPCServices/*.xpc/Info.plist'):
        for path in merged.glob(pattern):
            info=plistlib.loads(path.read_bytes())
            identifier=info.get('CFBundleIdentifier')
            if not isinstance(identifier,str) or not identifier:continue
            relative=str(path.relative_to(merged))
            if relative not in services.setdefault(identifier,[]):services[identifier].append(relative)
    service_index=output/'xpc-services.plist'
    for entries in services.values():entries.sort()
    service_index.write_bytes(plistlib.dumps({'root':str(merged.resolve()),'services':services},fmt=plistlib.FMT_BINARY))
    print('APPLE_SERVICE_INDEX '+json.dumps({'services':len(services),'sha256':sha(service_index),
        'seconds':round(time.monotonic()-started,3)}),flush=True)
except BaseException:
    for mount in [merged,*reversed(mounts)]:
        subprocess.run(['fusermount3','-u',str(mount)],check=False)
    raise
# Keep the read-only firmware mounted for the real loader's subsequent reads.
# The hosted runner owns its lifetime; no Apple binary is uploaded.
print('APPLE_USERLAND_ROOT', filesystem / 'root', flush=True)
system_version = plistlib.loads((filesystem / 'root/System/Library/CoreServices/SystemVersion.plist').read_bytes())
version_record = {key: system_version.get(key) for key in
                  ('ProductName', 'ProductVersion', 'ProductBuildVersion', 'ReleaseType')}
(evidence / 'system-version.json').write_text(json.dumps(version_record, indent=2) + '\n')
print('APPLE_SYSTEM_VERSION ' + json.dumps(version_record), flush=True)

record = {'firmware': source, 'verifiedFirmwareSHA256': actual,
          'images':{role:{'member':member,'sha256':sha(output/Path(member).name)} for role,member in images.items()},
          'apfsReader': reader, 'dyldBytes': loader.stat().st_size, 'dyldSHA256': sha(loader),
          'serviceIndexSHA256':sha(service_index)}
(evidence / 'apple-input.json').write_text(json.dumps(record, indent=2) + '\n')
print('APPLE_USERLAND_INPUT ' + json.dumps(record), flush=True)
