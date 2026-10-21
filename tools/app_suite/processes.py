import json
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
        try:session_identity=identity(self.session['pid'])
        except OSError as error:
            raise RuntimeError(f'Shared compatibility session {self.session["pid"]} exited; inspect its retained session.log') from error
        if session_identity != self.session:
            raise RuntimeError('Shared compatibility session identity changed')
        for name, item in self.processes.items():
            if item.get('faultObserved'):
                raise RuntimeError(f'{name} faulted; collecting original-process diagnosis')
            if not self.reference:
                trace=Path(item['runtime'])
                if trace.exists():
                    with trace.open('rb') as stream:
                        stream.seek(item.get('faultScanOffset',0));new=stream.read();item['faultScanOffset']=stream.tell()
                    observed=item.get('faultScanTail',b'')+new
                    item['faultScanTail']=observed[-64:]
                    if b'DARWIN_FAULT signal=' in observed:
                        item['faultObserved']=True
                        raise RuntimeError(f'{name} faulted; collecting original-process diagnosis')
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
        env['ARLINUX_WINDOW_MODE']='fullscreen'
        env['ARLINUX_TRACE_FD'] = str(trace.fileno())
        if not self.reference:
            env['ARLINUX_STOP_ON_FAULT']='1'
            env['DYLD_PRINT_LIBRARIES']='1'
            env['DYLD_PRINT_SEGMENTS']='1'
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
        self.diagnostic={'collectionErrors':[]}
        cache = ROOT / 'build/ios-input/filesystem/root/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64'
        for item in self.processes.values():
            runtime = Path(item['runtime'])
            arguments=[sys.executable,ROOT/'tools/symbolicate-cache.py',cache,runtime,runtime.with_suffix('.symbols.json')]
            if 'DARLING_UNHANDLED_SERVICE_INLINE' in runtime.read_text(errors='replace'):
                arguments.extend(['--lookup','/System/Library/Frameworks/QuartzCore.framework/QuartzCore','__ZL14query_displaysv'])
                arguments.extend(['--lookup','/System/Library/Frameworks/QuartzCore.framework/QuartzCore','__ZN2CA7Display7Display6updateEv'])
            if 'unsupported bootstrap trust predicate layout' in Path(item['stdout']).with_suffix('.stderr').read_text(errors='replace'):
                arguments.append('--bootstrap-context')
            if 'unable to locate IOSurface kernel service' in Path(item['stdout']).with_suffix('.stderr').read_text(errors='replace'):
                arguments.extend(['--lookup-bytes','4096'])
                for symbol in ('__Z29init_max_renderable_iosurfacePv','_CABackingStoreUpdate_',
                    '__ZL29CABackingStorePrepareUpdates_P14CABackingStoremmjjjyPN2CA14GenericContextEP11UpdateState'):
                    arguments.extend(['--lookup','/System/Library/Frameworks/QuartzCore.framework/QuartzCore',symbol])
            try:
                result=subprocess.run(list(map(str,arguments)),capture_output=True,timeout=15)
                runtime.with_suffix('.symbolication.log').write_bytes(result.stdout+result.stderr)
                if result.returncode:raise RuntimeError('Original-process symbolication failed; inspect '+str(runtime.with_suffix('.symbolication.log')))
            except (RuntimeError,OSError,subprocess.SubprocessError) as error:
                self.diagnostic['collectionErrors'].append(str(error))
                # Independent original-process stacks must still be collected.
        candidates = sorted(self.processes.values(), key=lambda item: item['app'] != self.current)
        live = next((item for item in candidates if item['process'].poll() is None), None)
        if not live:
            return
        pid = live['process'].pid
        commands = ['settings set auto-confirm true', 'thread backtrace all --count 24',
                    'register read pc sp x0 x1 x2 x3 x4 x5 x6 x7', 'image list', 'process detach']
        # Exact original Atlas fault: retain its source-defined Image metadata,
        # not Mach-O bytes or an arbitrary heap dump. The actual x19 object
        # must lie entirely in a writable non-executable original VM range.
        runtime=Path(live['runtime']).read_text(errors='replace')
        symbol_path=Path(live['runtime']).with_suffix('.symbols.json')
        try:symbols=json.loads(symbol_path.read_text())
        except (OSError,ValueError) as error:
            self.diagnostic['collectionErrors'].append('Runtime symbols unavailable: '+str(error))
            symbols=[]
        if any('Atlas5Image2mlEv' in record.get('nearestSymbol','') and 'faultContext' in record for record in symbols):
            register=re.search(r'DARWIN_FAULT_REGISTER index=0x0*13 pc=\S+ result=(0x[0-9a-f]+)',runtime)
            if register:
                address=int(register[1],16)
                for begin,end,permissions in re.findall(r'^([0-9a-f]+)-([0-9a-f]+) ([-rwxps]{4}) ',runtime,re.M):
                    if int(begin,16)<=address and address+192<=int(end,16) and permissions[:3]=='rw-':
                        commands.insert(-1,f'memory read --format x --size 8 --count 24 {address:#x}')
                        # FileRecord::_path is the source-defined pointer at
                        # Image+0x30. Read only a bounded C string inside the
                        # original writable non-executable mappings.
                        ranges=[(int(b,16),int(e,16)) for b,e,p in re.findall(
                            r'^([0-9a-f]+)-([0-9a-f]+) ([-rwxps]{4}) ',runtime,re.M) if p[:3]=='rw-']
                        commands.insert(-1,'script '+
                            f't=lldb.debugger.GetSelectedTarget(); p=t.GetProcess(); e=lldb.SBError(); '+
                            f'a=int.from_bytes(p.ReadMemory({address+0x30},8,e),"little"); '+
                            f'n=next((min(1024,end-a) for begin,end in {ranges!r} if begin<=a<end),0); '+
                            'print("ATLAS_IMAGE_PATH",p.ReadCStringFromMemory(a,n,e) if n else "outside writable metadata maps",e)')
                        break
        info=re.findall(r'DARLING_DYLD_INFO bytes=\S+ pc=(0x[0-9a-f]+) result=0x0+ ',runtime)
        if info:
            script=ROOT/'tools/app_suite/dyld_metadata.py'
            commands.insert(-1,'script import runpy; '+
                f'runpy.run_path({str(script)!r})["collect"](lldb.debugger.GetSelectedTarget(),{int(info[-1],16)},{live["runtime"]!r})')
        with (self.directory / 'original-process.lldb.log').open('wb') as log:
            args = ['sudo', '-n', 'timeout', '--kill-after=2', '10', 'lldb-18', '--batch', '--attach-pid', str(pid)]
            for item in commands:
                args += ['-o', item]
            result = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, timeout=15)
        self.diagnostic.update(identity=live['identity'],status=result.returncode,
            target='current app' if live['app']==self.current else 'surviving peer')
        command(sys.executable,ROOT/'tools/symbolicate-cache.py',cache,
                self.directory/'original-process.lldb.log',self.directory/'original-process.symbols.json',capture_output=True)
