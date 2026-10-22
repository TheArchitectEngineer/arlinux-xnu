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
    def stop_kernel_traces(self):
        for state in getattr(self,'kernel_traces',[]):
            if not state.is_file():continue
            with state.with_suffix('.cleanup.log').open('ab') as log:
                try:
                    subprocess.run(['sudo','-n','--preserve-env=GITHUB_ACTIONS',sys.executable,
                        str(ROOT/'tools/app_suite/uprobe_trace.py'),'stop',str(state)],
                        stdout=log,stderr=subprocess.STDOUT,timeout=10,check=True)
                except (OSError,subprocess.SubprocessError) as failure:
                    log.write(('KERNEL_TRACE_CLEANUP_FAILED '+str(failure)+'\n').encode())

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
        if not self.reference:
            log=self.output/(self.case['id']+'--session')/'session.log'
            with log.open('rb') as stream:
                stream.seek(getattr(self,'session_fault_offset',0));new=stream.read()
                self.session_fault_offset=stream.tell()
            observed=getattr(self,'session_fault_tail',b'')+new
            self.session_fault_tail=observed[-64:]
            if b'SESSION_NATIVE_FAULT ' in observed:
                self.session_fault_observed=True
            if getattr(self,'session_fault_observed',False):
                raise RuntimeError('Shared compatibility session faulted; collecting its original core threads')
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
        if not self.generations:self.configure_display()
        previous = self.processes.get(name)
        if previous and previous['process'].poll() is None:
            raise RuntimeError('App is still running: ' + name)
        state = self.directory / ('state-' + name)
        state.mkdir(exist_ok=True)  # same state on relaunch; fresh for each case
        groups=self.directory/'group-containers'
        groups.mkdir(exist_ok=True)  # shared by peers in this case, survives relaunch
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
            self.infrastructure_failure=True
            raise RuntimeError('App input failed: '+name+'; '+app.get('error','inspect its build log'))
        binary = ROOT / app['binary']
        kernel_symbols=app.get('kernelTraceSymbols',[]) if not self.reference and env.get('ARLINUX_DIAGNOSE_LIFECYCLE')=='1' else []
        if kernel_symbols:
            diagnostic_started=time.monotonic()
            script=ROOT/'tools/app_suite/uprobe_trace.py'
            specification=prefix.with_suffix('.kernel-symbols.json')
            cache_path=ROOT/'build/ios-input/filesystem/root/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64'
            specification.write_text(json.dumps([{**point,
                **({'cachePath':str(cache_path)} if point.get('dyldCacheImage') else
                   {'path':str(binary.parent/point['image'])})}
                for point in kernel_symbols],indent=2)+'\n')
            state_path=prefix.with_suffix('.kernel-trace.json')
            if not hasattr(self,'kernel_traces'):self.kernel_traces=[]
            self.kernel_traces.append(state_path)
            setup_error=None
            with prefix.with_suffix('.kernel-setup.log').open('wb') as log:
                try:
                    result=subprocess.run(['sudo','-n','--preserve-env=GITHUB_ACTIONS',sys.executable,str(script),'start',str(specification),str(state_path)],
                        stdout=log,stderr=subprocess.STDOUT,timeout=15)
                    log.write(f'\nKERNEL_TRACE_SETUP_EXIT status={result.returncode}\n'.encode())
                    if result.returncode:
                        setup_error=f'collector exited {result.returncode}'
                    else:
                        observed=json.loads(state_path.read_text())
                        if observed.get('status')!='active':
                            setup_error=observed.get('error') or 'collector did not enable tracing'
                except (OSError,ValueError,subprocess.SubprocessError) as error:
                    setup_error=str(error)
                    log.write(('KERNEL_TRACE_SETUP_UNAVAILABLE '+setup_error+'\n').encode())
            self.steps[-1]['diagnosticSeconds']=round(time.monotonic()-diagnostic_started,3)
            if setup_error:
                # An explicitly requested observation cannot answer its question
                # without a collector. Do not run an unobserved app to timeout.
                self.infrastructure_failure=True
                raise RuntimeError('Requested kernel observation unavailable before app launch: '+setup_error+
                    '; inspect '+str(prefix.with_suffix('.kernel-setup.log')))
        trace_symbols=app.get('diagnosticSymbols',[]) if not self.reference and env.get('ARLINUX_DIAGNOSE_LIFECYCLE')=='1' else []
        if trace_symbols:env['ARLINUX_DEBUG_ENTRY_STOP']='1'
        if app.get('input',{}).get('kind') in ('firmware','ipa'):
            env['ARLINUX_INSPECT_APPLICATION']='1'
        arguments=[str(ROOT / 'build/native/macho-native'), '--role', app['kind'], '--dyld',
            str(ROOT / 'build/ios-input/dyld'), '--root', str(ROOT / 'build/ios-input/filesystem/root'),
            '--service-index',str(ROOT/'build/ios-input/xpc-services.plist'),
            '--container',str(state.resolve()),
            '--group-root',str(groups.resolve()),
            str(binary)]
        if self.reference:
            binary=ROOT/'build/reference-apps'/(app['name']+'.app')/app['name']
            arguments=[str(binary)]
            # Match the Linux launcher's real per-journey application home.
            # CoreFoundation prefers CFFIXED_USER_HOME to the account's passwd
            # home; cwd/HOME alone left original apps using the runner's home.
            for relative in ('Documents','tmp','Library/Caches','Library/Preferences','Library/Application Support'):
                (state/relative).mkdir(parents=True,exist_ok=True)
            env.update(HOME=str(state.resolve()),CFFIXED_USER_HOME=str(state.resolve()),
                TMPDIR=str((state/'tmp').resolve())+'/')
            if app.get('input',{}).get('kind')=='ipa':
                # Original IPA references are observed externally. An injected
                # UIKit timer must not initialize UIApplication before the app.
                env.pop('DYLD_INSERT_LIBRARIES',None)
            elif app.get('input',{}).get('kind')=='firmware':
                env['DYLD_INSERT_LIBRARIES']=str(ROOT/'build/reference-tools/libarlinux-inspection.dylib')
        reference_trace=self.reference and app.get('input',{}).get('kind')=='ipa' and env.get('ARLINUX_DIAGNOSE_LIFECYCLE')=='1'
        if reference_trace:
            # This unprivileged Python process stops before exec. LLDB follows
            # the same PID into the original executable, catching early dyld /
            # runtime traps that happen before an observer constructor exists.
            insertion=env.pop('DYLD_INSERT_LIBRARIES','')
            script='import os,signal,sys; os.kill(os.getpid(),signal.SIGSTOP); os.environ["DYLD_INSERT_LIBRARIES"]=sys.argv[1]; os.execv(sys.argv[2],sys.argv[2:])'
            arguments=[sys.executable,'-c',script,insertion,str(binary)]
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
        if reference_trace:
            diagnostic_started=time.monotonic()
            output=prefix.with_suffix('.apple-startup.json')
            script=ROOT/'tools/app_suite/reference_startup_trace.py'
            invocation=('script import runpy; '+f'runpy.run_path({str(script)!r})["trace"](lldb.debugger,{str(output)!r})')
            try:
                deadline=time.monotonic()+5
                while True:
                    if process.poll() is not None:raise RuntimeError('Reference launch exited before debugger attachment')
                    status=command('ps','-o','stat=','-p',process.pid,capture_output=True,text=True).stdout
                    if 'T' in status:break
                    if time.monotonic()>=deadline:raise RuntimeError('Reference launch did not stop before exec')
                    time.sleep(.02)
                with prefix.with_suffix('.startup.lldb.log').open('wb') as log:
                    result=subprocess.run(['sudo','-n','/usr/bin/lldb','--batch','--attach-pid',str(process.pid),
                        '-o','settings set auto-confirm true','-o',invocation],stdout=log,stderr=subprocess.STDOUT,timeout=24)
                if result.returncode or not output.is_file():
                    self.infrastructure_failure=True
                    raise RuntimeError('Apple launch debugger failed; inspect startup.lldb.log')
                record['startupTrace']=str(output.relative_to(self.output))
                record['diagnosticLaunch']=True
                if process.poll() is None:
                    record['identity']=identity(process.pid)
                    self.processes[name]['identity']=record['identity']
            finally:
                if process.poll() is None:os.kill(process.pid,signal.SIGCONT)
                self.steps[-1]['diagnosticSeconds']=round(time.monotonic()-diagnostic_started,3)
        if trace_symbols:
            diagnostic_started=time.monotonic()
            # The same owned original process stops before dyld. The debugger
            # records real entry/callback execution, never calls either one.
            specification=prefix.with_suffix('.trace-symbols.json')
            specification.write_text(json.dumps(trace_symbols))
            script=ROOT/'tools/app_suite/original_trace.py'
            invocation=('script import runpy; '+f'runpy.run_path({str(script)!r})["trace"]('
                f'lldb.debugger,{str(specification)!r},{str(prefix.with_suffix(".runtime.log"))!r},'
                f'{str(prefix.with_suffix(".stderr"))!r})')
            with prefix.with_suffix('.startup.lldb.log').open('wb') as output:
                try:
                    # Attaching immediately races the loader's cache preparation:
                    # an early collector detach can precede the requested stop.
                    # Require both the actual entry record and kernel stop state.
                    deadline=time.monotonic()+30
                    while True:
                        if process.poll() is not None:raise RuntimeError('Original process exited before diagnostic entry stop')
                        status=Path(f'/proc/{process.pid}/status').read_text()
                        ready='NATIVE_DARWIN_ENTRY entry=' in prefix.with_suffix('.stderr').read_text(errors='replace')
                        if ready and re.search(r'^State:\s+T\s',status,re.M):break
                        if time.monotonic()>=deadline:raise RuntimeError('Original diagnostic entry stop was not observed within 30 seconds')
                        time.sleep(.02)
                    observed=subprocess.run(['sudo','-n','timeout','--kill-after=2','30','lldb-18','--batch',
                        '--attach-pid',str(process.pid),'-o','settings set auto-confirm true','-o',invocation],
                        stdout=output,stderr=subprocess.STDOUT,timeout=34)
                    output.write(f'\nSTARTUP_TRACE_EXIT status={observed.returncode}\n'.encode())
                except (OSError,subprocess.SubprocessError) as error:
                    output.write(f'\nSTARTUP_TRACE_UNAVAILABLE {error}\n'.encode())
                finally:
                    # A collector failure must not leave the app stopped.
                    if process.poll() is None:os.kill(process.pid,signal.SIGCONT)
                    self.steps[-1]['diagnosticSeconds']=round(time.monotonic()-diagnostic_started,3)
        if self.reference and app['kind']=='application':
            result=subprocess.run([str(ROOT/'build/reference-tools/desktop-events'),'focus',str(process.pid)],capture_output=True,timeout=7)
            if result.returncode:
                stderr.write(b'REFERENCE_FOCUS_PENDING '+result.stderr);stderr.flush()
            if number==1:
                for dialog in self.case.get('reference',{}).get('startupDialogs',[]):
                    deadline=time.monotonic()+5
                    while True:
                        self.guard()
                        observation=json.loads(command(ROOT/'build/reference-tools/desktop-events','press',process.pid,
                            dialog['button'],capture_output=True,text=True).stdout)
                        self.steps[-1]['referenceDialog']=observation
                        if not observation['trusted']:
                            self.infrastructure_failure=True
                            raise RuntimeError('Apple external accessibility permission unavailable')
                        if observation['found']:
                            if observation.get('actionError')!=0:raise RuntimeError('External native-dialog button press failed')
                            break
                        if time.monotonic()>=deadline:
                            if not dialog.get('optional'):raise RuntimeError('Native startup dialog button missing: '+dialog['button'])
                            break
                        time.sleep(.2)

    def diagnose(self):
        # Read the failed original process, not a replay that changes scheduling.
        if self.reference:
            self.diagnostic={'collectionErrors':[],'appleCrashReport':None}
            name=getattr(self,'diagnostic_target',self.current)
            item=self.processes.get(name)
            if not item or item['process'].poll() is None:return
            app=self.apps[name];expected=ROOT/'build/reference-apps'/(app['name']+'.app')/app['name']
            reports=Path.home()/'Library/Logs/DiagnosticReports';deadline=time.monotonic()+8
            # ReportCrash publishes asynchronously. Read only this owned PID
            # and executable, never unrelated host reports or another run.
            while True:
                for path in sorted(reports.glob(app['name']+'*.ips'),reverse=True)[:8]:
                    try:
                        if path.stat().st_size>16*1024*1024:continue
                        text=path.read_text();header,end=json.JSONDecoder().raw_decode(text)
                        report=json.loads(text[end:]) if text[end:].strip() else header
                        if report.get('pid')!=item['process'].pid or report.get('procPath')!=str(expected):continue
                        retained={key:report[key] for key in ('pid','procName','procPath','captureTime',
                            'osVersion','exception','termination','faultingThread','threads','usedImages',
                            'asi','lastExceptionBacktrace') if key in report}
                        output=self.directory/'apple-crash.json'
                        output.write_text(json.dumps(retained,indent=2)+'\n')
                        self.diagnostic['appleCrashReport']={'path':str(output.relative_to(self.output)),
                            'sha256':sha(output),'exception':retained.get('exception'),
                            'termination':retained.get('termination')}
                        return
                    except (OSError,ValueError) as error:
                        if len(self.diagnostic['collectionErrors'])<4:self.diagnostic['collectionErrors'].append(str(error))
                if time.monotonic()>=deadline:break
                time.sleep(.2)
            self.diagnostic['reportUnavailable']='No matching OS crash report within 8 seconds'
            return
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
        # Deep image decode stacks can consume the ordinary per-thread bound
        # before reaching the calling framework. Extend only the selected
        # stopped thread, leaving the other threads at their existing limit.
        commands = ['settings set auto-confirm true', 'thread backtrace all --count 24',
                    'thread backtrace --count 64',
                    'register read pc sp x0 x1 x2 x3 x4 x5 x6 x7', 'image list', 'process detach']
        frame_directory=self.directory/f"{live['app']}-{live['generation']}-renderer"
        frame_script=ROOT/'tools/app_suite/dyld_metadata.py'
        commands.insert(-1,'script import runpy; '+f'runpy.run_path({str(frame_script)!r})["capture_frame"]('
            f'lldb.debugger.GetSelectedTarget(),{str(frame_directory)!r})')
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
        commands.insert(-1,'target variable guest_next native_guest_gap_requests native_guest_mapping_conflicts')
        commands.insert(-1,'target variable native_workqueue_narrow_observations')
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
        arguments=[sys.executable,ROOT/'tools/symbolicate-cache.py',cache,
                self.directory/'original-process.lldb.log',self.directory/'original-process.symbols.json']
        kernel_state=Path(str(live['runtime']).removesuffix('.runtime.log')+'.kernel-trace.json')
        if kernel_state.is_file():
            observed=json.loads(kernel_state.read_text())
            if observed.get('status')=='recorded' and any(point.get('role')=='dispatch-submit' for point in observed.get('symbols',[])):
                # Match bounded receiver words to this firmware's actual queue
                # layout and worker-selection code, not a source-version guess.
                for symbol in ('__dispatch_worker_thread2','__dispatch_root_queue_drain','__dispatch_root_queue_poke_slow'):
                    arguments+=['--lookup','/usr/lib/system/libdispatch.dylib',symbol]
                arguments+=['--lookup-bytes','2048']
        session_log=self.output/(self.case['id']+'--session')/'session.log'
        if session_log.is_file() and 'name=com.apple.containermanagerd' in session_log.read_text(errors='replace'):
            # A real original app asked for the missing shared-container service.
            # Read its Foundation consumer and C boundary, not unrelated symbols
            # or a synthetic call that supplies the missing URL.
            arguments+=['--objc-method','/System/Library/Frameworks/Foundation.framework/Foundation',
                'NSFileManager','containerURLForSecurityApplicationGroupIdentifier:',
                '--lookup-fragment','/usr/lib/system/libsystem_containermanager.dylib',
                'container_create_or_lookup_app_group_path_by_app_group_identifier']
        if os.environ.get('ARLINUX_DIAGNOSE_LIFECYCLE')=='1' and 'IOS_POINTER_DELIVER ' in Path(live['stdout']).with_suffix('.stderr').read_text(errors='replace'):
            image='/System/Library/PrivateFrameworks/UIKitCore.framework/UIKitCore'
            for selector in ('_addTouch:forDelayedDelivery:','_clearTouches'):
                arguments+=['--objc-method',image,'UITouchesEvent',selector]
        command(*arguments,capture_output=True)
        self.diagnose_services(cache)

    def diagnose_services(self,cache):
        # A real helper has started, but the app still lacks its requested
        # effect. Inspect those original workers and their owning session;
        # app-only stacks cannot distinguish loading from a service wait.
        log=self.output/(self.case['id']+'--session')/'session.log'
        if not log.is_file():return
        session_text=log.read_text(errors='replace')
        started=re.findall(r'^SESSION_SERVICE_STARTED owner=(\d+) incarnation=(\d+) service=(\S+) instance=(\d+) pid=(\d+) start_ticks=(\d+) ',
            session_text,re.M)
        if not started and 'SESSION_NATIVE_FAULT ' not in session_text:return
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
            if 'service=com.apple.WebKit.GPU ' in session_text:
                # Shared stream mapping now works; inspect original rendering policy.
                image='/System/Library/Frameworks/WebKit.framework/WebKit'
                arguments+=['--lookup-fragment',image,'setUseGPUProcessForDOMRendering',
                    '--objc-method',image,'WKPreferences','_features',
                    '--objc-method',image,'WKPreferences','_setEnabled:forFeature:']
            command(*arguments,capture_output=True)
        except (OSError,RuntimeError,subprocess.SubprocessError) as error:
            self.diagnostic['collectionErrors'].append('Service runtime symbolication: '+str(error))
