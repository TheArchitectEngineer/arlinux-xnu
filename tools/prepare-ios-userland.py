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
        raise SystemExit('APFS reader source mismatch: ' + actual_source)
reader_build = output / 'apfs-build'
run('cmake', '-S', reader_source, '-B', reader_build, '-DUSE_FUSE3=ON',
    '-DCMAKE_C_COMPILER_LAUNCHER=ccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=ccache')
run('cmake', '--build', reader_build, '--target', 'apfs-fuse', '-j2')

archive = output / 'firmware.ipsw'
run('curl', '--fail', '--location', '--retry', '3', '--output', archive, source['url'])
actual = sha(archive)
if actual != source['sha256']:
    raise SystemExit('Apple firmware SHA-256 mismatch: ' + actual)
print('APPLE_INPUT_SHA256', actual, flush=True)
unzip = shutil.which('7zz') or shutil.which('7z')
if not unzip:
    raise SystemExit('7-Zip is required on the runner')
run(unzip, 'e', '-y', '-o' + str(output), archive, 'BuildManifest.plist')
manifest = plistlib.loads((output / 'BuildManifest.plist').read_bytes())
identity = manifest['BuildIdentities'][0]
member = identity['Manifest']['OS']['Info']['Path']
if Path(member).name != member or not member.endswith('.dmg'):
    raise SystemExit('Unexpected OS image path in Apple BuildManifest')
run(unzip, 'e', '-y', '-o' + str(output), archive, member)
image = output / member
# Release the validated archive before expanding the filesystem on the runner.
archive.unlink()
filesystem = output / 'filesystem'
filesystem.mkdir(exist_ok=True)
# APFS compressed files keep their bytes in decmpfs/resource-fork attributes.
# Read through the actual APFS decoder, not 7-Zip's zero-size data-fork view.
run(reader_build / 'apfs-fuse', '-o', 'ro', image, filesystem)
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
    cache = filesystem / 'root/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64'
    with cache.open('rb') as stream:
        header = stream.read(16384)
        mappings_offset, mappings_count, images_offset, images_count = struct.unpack_from('<IIII', header, 16)
        slide_offset, slide_size = struct.unpack_from('<QQ', header, 56)
        mappings = [dict(zip(['address', 'size', 'offset', 'maxProtection', 'protection'],
                            struct.unpack_from('<QQQII', header, mappings_offset+i*32)))
                    for i in range(mappings_count)]
        stream.seek(slide_offset)
        slide_header = stream.read(40)
    cache_record = {'bytes': cache.stat().st_size, 'sha256': sha(cache),
                    'magic': header[:16].decode('ascii', errors='replace'),
                    'mappings': mappings, 'imagesOffset': images_offset,
                    'imagesCount': images_count, 'slideOffset': slide_offset,
                    'slideBytes': slide_size, 'slideHeaderWords': list(struct.unpack('<IIIIIIQQ', slide_header))}
    (evidence / 'cache-input.json').write_text(json.dumps(cache_record, indent=2)+'\n')
    print('APPLE_CACHE_INPUT '+json.dumps(cache_record), flush=True)
except BaseException:
    run('fusermount3', '-u', filesystem)
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
          'osImage': member, 'osImageSHA256': sha(image),
          'apfsReader': reader, 'dyldBytes': loader.stat().st_size, 'dyldSHA256': sha(loader)}
(evidence / 'apple-input.json').write_text(json.dumps(record, indent=2) + '\n')
print('APPLE_USERLAND_INPUT ' + json.dumps(record), flush=True)
