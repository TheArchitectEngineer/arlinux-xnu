"""Observe one owned Apple-host launch through exec; never replay the app."""
import json
import signal
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
            for thread in threads[:16]:
                frames=[]
                for frame in list(thread)[:24]:
                    module=frame.GetModule().GetFileSpec()
                    frames.append({'pc':hex(frame.GetPC()),'function':frame.GetFunctionName(),
                        'module':str(module)})
                record['threads'].append({'id':thread.GetThreadID(),
                    'reason':thread.GetStopDescription(512),'frames':frames})
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
