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

root = Path(__file__).resolve().parents[1]
source = json.loads((root / 'sources.lock.json').read_text())['iosUserland']
output = root / 'build/ios-input'
evidence = root / 'build/dyld'
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
run('cmake', '-S', reader_source, '-B', reader_build, '-DUSE_FUSE3=ON')
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
    shutil.copyfile(filesystem / 'usr/lib/dyld', loader)
finally:
    run('fusermount3', '-u', filesystem)
record = {'firmware': source, 'verifiedFirmwareSHA256': actual,
          'osImage': member, 'osImageSHA256': sha(image),
          'apfsReader': reader, 'dyldBytes': loader.stat().st_size, 'dyldSHA256': sha(loader)}
(evidence / 'apple-input.json').write_text(json.dumps(record, indent=2) + '\n')
print('APPLE_USERLAND_INPUT ' + json.dumps(record), flush=True)
