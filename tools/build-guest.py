#!/usr/bin/env python3
"""Internal hosted build node for the real XNU process journey executable."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--compiler', required=True)
args = parser.parse_args()
lock = json.loads((root / 'sources.lock.json').read_text())['iosSweet']
source = root / 'build/guest-source'
source.mkdir(parents=True, exist_ok=True)
subprocess.run(['git', 'init', '-q', str(source)], check=True)
subprocess.run(['git', '-C', str(source), 'fetch', '-q', '--depth', '1',
                'https://github.com/' + lock['repository'] + '.git', lock['commit']], check=True)
subprocess.run(['git', '-C', str(source), 'checkout', '-q', '--detach', 'FETCH_HEAD'], check=True)
revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
if revision != lock['commit']:
    raise SystemExit('Guest Mach-O writer source differs from the declared pin')
args.output.mkdir(parents=True, exist_ok=True)
subprocess.run([args.compiler, '--target=arm64-apple-ios16.0', '-c',
                str(root / 'guests/init.S'), '-o', str(args.output / 'init.o')], check=True)
subprocess.run([sys.executable, str(source / 'tools/mkinit.py'),
                str(args.output / 'init.o'), str(args.output / 'init.macho')], check=True)
digest = hashlib.sha256((args.output / 'init.macho').read_bytes()).hexdigest()
(args.output / 'SHA256SUMS').write_text(digest + '  init.macho\n')
print('XNU_GUEST_IMAGE_SHA256 ' + digest, flush=True)
