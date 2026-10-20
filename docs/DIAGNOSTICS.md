# Diagnosing the actual application journey

Use the existing `build.sh` entry. Start with `status --run-id RUN_ID`, then
`report --run-id RUN_ID --job dyld-userland`. Report reads an existing Actions
run; it never builds, dispatches or executes guest code on the workstation.
It records the run's source commit and latest attempt, failed step, actual
application status, LLDB status, exception text, original fault PC and receipt
markers. Full logs and their SHA-256 accompany the JSON report under
`build/runs/RUN_ID/attempt-N/`. Omit `--job` to include all jobs.

These are observed outputs, not a second acceptance framework. Missing logs
remain unavailable; compilation errors remain failures even without app
markers. Report prints only actual line-start markers, not echoed commands.
LLDB status 0 means the diagnostic command completed; it does not change an
application's failure. Receipt markers expose provenance; downloading a report
does not independently verify every artifact listed in a receipt. Raw logs
remain the source for details outside the concise report.

## Choose evidence for the question

| Question | Existing tool / evidence | Limit |
| --- | --- | --- |
| Why did UIKit throw? | Failure-only LLDB breakpoint at the actual cache's `objc_exception_throw`; read-only Objective-C ivar/CFString decoder | Selected firmware layouts only; unsupported strings report a decoding error. Never use `po` or injected guest methods to obtain the reason. |
| Where did the ordinary attempt crash? | `app.runtime.log`, `NORMAL_RUNTIME_FAULT_PC`, same-cache symbolication and nearby disassembly | LLDB retry can take a different thread interleaving; keep both attempts distinct. Nearest symbols do not prove a function's internal semantics. |
| Which threads are waiting? | Bounded LLDB resume, interruption and all-thread stacks | Stopping changes scheduling. A wait timeout is not normal exit; forced interruption still needs an actual hanging-run validation. |
| Did a Mach/workloop event really arrive? | Runtime event-delivery trace plus receiver/worker stacks | Delivery alone does not prove callback completion or a successful reply. |
| Which service does a framework actually request? | Bounded LLDB bootstrap lookup callbacks, using the actual hit frame for service name, port and caller | No lookup success is fabricated. A NULL bootstrap port is different from a reachable server rejecting a request. |
| Is the Linux transport stuck? | LLDB native frames; targeted `strace` if Linux syscall arguments/timing are needed | Linux syscalls describe the transport, not the guest Darwin operation. Tracing and LLDB both use ptrace; choose separate runs. |
| Does the app itself work with Apple's UI runtime? | Existing PlayCover macOS reference journey, real HID input, screenshots, exit and relaunch | macOS provides services Linux lacks; reference success is not Linux acceptance. |

LLDB supports Python breakpoint scripts using the actual hit frame, stop hooks
and exception APIs. Our ELF process hosts mapped Mach-O code, so native ObjC
plugin discovery and `GetCurrentException()` cannot be assumed to work here.
The currently verified path resolves the throw function from the exact cache
and reads metadata without executing guest code.

Record/replay would help scheduling failures, but is not a ready replacement:
rr documents support for selected ARM CPUs (Apple M1+), not arbitrary hosted
ARM runners. No rr replay has been validated in this project. Do not describe
ordinary reruns as deterministic replay.

## Improve the next iteration

Read the report and the relevant raw stack before changing runtime behavior.
Name one hypothesis and the real-app observation that would distinguish it.
For an exception, capture the throw reason before adding more syscall logging.
For a wait, identify sender, receiver, queue and actual Linux TID before
changing scheduling or returning success. Retain the ordinary failure as well
as the debugger's earlier stop. Avoid parallel diagnostic builders and broad
logging that obscures the causal event.

Run `build --mode app` only after pushing the intended source; continue
observing that exact run through local timeouts. Reading existing logs needs
no rebuild. Keep the full console prerequisites and app journey: faster
diagnosis must not weaken acceptance. No new cache scheme is introduced here;
any future setup optimization must still validate its declared inputs/outputs.

The verified improvement in run
[39348154790](https://github.com/taowen/arlinux-xnu/actions/runs/39348154790)
is a concrete exception reason: `NSInternalInconsistencyException`,
`backboardd isn't running -- or we couldn't talk to it -- result:10000003`.
The normal attempt aborts with status 133; the debugger retry stops at the
throw. This narrows the next investigation to the display-service connection,
but does not implement a working display service or Linux GUI.

Follow-up run [39349359592](https://github.com/taowen/arlinux-xnu/actions/runs/39349359592)
verifies the bootstrap callback: the actual display service name is
`com.apple.backboard.display.services`, queried with bootstrap port 0.
The same exception is captured afterward. The ordinary attempt remains status
133, both dynamic console journeys pass, and LLDB completes evidence capture.
The callback module must be imported into LLDB's interpreter namespace; merely
placing it in Python sys.modules did not activate callbacks in the preceding
attempt. Callbacks auto-continue and have a per-breakpoint hit bound.

## Primary references

- [LLDB breakpoint scripts](https://lldb.llvm.org/use/tutorials/breakpoint-triggered-scripts.html)
- [LLDB stop hooks](https://lldb.llvm.org/use/tutorials/extending-target-stop-hooks.html)
- [LLDB thread exception APIs](https://lldb.llvm.org/python_api/lldb.SBThread.html)
- [GitHub job logs and attempt-scoped jobs](https://docs.github.com/en/rest/actions/workflow-jobs)
- [strace manual](https://man7.org/linux/man-pages/man1/strace.1.html)
- [rr hardware and recording limitations](https://rr-project.org/)

The native Wayland path is now exercised by the same app in
[39351144939](https://github.com/taowen/arlinux-xnu/actions/runs/39351144939).
WAYLAND_BACKEND_READY follows configure, real wl_shm submission and a compositor
frame callback. DARWIN_DISPLAY_QUERY records the actual geometry served to the
genuine framework. The next throw is `main display is nil`, alongside a lookup
for CARenderServer. The backend buffer is blank and never counts as an app
screenshot. Weston runs headless with pixman on the runner; deployment should
connect to ARLinux's existing compositor through WAYLAND_DISPLAY/WAYLAND_SOCKET.
No ARLinux device integration has been validated.

Display-list follow-up [39353289697](https://github.com/taowen/arlinux-xnu/actions/runs/39353289697)
passes the nil-main-display assertion and reaches a bounds assertion.
DARWIN_RENDER_DISPLAY_LIST records the actual mapping and byte count.
Method inspection uses the real class metadata, including objc4's low two
method-list fixup bits. Adjacent ADRP/ADD references are read as bounded raw
bytes: arbitrary code references must not be handed to LLDB 18's CString SWIG
binding, which raised SystemError on a binary block in run 39352538927.
LLDB command success alone does not prove every optional decoder ran.
