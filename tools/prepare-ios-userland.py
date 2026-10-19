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
run(unzip, 'x', '-y', '-o' + str(filesystem), image, 'usr/lib/dyld', '*/usr/lib/dyld', '-r')
matches = list(filesystem.rglob('dyld'))
if not matches:
    # UDIF may expose a filesystem image as a nested archive rather than open
    # it transparently. Extract the container, then select dyld from that FS.
    container = output / 'container'
    run(unzip, 'x', '-y', '-o' + str(container), image)
    for nested in container.iterdir():
        if nested.is_file():
            run(unzip, 'x', '-y', '-o' + str(filesystem), nested,
                'usr/lib/dyld', '*/usr/lib/dyld', '-r')
    matches = list(filesystem.rglob('dyld'))
matches = [path for path in matches if path.is_file() and not path.is_symlink()]
if len(matches) != 1:
    raise SystemExit('Expected one real dyld file, found: ' + str(matches))
loader = output / 'dyld'
shutil.copyfile(matches[0], loader)
record = {'firmware': source, 'verifiedFirmwareSHA256': actual,
          'osImage': member, 'osImageSHA256': sha(image),
          'dyldBytes': loader.stat().st_size, 'dyldSHA256': sha(loader)}
(evidence / 'apple-input.json').write_text(json.dumps(record, indent=2) + '\n')
print('APPLE_USERLAND_INPUT ' + json.dumps(record), flush=True)
