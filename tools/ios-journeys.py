#!/usr/bin/env python3
"""Runner-only real iOS app -> compositor pixels/input/lifecycle journeys.

Invoked by build.sh's workflow, never a workstation build/run entry. No guest
test RPC, injected control actions, mocked framework, stdout-as-visual oracle,
or changed pixels. X11 feeds a real nested Weston seat; all observations come
from its composed screen. Failure cleanup is separate from normal exit proof.
"""
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import re
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/dyld/journeys'


def command(*args, timeout=10, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, timeout=timeout, **kwargs)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def identity(pid):
    proc = Path('/proc') / str(pid)
    stat = (proc / 'stat').read_text()
    return {'pid': pid, 'startTicks': int(stat[stat.rfind(')')+1:].split()[19]),
            'executable': os.readlink(proc / 'exe')}


class Journey:
    def __init__(self, case, apps, session):
        self.case, self.apps, self.session = case, apps, session
        self.directory = OUT / case['id']
        self.directory.mkdir(parents=True, exist_ok=False)
        self.processes, self.streams, self.generations = {}, [], []
        self.steps, self.observations, self.cleanup = [], [], []
        self.current = None

    def guard(self):
        if identity(self.session['pid']) != self.session:
            raise RuntimeError('Shared compatibility session identity changed')
        for name, item in self.processes.items():
            if item['process'].poll() is not None and not item.get('expectedExit'):
                raise RuntimeError(f'{name} exited unexpectedly: {item["process"].returncode}')

    def snapshot(self, label):
        path = self.directory / f'{len(self.observations):03d}-{label}.png'
        command('scrot', path)
        result = command('tesseract', path, 'stdout', '--psm', '11', 'tsv',
                         capture_output=True, text=True)
        path.with_suffix('.tsv').write_text(result.stdout)
        words = [row for row in csv.DictReader(io.StringIO(result.stdout), delimiter='\t')
                 if row['text'].strip()]
        text = ' '.join(word['text'] for word in words)
        path.with_suffix('.txt').write_text(text + '\n')
        self.observations.append({'screenshot': str(path.relative_to(OUT)),
                                  'sha256': sha(path), 'text': text,
                                  'timeMonotonic': time.monotonic()})
        return words, text

    def observe(self, pattern, word=False):
        deadline = time.monotonic() + 8
        regex = re.compile(pattern, re.I)
        while True:
            self.guard()
            words, text = self.snapshot('observe')
            if word:
                matches = [item for item in words if regex.search(item['text'])]
                if len(matches) == 1:
                    return matches[0]
                if len(matches) > 1:
                    raise RuntimeError('Ambiguous visible control: ' + pattern)
            elif regex.search(text):
                return
            if time.monotonic() >= deadline:
                raise RuntimeError('Required compositor-visible effect missing: ' + pattern)
            time.sleep(.5)

    def launch(self, name):
        previous = self.processes.get(name)
        if previous and previous['process'].poll() is None:
            raise RuntimeError('App is still running: ' + name)
        state = self.directory / ('state-' + name)
        state.mkdir(exist_ok=True)  # same state on relaunch; fresh for each case
        number = sum(item['app'] == name for item in self.generations) + 1
        prefix = self.directory / f'{name}-{number}'
        stdout, stderr = prefix.with_suffix('.stdout').open('wb'), prefix.with_suffix('.runtime.log').open('wb')
        self.streams += [stdout, stderr]
        env = os.environ.copy()
        capture = self.directory / f'{name}-{number}-renderer'
        capture.mkdir()
        env['ARLINUX_CAPTURE_DIR'] = str(capture)
        binary = ROOT / self.apps[name]['binary']
        process = subprocess.Popen([str(ROOT / 'build/native/macho-native'), '--dyld',
            str(ROOT / 'build/ios-input/dyld'), '--root', str(ROOT / 'build/ios-input/filesystem/root'),
            str(binary)], cwd=state, env=env, stdout=stdout, stderr=stderr, start_new_session=True)
        record = {'app': name, 'generation': number, 'identity': identity(process.pid),
                  'input': self.apps[name], 'binarySHA256': sha(binary)}
        if previous and record['identity'] == previous['identity']:
            raise RuntimeError('Relaunch reused the original process identity')
        self.generations.append(record)
        self.processes[name] = {**record, 'process': process}
        self.current = name

    def step(self, step):
        action, name = step['do'], step.get('app', self.current)
        if action == 'launch':
            self.launch(name)
        elif action == 'see':
            self.observe(step['text'])
        elif action == 'click':
            target = self.observe(step['text'], word=True)
            x = int(target['left']) + int(target['width'])//2
            y = int(target['top']) + int(target['height'])//2
            command('xdotool', 'mousemove', '--sync', x, y)
            command('xdotool', 'mousedown', '1')
            time.sleep(.08)
            command('xdotool', 'mouseup', '1')
        elif action == 'type':
            command('xdotool', 'type', '--clearmodifiers', '--delay', '40', step['text'])
        elif action == 'close':
            command('xdotool', 'key', '--clearmodifiers', 'alt+F4')
        elif action == 'exit':
            item = self.processes[name]
            item['expectedExit'] = True
            status = item['process'].wait(timeout=5)
            if status != 0:
                raise RuntimeError(f'Normal close returned {status}, expected 0')
            self.steps[-1]['exitStatus'] = status
        elif action == 'alive':
            item = self.processes[name]
            if item['process'].poll() is not None or identity(item['process'].pid) != item['identity']:
                raise RuntimeError('Peer identity/lifetime was disrupted: ' + name)
        elif action == 'crash':
            item = self.processes[name]
            item['expectedExit'] = True
            os.kill(item['process'].pid, signal.SIGKILL)
            if item['process'].wait(timeout=5) != -signal.SIGKILL:
                raise RuntimeError('Requested crash was not observed')
            time.sleep(.4)
        else:
            raise RuntimeError('Unknown journey action: ' + action)

    def diagnose(self):
        # Read the failed original process, not a replay that changes scheduling.
        live = next((item for item in self.processes.values() if item['process'].poll() is None), None)
        if not live:
            return
        self.current = live['app']
        pid = live['process'].pid
        commands = ['settings set auto-confirm true', 'thread backtrace all --count 24',
            'script import importlib.util,sys; spec=importlib.util.spec_from_file_location("dyld_debugger",'
            + repr(str(ROOT / 'tools/dyld-debugger.py')) + '); helper=importlib.util.module_from_spec(spec); '
            'sys.modules[spec.name]=helper; spec.loader.exec_module(helper)',
            'script helper.capture_ipc_waits(lldb.debugger.GetSelectedTarget().GetProcess())',
            'register read pc sp x0 x1 x2 x3 x4 x5 x6 x7', 'process detach']
        with (self.directory / 'original-process.lldb.log').open('wb') as log:
            args = ['sudo', '-n', 'timeout', '--kill-after=2', '10', 'lldb-18', '--batch', '--attach-pid', str(pid)]
            for item in commands:
                args += ['-o', item]
            result = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, timeout=15)
        self.diagnostic = {'identity': live['identity'], 'status': result.returncode}

    def run(self):
        error = None
        self.diagnostic = None
        try:
            for step in self.case['steps']:
                if error:
                    self.steps.append({**step, 'status': 'blocked', 'reason': 'Earlier required step failed'})
                    continue
                self.steps.append({**step, 'status': 'running'})
                started = time.monotonic()
                try:
                    self.guard()
                    self.step(step)
                    self.steps[-1]['status'] = 'passed'
                except (RuntimeError, OSError, subprocess.SubprocessError) as failure:
                    error = str(failure)
                    self.steps[-1].update(status='failed', reason=error)
                    try:
                        self.snapshot('failure')
                        self.diagnose()
                    except (RuntimeError, OSError, subprocess.SubprocessError) as diagnostic_error:
                        self.diagnostic = {'error': str(diagnostic_error)}
                self.steps[-1]['seconds'] = round(time.monotonic() - started, 3)
        finally:
            for name, item in self.processes.items():
                process = item['process']
                forced = process.poll() is None
                if forced:
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=2)
                self.cleanup.append({'app': name, 'identity': item['identity'], 'forced': forced,
                                     'status': process.returncode, 'acceptanceEvidence': False})
            for stream in self.streams:
                stream.close()
        return {'id': self.case['id'], 'status': 'failed' if error else 'passed', 'error': error,
                'session': self.session, 'processes': self.generations, 'steps': self.steps,
                'observations': self.observations, 'diagnostic': self.diagnostic, 'cleanup': self.cleanup}


