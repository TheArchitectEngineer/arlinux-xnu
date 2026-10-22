"""Observe one owned Apple-host launch through exec; never replay the app."""
import json
import re
import signal
import subprocess
import time
from pathlib import Path


def trace(debugger, output):
    import lldb
    process=debugger.GetSelectedTarget().GetProcess()
    debugger.SetAsync(True)
    records=[];deadline=time.monotonic()+8
    process.Continue()
    try:
        while time.monotonic()<deadline:
            state=process.GetState()
            if state in (lldb.eStateExited,lldb.eStateDetached,lldb.eStateInvalid):
                records.append({'state':lldb.SBDebugger.StateAsCString(state)})
                break
            if state!=lldb.eStateStopped:
                time.sleep(.02);continue
            threads=list(process)
            reasons=[thread.GetStopReason() for thread in threads]
            # The initial diagnostic Python process execs the original app.
            # Continue only debugger exec events or its explicit SIGSTOP.
            meaningful=[thread for thread in threads if thread.GetStopReason() not in
                (lldb.eStopReasonNone,lldb.eStopReasonInvalid,lldb.eStopReasonExec)
                and not (thread.GetStopReason()==lldb.eStopReasonSignal and
                    thread.GetStopReasonDataCount() and thread.GetStopReasonDataAtIndex(0)==signal.SIGSTOP)]
            if not meaningful:
                records.append({'event':'exec-or-launch-stop','reasons':reasons})
                process.Continue();time.sleep(.02);continue
            record={'event':'actual-stop','pid':process.GetProcessID(),'threads':[]}
            # LLDB's image notification breakpoint can fail during exec into
            # an iOS-on-Mac image. Keep the OS's actual file-backed mappings
            # so unknown frame addresses can still be matched to the binary.
            mappings=[]
            try:
                maps=subprocess.run(['/usr/bin/vmmap','-w',str(process.GetProcessID())],
                    capture_output=True,text=True,timeout=5)
                path=Path(output).with_suffix('.vmmap.log')
                path.write_text((maps.stdout+maps.stderr)[:2*1024*1024])
                record['memoryMap']={'path':path.name,'status':maps.returncode}
                for line in maps.stdout.splitlines():
                    match=re.match(r'^__TEXT\s+([0-9a-fA-F]+)-([0-9a-fA-F]+)\s+.*?\s(/.+)$',line)
                    if match:mappings.append((int(match[1],16),int(match[2],16),match[3]))
            except (OSError,subprocess.SubprocessError) as error:
                record['memoryMap']={'error':str(error)}
            for thread in threads[:16]:
                frames=[]
                for frame in list(thread)[:24]:
                    module=frame.GetModule().GetFileSpec()
                    value={'pc':hex(frame.GetPC()),'function':frame.GetFunctionName(),
                        'module':str(module),'fileAddress':hex(frame.GetPCAddress().GetFileAddress())}
                    for start,end,path in mappings:
                        if start<=frame.GetPC()<end:
                            value.update(mappedImage=path,imageOffset=hex(frame.GetPC()-start),imageBase=hex(start))
                            break
                    frames.append(value)
                record['threads'].append({'id':thread.GetThreadID(),
                    'reason':thread.GetStopDescription(512),'frames':frames,
                    'registers':{name:thread.GetFrameAtIndex(0).FindRegister(name).GetValue()
                        for name in ('pc','sp','x0','x1','x2','x3','x4','x5','x6','x7')}})
            # Resolve only the actual fault thread, batched by at most four
            # real file-backed images. No guest expression/function is called.
            budget=time.monotonic()+4;groups={}
            for thread in record['threads']:
                if not thread['reason']:continue
                for frame in thread['frames'][:12]:
                    if 'mappedImage' in frame:groups.setdefault((frame['mappedImage'],frame['imageBase']),[]).append(frame)
                break
            for (path,base),frames in list(groups.items())[:4]:
                if time.monotonic()>=budget or not Path(path).is_file():continue
                try:
                    resolved=subprocess.run(['/usr/bin/atos','-arch','arm64','-o',path,'-l',base,
                        *[frame['pc'] for frame in frames]],capture_output=True,text=True,timeout=max(.1,budget-time.monotonic()))
                    if resolved.returncode==0:
                        for frame,symbol in zip(frames,resolved.stdout.splitlines()):frame['mappedSymbol']=symbol
                except (OSError,subprocess.SubprocessError) as error:
                    record['symbolicationError']=str(error)
            records.append(record)
            break
        else:records.append({'event':'observation-deadline','seconds':8})
    finally:
        if process.GetState() in (lldb.eStateRunning,lldb.eStateStepping):
            process.Stop()
        if process.GetState()==lldb.eStateStopped:
            result=process.Detach()
            records.append({'event':'detach','success':result.Success(),'error':str(result)})
        Path(output).write_text(json.dumps(records,indent=2)+'\n')
