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
                self.diagnostic_target=name
                raise RuntimeError(f'{name} faulted; collecting original-process diagnosis')
            if not self.reference:
                trace=Path(item['runtime'])
                if trace.exists():
                    with trace.open('rb') as stream:
                        stream.seek(item.get('faultScanOffset',0));new=stream.read();item['faultScanOffset']=stream.tell()
                    observed=item.get('faultScanTail',b'')+new
                    item['faultScanTail']=observed[-64:]
                    if b'DARWIN_FAULT signal=' in observed or b'DARLING_RPC_FATAL ' in observed:
                        item['faultObserved']=True
                        self.diagnostic_target=name
                        raise RuntimeError(f'{name} faulted; collecting original-process diagnosis')
            if item['process'].poll() is not None and not item.get('expectedExit'):
                self.diagnostic_target=name
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
            '--service-index',str(ROOT/'build/ios-input/xpc-services.plist'),
            '--container',str(state.resolve()),
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
            stderr=Path(item['stdout']).with_suffix('.stderr')
            if stderr.is_file():arguments.extend(['--stderr',stderr])
            if 'APP_WORKER_QUEUE stage=background' in Path(item['stdout']).read_text(errors='replace'):
                for symbol in ('_dispatch_main_q','_dispatch_get_main_queue_port_4CF',
                    '_dispatch_main_queue_callback_4CF','_dispatch_runloop_queue_poke','_dispatch_main_queue_wakeup'):
                    arguments.extend(['--lookup','/usr/lib/system/libdispatch.dylib',symbol])
            if 'fileSystemRepresentationWithPath:]: nil or empty path argument' in stderr.read_text(errors='replace'):
                for symbol in ('_CheckSandboxAccess','-[TIImageCacheClient initWithLocalAccess:]'):
                    arguments.extend(['--lookup','/System/Library/PrivateFrameworks/TextInput.framework/TextInput',symbol])
                arguments.extend(['--list-symbols','/System/Library/PrivateFrameworks/GraphicsServices.framework/GraphicsServices','HardwareKeyboard'])
            if 'IOS_COLOR_LOOKUP_FAILED ' in stderr.read_text(errors='replace'):
                arguments.extend(['--lookup','/System/Library/PrivateFrameworks/CoreUI.framework/CoreUI',
                    '-[CUIDesignLibraryCatalog initWithURL:error:]'])
            if 'DARLING_UNHANDLED_SERVICE_INLINE' in runtime.read_text(errors='replace'):
                arguments.extend(['--lookup','/System/Library/Frameworks/QuartzCore.framework/QuartzCore','__ZL14query_displaysv'])
                arguments.extend(['--lookup','/System/Library/Frameworks/QuartzCore.framework/QuartzCore','__ZN2CA7Display7Display6updateEv'])
            if 'unsupported bootstrap trust predicate layout' in Path(item['stdout']).with_suffix('.stderr').read_text(errors='replace'):
                arguments.append('--bootstrap-context')
            if 'unable to locate IOSurface kernel service' in Path(item['stdout']).with_suffix('.stderr').read_text(errors='replace'):
                # The original Web fault reaches its maximum-surface query,
                # before any surface allocation. Resolve that actual boundary,
                # including WebCore's own platform-configuration setter.
                arguments.extend(['--lookup-bytes','1024'])
                for symbol in ('__ZN7WebCore9IOSurface11maximumSizeEv',
                               '__ZN7WebCore9IOSurface14setMaximumSizeENS_7IntSizeE',
                               '__ZN7WebCore9IOSurface20bytesPerRowAlignmentEv',
                               '__ZN7WebCore9IOSurface23setBytesPerRowAlignmentEm'):
                    arguments.extend(['--lookup','/System/Library/PrivateFrameworks/WebCore.framework/WebCore',symbol])
                for symbol in ('_IOSurfaceGetPropertyAlignment','_IOSurfaceClientGetPropertyAlignment'):
                    arguments.extend(['--lookup','/System/Library/Frameworks/IOSurface.framework/IOSurface',symbol])
            try:
                result=subprocess.run(list(map(str,arguments)),capture_output=True,timeout=15)
                runtime.with_suffix('.symbolication.log').write_bytes(result.stdout+result.stderr)
                if result.returncode:raise RuntimeError('Original-process symbolication failed; inspect '+str(runtime.with_suffix('.symbolication.log')))
            except (RuntimeError,OSError,subprocess.SubprocessError) as error:
                self.diagnostic['collectionErrors'].append(str(error))
                # Independent original-process stacks must still be collected.
        target=getattr(self,'diagnostic_target',self.current)
        candidates = sorted(self.processes.values(), key=lambda item: item['app'] != target)
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
        commands.insert(-1,'target variable native_mach_wakeups native_mach_wakeups_omitted')
        queue_addresses={int(record['address'],16) for record in symbols if record.get('symbol')=='_dispatch_main_q'}
        # The fixture logs the actual public dispatch_get_main_queue() object.
        # This keeps metadata collection useful even when a private symbol is absent.
        queue_addresses.update(int(value,16) for value in re.findall(
            r'APP_WORKER_QUEUE stage=background[^\n]* main_queue=(0x[0-9a-f]+)',Path(live['stdout']).read_text(errors='replace')))
        for address in sorted(queue_addresses)[:2]:
            try:maps=Path(f'/proc/{pid}/maps').read_text()
            except OSError as error:
                self.diagnostic['collectionErrors'].append('Dispatch metadata maps unavailable: '+str(error))
                continue  # Optional metadata cannot suppress original-process stacks.
            if any(int(begin,16)<=address and address+128<=int(end,16) and permissions[:3]=='rw-'
                   for begin,end,permissions in re.findall(r'^([0-9a-f]+)-([0-9a-f]+) ([-rwxps]{4}) ',maps,re.M)):
                commands.insert(-1,f'memory read --format x --size 8 --count 16 {address:#x}')
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
            target='failed-step app' if live['app']==target else 'surviving peer')
        command(sys.executable,ROOT/'tools/symbolicate-cache.py',cache,
                self.directory/'original-process.lldb.log',self.directory/'original-process.symbols.json',capture_output=True)
        self.diagnose_services(cache)

    def diagnose_services(self,cache):
        # A real helper has started, but the app still lacks its requested
        # effect. Inspect those original workers and their owning session;
        # app-only stacks cannot distinguish loading from a service wait.
        log=self.output/(self.case['id']+'--session')/'session.log'
        if not log.is_file():return
        started=re.findall(r'^SESSION_SERVICE_STARTED owner=(\d+) incarnation=(\d+) service=(\S+) instance=(\d+) pid=(\d+) start_ticks=(\d+) ',
            log.read_text(errors='replace'),re.M)
        if not started:return
        peers=[('session',self.session)]
        # Prefer a live original process for each actual service. An exited
        # early retry must not consume the bounded attachment budget.
        selected=[];services=set()
        for owner,incarnation,service,instance,pid,ticks in reversed(started):
            if service in services:continue
            expected={'pid':int(pid),'startTicks':int(ticks),
                'executable':str(ROOT/'build/native/macho-native')}
            try:
                if identity(expected['pid'])!=expected:continue
            except (OSError,ProcessLookupError):continue
            selected.append((instance,expected));services.add(service)
            if len(selected)==2:break
        for instance,expected in selected:
            peers.append(('service-'+instance,expected))
        observations=[]
        for name,expected in peers:
            record={'kind':name,'identity':expected};observations.append(record)
            try:
                if identity(expected['pid'])!=expected:
                    record['unavailable']='original process identity changed';continue
                prefix=self.directory/name
                path=prefix.with_suffix('.lldb.log')
                with path.open('wb') as stream:
                    args=['sudo','-n','timeout','--kill-after=1','6','lldb-18','--batch',
                        '--attach-pid',str(expected['pid'])]
                    for action in ('settings set auto-confirm true','thread backtrace all --count 16',
                                   'register read pc sp x0 x1 x2 x3','process detach'):
                        args+=['-o',action]
                    result=subprocess.run(args,stdout=stream,stderr=subprocess.STDOUT,timeout=9)
                record.update(status=result.returncode,log=str(path.relative_to(self.output)))
                command(sys.executable,ROOT/'tools/symbolicate-cache.py',cache,path,
                    prefix.with_suffix('.symbols.json'),capture_output=True)
            except ProcessLookupError:
                record['unavailable']='original process exited'
            except FileNotFoundError as error:
                if str(error.filename).startswith('/proc/'):
                    record['unavailable']='original process exited'
                else:record['collectionError']=str(error)
            except (OSError,RuntimeError,subprocess.SubprocessError) as error:
                record['collectionError']=str(error)
        self.diagnostic['serviceProcesses']=observations
        self.diagnostic['serviceProcessesOmitted']=max(0,len(started)-len(selected))

    def symbolize_retired_services(self):
        # The session is now retired and its log closed. Symbolicating while
        # helpers still execute can miss the actual exit/fault during teardown.
        if self.reference or self.diagnostic is None:return
        log=self.output/(self.case['id']+'--session')/'session.log'
        if not log.is_file() or 'SESSION_SERVICE_STARTED ' not in log.read_text(errors='replace'):return
        cache=ROOT/'build/ios-input/filesystem/root/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64'
        try:
            arguments=[sys.executable,ROOT/'tools/symbolicate-cache.py',cache,log,
                self.directory/'service-runtime.symbols.json']
            session_text=log.read_text(errors='replace')
            if 'BOOTSTRAP_XPC_PACKED_UNIMPLEMENTED id=40000325 ' in session_text:
                image='/usr/lib/system/libxpc.dylib'
                arguments+=['--lookup-fragment',image,'__xpc_bootstrap_routine_check_in',
                    '--lookup-fragment',image,'__xpc_connection_check_in',
                    '--lookup',image,'_bootstrap_check_in3',
                    '--lookup',image,'__xpc_interface_routine']
            if 'BOOTSTRAP_XPC_PACKED_UNIMPLEMENTED id=400002c7 ' in session_text or \
               'BOOTSTRAP_XPC_PACKED_UNIMPLEMENTED id=400002ce ' in session_text:
                # Original helpers requested bootstrap/service metadata. Read
                # their actual reply consumers before implementing wire keys;
                # this is tied to that observed failure, not a symbol dump for
                # every app or an invented managed-service flag.
                image='/usr/lib/system/libxpc.dylib'
                for symbol in ('_xpc_copy_bootstrap','__xpc_copy_xpcservice_dictionary',
                               '__fetch_xpcservice_info','__xpc_dictionary_create_bootstrap_copy'):
                    arguments+=['--lookup',image,symbol]
                arguments+=['--list-symbols',image,'xpcservice']
            if 'DARWIN_EXIT_FRAME ' in session_text and 'service=com.apple.WebKit.Networking ' in session_text:
                # Run 39572965048 resolves the original exit branch to these
                # two consumers, not an entitlement failure or missing launch.
                image='/System/Library/Frameworks/WebKit.framework/WebKit'
                for symbol in ('__ZN6WebKit29XPCServiceInitializerDelegate23getConnectionIdentifierERN3IPC10Connection10IdentifierE',
                               '__ZN6WebKit29XPCServiceInitializerDelegate19getClientIdentifierERN3WTF6StringE'):
                    arguments+=['--lookup',image,symbol]
            if 'service=com.apple.WebKit.WebContent ' in session_text:
                # Full run 39600777191 reaches actual rendering stream creation
                # after the database wait. Read the failed buffer consumer.
                arguments+=['--lookup',
                    '/System/Library/Frameworks/WebKit.framework/WebKit',
                    '__ZN3IPC22StreamClientConnection6createEj']
            command(*arguments,capture_output=True)
        except (OSError,RuntimeError,subprocess.SubprocessError) as error:
            self.diagnostic['collectionErrors'].append('Service runtime symbolication: '+str(error))