def main():
    if sys.platform != 'linux' or platform.machine() not in ('aarch64', 'arm64') or os.environ.get('GITHUB_ACTIONS') != 'true':
        raise SystemExit('Real app journeys execute only on the ARM64 GitHub runner through build.sh')
    manifest_path = ROOT / 'journeys/ios-apps.json'
    manifest = json.loads(manifest_path.read_text())
    OUT.mkdir(parents=True, exist_ok=False)
    session = identity(int(os.environ['ARLINUX_SESSION_PID']))
    results = []
    for case in manifest['cases']:
        result = Journey(case, manifest['apps'], session).run()
        results.append(result)
        print('IOS_JOURNEY ' + json.dumps({'id': result['id'], 'status': result['status'], 'error': result['error']}), flush=True)
    result = {'version': 1, 'source': os.environ['GITHUB_SHA'], 'runId': os.environ['GITHUB_RUN_ID'],
              'attempt': os.environ['GITHUB_RUN_ATTEMPT'], 'manifestSHA256': sha(manifest_path),
              'backend': 'native-darling-wayland-x11', 'deviceAcceptance': False,
              'passed': bool(results) and all(item['status'] == 'passed' for item in results), 'cases': results}
    (OUT / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('IOS_SUITE ' + json.dumps({'passed': result['passed'], 'cases': len(results),
                                   'result': 'build/dyld/journeys/result.json'}), flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
