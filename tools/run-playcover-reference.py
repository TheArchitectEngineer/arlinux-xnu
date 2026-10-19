#!/usr/bin/env python3
"""Runner-only observation of our iOS binary using PlayCover's conversion.

Does not emulate macOS/UIKit and does not establish Linux compatibility.
No PlayTools injection yet: isolate the unmodified app/conversion/host path.
Called only by the root build.sh workflow. GUI and debugger failures are evidence.
"""
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import shutil
import signal
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[1]
os.chdir(root)
out = root / 'build/playcover'
out.mkdir(parents=True, exist_ok=True)
if sys.platform != 'darwin' or platform.machine() != 'arm64':
    raise SystemExit('The reference requires a real ARM64 macOS runner')


def run(*args):
    print('REFERENCE_COMMAND', *map(str, args), flush=True)
    subprocess.run(list(map(str, args)), check=True)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


source = json.loads((root / 'sources.lock.json').read_text())['playcoverReference']
upstream = root / 'build/research/playcover-reference'
upstream.mkdir(parents=True, exist_ok=True)
run('git', '-C', upstream, 'init')
run('git', '-C', upstream, 'fetch', '--depth=1',
    'https://github.com/' + source['repository'] + '.git', source['commit'])
run('git', '-C', upstream, 'checkout', '--detach', 'FETCH_HEAD')
# Retain the corresponding sources/license alongside the compiled GPL tool.
corresponding = out / 'converter-source'
corresponding.mkdir(exist_ok=True)
files = ['PlayCover/Utils/Macho.swift', 'PlayCover/Utils/Extensions/DataExtensions.swift',
         'PlayCover/PlayCoverError.swift', 'LICENSE']
for name in files:
    shutil.copyfile(upstream / name, corresponding / Path(name).name)
for name in ['playcover-convert.swift', 'playcover-mach-bridge.h', 'run-playcover-reference.py']:
    shutil.copyfile(root / 'tools' / name, corresponding / name)
(corresponding / 'source.json').write_text(json.dumps(source, indent=2) + '\n')
run('xcrun', 'swiftc', '-O', '-g', '-import-objc-header',
    corresponding / 'playcover-mach-bridge.h', corresponding / 'Macho.swift',
    corresponding / 'DataExtensions.swift', corresponding / 'PlayCoverError.swift',
    corresponding / 'playcover-convert.swift', '-o', out / 'playcover-convert')

run('xcrun', 'swiftc', '-O', root / 'tools/playcover-ui.swift', '-o', out / 'playcover-ui')

original = root / 'build/native/ARHello.app'
app = out / 'ARHello.app'
shutil.copytree(original, app, dirs_exist_ok=True)
binary = app / 'ARHello'
record = {'name': 'ARHello', 'version': '1.0', 'source': source,
          'originalSHA256': digest(binary), 'platform': platform.platform(),
          'injectedPlayTools': False}
run(out / 'playcover-convert', binary)
info = plistlib.loads((app / 'Info.plist').read_bytes())
info['LSMinimumSystemVersion'] = '11.0'
(app / 'Info.plist').write_bytes(plistlib.dumps(info))
entitlements = out / 'debug-entitlements.plist'
entitlements.write_bytes(plistlib.dumps({'com.apple.security.get-task-allow': True}))
run('chmod', '+x', binary)
run('codesign', '--force', '--sign', '-', '--entitlements', entitlements, app)
record['convertedSignedSHA256'] = digest(binary)
(out / 'app-input.json').write_text(json.dumps(record, indent=2) + '\n')
print('PLAYCOVER_REFERENCE_INPUT', json.dumps(record), flush=True)

# Two real GUI launches, using the same converted/signed application each time.
# LLDB logs the actual UIKit entry; external host events must reach the app's
# real input callback. Normal NSRunningApplication termination must exit it.
commands = out / 'reference.lldb'
commands.write_text('\n'.join([
    'settings set auto-confirm true',
    'settings set target.disable-aslr false',
    'target create ' + str(binary),
    'breakpoint set --name UIApplicationMain',
    'run', 'thread backtrace all', 'image list',
    'breakpoint disable', 'continue', 'process status', 'quit',
]) + '\n')


def observe(path, marker, process, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if marker in path.read_text(errors='replace'):
            return
        if process.poll() is not None:
            raise RuntimeError('Reference exited before ' + marker)
        time.sleep(0.2)
    raise TimeoutError('Reference did not reach ' + marker)


for attempt in (1, 2):
    log_path = out / ('lldb-' + str(attempt) + '.log')
    status = 1
    with log_path.open('w') as log:
        process = subprocess.Popen(['xcrun', 'lldb', '--batch', '-s', str(commands)],
                                   stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            observe(log_path, 'ARHELLO_READY', process)
            run('screencapture', '-x', out / ('ready-' + str(attempt) + '.png'))
            run(out / 'playcover-ui', 'click')
            observe(log_path, 'ARHELLO_INPUT increment callback', process, seconds=10)
            run('screencapture', '-x', out / ('clicked-' + str(attempt) + '.png'))
            run(out / 'playcover-ui', 'quit')
            status = process.wait(timeout=15)
            print('REFERENCE_NORMAL_EXIT attempt=' + str(attempt) + ' debuggerStatus=' + str(status), flush=True)
            # LLDB itself can exit zero after a fatal app signal. Preserve the
            # actual inferior exit status, rather than treating LLDB as the app.
            text = log_path.read_text(errors='replace')
            if 'exited with status = 0 ' not in text:
                raise RuntimeError('No observed normal application exit status 0')
            if status:
                raise RuntimeError('Debugger failed during normal exit')
        except (TimeoutError, RuntimeError, subprocess.TimeoutExpired) as error:
            print('REFERENCE_JOURNEY_FAILURE', str(error), flush=True)
            subprocess.run(['screencapture', '-x', str(out / ('failed-' + str(attempt) + '.png'))], timeout=10)
            status = 124 if isinstance(error, (TimeoutError, subprocess.TimeoutExpired)) else 1
        finally:
            if process.poll() is None:
                # Failure cleanup is explicitly separate from normal exit proof.
                subprocess.run([str(out / 'playcover-ui'), 'quit'], timeout=10, check=False)
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
    print(log_path.read_text(errors='replace'), flush=True)
    print('REFERENCE_JOURNEY_STATUS attempt=' + str(attempt) + ' status=' + str(status), flush=True)
    if status:
        raise SystemExit(status)
