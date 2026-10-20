import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
from .context import ROOT,command,sha,identity

class Processes:
    def helper_processes(self, name):
        if self.reference:
            raise RuntimeError('iOS extension process identity is not certified by the Apple-host reference')
        binary = str(ROOT / self.apps[name]['helper'])
        matches = []
        for proc in Path('/proc').iterdir():
            if not proc.name.isdecimal():
                continue
            try:
                arguments = (proc / 'cmdline').read_bytes().split(b'\0')
                if binary.encode() not in arguments:
                    continue
                if b'--role' not in arguments or arguments[arguments.index(b'--role')+1] != self.apps[name].get('helperRole','service').encode():
                    continue
                environment = (proc / 'environ').read_bytes().split(b'\0')
                expected = ('ARLINUX_DARLING_PID=' + str(self.session['pid'])).encode()
                if expected not in environment:
                    continue
                candidate = identity(int(proc.name))
                if candidate['executable'] != str(ROOT / 'build/native/macho-native'):
                    continue
                matches.append(candidate)
            except (OSError, ValueError):
                continue  # process vanished or is not readable by this runner
        return matches

    def guard(self):
        if identity(self.session['pid']) != self.session:
            raise RuntimeError('Shared compatibility session identity changed')
        for name, item in self.processes.items():
            if item['process'].poll() is not None and not item.get('expectedExit'):
                raise RuntimeError(f'{name} exited unexpectedly: {item["process"].returncode}')

    def launch(self, name):
        previous = self.processes.get(name)
        if previous and previous['process'].poll() is None:
            raise RuntimeError('App is still running: ' + name)
        state = self.directory / ('state-' + name)
        state.mkdir(exist_ok=True)  # same state on relaunch; fresh for each case
        number = sum(item['app'] == name for item in self.generations) + 1
        prefix = self.directory / f'{name}-{number}'
        stdout = prefix.with_suffix('.stdout').open('wb')
        stderr = prefix.with_suffix('.stderr').open('wb')
        trace = prefix.with_suffix('.runtime.log').open('wb')
        self.streams += [stdout, stderr, trace]
        env = self.environment.copy()
        env['ARLINUX_TRACE_FD'] = str(trace.fileno())
        capture = self.directory / f'{name}-{number}-renderer'
        capture.mkdir()
        env['ARLINUX_CAPTURE_DIR'] = str(capture)
        app = self.apps[name]
        if app['build'] != 'passed':
            raise RuntimeError('App build failed: '+name+'; inspect ios-app-inputs/'+name+'.build.log')
        binary = ROOT / app['binary']
        arguments=[str(ROOT / 'build/native/macho-native'), '--role', app['kind'], '--dyld',
            str(ROOT / 'build/ios-input/dyld'), '--root', str(ROOT / 'build/ios-input/filesystem/root'),
            str(binary)]
        if self.reference:
            binary=ROOT/'build/reference-apps'/(app['name']+'.app')/app['name']
            arguments=[str(binary)]
        process = subprocess.Popen(arguments, cwd=state, env=env, stdout=stdout, stderr=stderr,
            stdin=subprocess.PIPE, pass_fds=(trace.fileno(),), start_new_session=True)
        record = {'app': name, 'generation': number,
                  'identity': {'pid': process.pid, 'startTicks': None, 'executable': None},
                  'input': self.apps[name], 'binarySHA256': sha(binary)}
        # Own the process before reading /proc: an early crash must still be
        # reaped and retained as a launch failure, not orphaned by diagnosis.
        self.processes[name] = {**record, 'process': process, 'runtime': str(prefix.with_suffix('.runtime.log')), 'stdout': str(prefix.with_suffix('.stdout'))}
        record['identity'] = identity(process.pid)
        if previous and record['identity'] == previous['identity']:
            raise RuntimeError('Relaunch reused the original process identity')
        self.generations.append(record)
        self.processes[name] = {**record, 'process': process, 'runtime': str(prefix.with_suffix('.runtime.log')), 'stdout': str(prefix.with_suffix('.stdout'))}
        self.current = name
        if self.reference and app['kind']=='application':
            result=subprocess.run([str(ROOT/'build/reference-tools/desktop-events'),'focus',str(process.pid)],capture_output=True,timeout=7)
            if result.returncode:
                stderr.write(b'REFERENCE_FOCUS_PENDING '+result.stderr);stderr.flush()

    def diagnose(self):
        # Read the failed original process, not a replay that changes scheduling.
        if self.reference:
            return  # actual exit/status and screenshot retained; no Linux cache/LLDB on Apple host
        cache = ROOT / 'build/ios-input/filesystem/root/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64'
        for item in self.processes.values():
            runtime = Path(item['runtime'])
            command(sys.executable, ROOT / 'tools/symbolicate-cache.py', cache, runtime,
                    runtime.with_suffix('.symbols.json'), capture_output=True)
        candidates = sorted(self.processes.values(), key=lambda item: item['app'] != self.current)
        live = next((item for item in candidates if item['process'].poll() is None), None)
        if not live:
            return
        pid = live['process'].pid
        commands = ['settings set auto-confirm true', 'thread backtrace all --count 24',
                    'register read pc sp x0 x1 x2 x3 x4 x5 x6 x7', 'image list', 'process detach']
        with (self.directory / 'original-process.lldb.log').open('wb') as log:
            args = ['sudo', '-n', 'timeout', '--kill-after=2', '10', 'lldb-18', '--batch', '--attach-pid', str(pid)]
            for item in commands:
                args += ['-o', item]
            result = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, timeout=15)
        command(sys.executable,ROOT/'tools/symbolicate-cache.py',cache,
                self.directory/'original-process.lldb.log',self.directory/'original-process.symbols.json',capture_output=True)
        self.diagnostic = {'identity': live['identity'], 'status': result.returncode,
                           'target': 'current app' if live['app'] == self.current else 'surviving peer'}
