"""Runner-only helpers for the existing real-application LLDB diagnosis."""
from pathlib import Path
import re
import time
import lldb


def resume_bounded(process, seconds=12):
    debugger = process.GetTarget().GetDebugger()
    asynchronous = debugger.GetAsync()
    listener = lldb.SBListener('darwin-journey-resume')
    listener.StartListeningForEvents(process.GetBroadcaster(), lldb.SBProcess.eBroadcastBitStateChanged)
    debugger.SetAsync(True)
    try:
        error = process.Continue()
        if error.Fail():
            raise RuntimeError(str(error))
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            event = lldb.SBEvent()
            if not listener.WaitForEvent(1, event):
                continue
            state = lldb.SBProcess.GetStateFromEvent(event)
            if state == lldb.eStateStopped and lldb.SBProcess.GetRestartedFromEvent(event):
                continue
            if state in (lldb.eStateStopped, lldb.eStateExited, lldb.eStateCrashed, lldb.eStateDetached):
                return
        print('DIAGNOSTIC_WAIT_INTERRUPTED after', seconds, 'seconds; this is not normal app exit', flush=True)
        error = process.Stop()
        if error.Fail():
            raise RuntimeError(str(error))
        deadline = time.monotonic() + 3
        while process.GetState() != lldb.eStateStopped and time.monotonic() < deadline:
            time.sleep(0.05)
        if process.GetState() != lldb.eStateStopped:
            raise RuntimeError('Unable to interrupt the actual waiting process')
    finally:
        listener.StopListeningForEvents(process.GetBroadcaster(), lldb.SBProcess.eBroadcastBitStateChanged)
        debugger.SetAsync(asynchronous)


def capture_normal_fault(target):
    """The ordinary attempt and LLDB retry can fail on different threads."""
    path = Path('build/dyld/app.runtime.log')
    if not path.exists():
        path = Path('build/dyld/runtime-1.log')
    if not path.exists():
        return
    lines = path.read_text(errors='replace').splitlines()
    addresses = set()
    for index, line in enumerate(lines[:-1]):
        if 'DARWIN_NATIVE_FAULT signal/pc/address follows' in line:
            match = re.search(r'pc=(0x[0-9a-fA-F]+)', lines[index+1])
            if match:
                addresses.add(int(match[1], 16))
    for address in sorted(addresses):
        print('NORMAL_RUNTIME_FAULT_PC', hex(address), flush=True)
        target.GetDebugger().HandleCommand('disassemble --start-address ' + hex(max(0, address-128)) + ' --count 64')
