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
application's failure. APP_PROCESS_STATUS preserves the real exit status;
APP_STARTUP_INCOMPLETE rejects status 0 before the app's actual window-ready
callback. DARWIN_EXIT_FRAME records a bounded real guest frame-pointer chain.
Receipt markers expose provenance; downloading a report
does not independently verify every artifact listed in a receipt. Raw logs
remain the source for details outside the concise report.

With `--ipc darling`, dyld evidence contains session.log and the exact session
PID/prefix used by the native client. The linked Mach core and bootstrap service
now belong to that same PID; its log includes SESSION_MACH_CORE_STARTED and
SESSION_READY. Older runs retain a separate darling.log/server PID. DARLING_NATIVE_CONNECTED records
actual self-port RPC results. DARLING_RPC_WAIT reads the generated call's saved
header/body from a stopped native frame, or reports unavailable fields as null.
Use `build.sh download --run-id RUN_ID --artifact linux-arm64-apple-dyld-evidence`
when the exact attempt's debugger.runtime.log is needed. This selects only the
textual diagnostic artifact; Apple firmware/cache remain on the runner. Verify
the requested log against its receipt SHA-256 before correlating it. Ordinary
and LLDB attempts use distinct processes/port namespaces: never match numeric
port names across them. IPC_CALLBACK follows real send/continuation/hook objects;
DARLING_PORT_CONSTRUCT/GUARD record the actual kernel-operation input/results.
XPC creation and handler/event callbacks correlate service names with actual
connection pointers within one attempt. EXIT_CALLER_CODE walks the hit thread's
real FP/LR chain and reads bounded caller disassembly/string references. It
must not use LLDB's potentially different selected thread. Run 39383448706
uses this evidence to identify the assertiond process-info disconnect exit;
the observed error object alone does not specify the missing service protocol.

Do not interpret the legacy IPC endpoint array as Darling's server queues;
the snapshot is suppressed when the community backend is active.
DARLING_KQCHAN_EVENT records actual server-filled kevent data, including its
returned receive address, byte count and residual. DARLING_S2C_MEMORY records
real mapping/protection results; a display reply producer marker alone does
not prove delivery. Correlate the genuine client's following request/stack.
If the original RPC server has exited, the workflow preserves darling.log and
reports DARLING_SERVER_UNAVAILABLE instead of trying LLDB against a dead IPC
service. A skipped retry is not a diagnosis or a passing application result.

## Choose evidence for the question

| Question | Existing tool / evidence | Limit |
| --- | --- | --- |
| Why did UIKit throw? | Failure-only LLDB breakpoint at the actual cache's `objc_exception_throw`; read-only Objective-C ivar/CFString decoder | Selected firmware layouts only; unsupported strings report a decoding error. Never use `po` or injected guest methods to obtain the reason. |
| Where did the ordinary attempt crash? | `app.runtime.log`, `NORMAL_RUNTIME_FAULT_PC`, same-cache symbolication and nearby disassembly | LLDB retry can take a different thread interleaving; keep both attempts distinct. Nearest symbols do not prove a function's internal semantics. |
| Which threads are waiting? | Bounded LLDB resume, interruption and all-thread stacks; IPC_WAIT and IPC_QUEUE_SNAPSHOT read saved Mach arguments and queue state | Stopping changes scheduling. A wait timeout is not normal exit. An empty queue identifies a wait, not the missing sender; correlate headers, service lookup and stacks. Optimized-out arguments report a decoding error. |
| Did a Mach/workloop event really arrive? | Runtime event-delivery trace plus receiver/worker stacks | Delivery alone does not prove callback completion or a successful reply. |
| Which service does a framework actually request? | Bounded LLDB bootstrap lookup callbacks, using the actual hit frame for service name, port and caller | No lookup success is fabricated. A NULL bootstrap port is different from a reachable server rejecting a request. |
| Is the Linux transport stuck? | LLDB native frames; targeted `strace` if Linux syscall arguments/timing are needed | Linux syscalls describe the transport, not the guest Darwin operation. Tracing and LLDB both use ptrace; choose separate runs. |
| Does the app itself work with Apple's UI runtime? | Existing PlayCover macOS reference journey, real HID input, screenshots, exit and relaunch | macOS provides services Linux lacks; reference success is not Linux acceptance. |

LLDB supports Python breakpoint scripts using the actual hit frame, stop hooks
and exception APIs. Our ELF process hosts mapped Mach-O code, so native ObjC
plugin discovery and `GetCurrentException()` cannot be assumed to work here.
The currently verified path resolves the throw function from the exact cache
and reads metadata without executing guest code.

Run [39357483030](https://github.com/taowen/arlinux-xnu/actions/runs/39357483030)
also validates the bounded interruption path on a genuinely waiting app:
the ordinary attempt times out (124), LLDB interrupts after 12 seconds and
captures all-thread stacks. That does not establish application progress.
Run [39357931610](https://github.com/taowen/arlinux-xnu/actions/runs/39357931610)
also verifies saved Mach arguments and the queue snapshot: three threads wait
on empty special reply queues. A receive-only buffer's leftover header is not
proof of its original request; use the actual send trace for that relationship.

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

For a UIKit event-loop wait before the application delegate runs, launch
breakpoints read the actual Objective-C method metadata and observe selected
UIApplication/workspace methods. APP_LAUNCH_CALL records the actual thread,
caller, bounded stack and code; missing selected calls are not whole-program
coverage. The signed/unsigned XPC integer setters filter message=11. Run
39391469924 identifies its actual BKSWorkspace.init constructor and
sendToConnection: path. Run 39392250655 records real registration/retirement
for two concurrent clients plus the separate debugger client. A connection or
registration does not establish scene launch, app rendering or input.

APP_LAUNCH_PROTOCOL reads bounded code for the actual workspace dispatcher and
event codecs without calling guest methods. Run 39393971710 identifies signed
message 1 -> _queue_handleLaunch: and the empty base event encoding. Run
39394942049 then verifies real event delivery, the UIKit launch callbacks and
separate replies from two concurrent apps. The decoded response requests task
suspension policy; it is not proof that a scene exists, an app delegate ran or
a process was suspended. Continue at actual main-scene creation.

For create-scene, run 39397655202 verifies actual delivery and the nil-settings
assertion in FBSSceneImpl. The session now treats the exact kernel send-once
cancellation as cancellation, preserving a live service for diagnosis. A zero
audit token must never authenticate an application message or mark a reply
successful. BSSettings object-map values need the real object codec. Executable
cache mappings include strings: codec block identification must require actual
stack-block storage and code prologues, not executable permissions alone. Older
APP_SETTINGS_CODEC_BLOCK labels include literals and require disassembly review.

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

GraphicsServices may report an event-port registration failure by requesting a
stackshot through synchronous XPC, then block in the reporting path. A wait in
CrashReporterSupport therefore does not identify the original registration
error. The runner resolves __GSWriteStackshot by symbol in the same cache and
captures its bounded C-string argument as GRAPHICS_FAILURE before continuing.
The callback neither suppresses the failure nor invokes guest methods. Run
39361738985 verifies its first actual hit: ARHello.gsEvents registration failed
with KERN_FAILURE (5), before synchronous stackshot reporting blocked.
When the individual job-log endpoint is unavailable, report now falls back to
gh run view for the exact run/attempt/job and records the raw log format and
SHA-256; marker parsing removes only the known transport prefix.

For a libxpc BRK, capture_normal_fault now reuses the bounded ADRP/ADD literal
reader as FAULT_REFERENCE. Nearby strings can describe different branches;
match the actual PC and instruction path before naming an assertion. Run
39363495230 verifies the reader at 0x180b1d1b8: the preceding literal says
"Data corruption: Malformed Mach message or kernel bug." The corresponding
DARWIN_MACH_SEND_DATA_REJECTED record shows a valid 1260-byte request rejected
by our old 1024-byte capacity, before any guest buffer read. That local limit
must not masquerade as invalid guest memory. Observe the real queue send/receive
after changing capacity; accepting the input alone is not transport success.

DARWIN_REPLY_PORT_DESTROYED verifies shutdown and dead-name conversion;
DARWIN_REPLY_RECEIVE_DIED separately records an actual blocked receive returning
PORT_DIED. Destruction before a subsequent receive can instead produce
INVALID_NAME. A destruction marker alone does not verify waiter wakeup.

After a fix, compare the actual throwing thread and callers before equating
identical exception text. Run 39372733667 still reports a nil dictionary value,
but the throwing caller has moved from UIKit entitlement handling to BackBoard
HID initialization. EXCEPTION_CALLER_CODE/REFERENCE capture bounded actual caller
instructions/literals. DICTIONARY_NIL_INPUT decodes actual ObjC dictionary ABI
arguments only on a nil value; it identified the missing bundleID. Security
entry callbacks remain read-only. Recursive Python return-breakpoint installation
was removed after LLDB 18 produced autogenerated-callback KeyErrors; historical
SECURITY_RETURN markers must not imply reliable coverage of every return.
Codesign traces include operation, signature bytes and process incarnation.

The pinned community IOKit reference (`darlinghq/darling-iokituser`,
`0b989e3ad71f96fcd28b842ef8ea9bb8214dc7cd`, hid.subproj/IOHIDEventSystemClient.c)
creates a CF runtime object but exposes empty devices/properties and no event
delivery. It is not evidence of usable HID input. Preserve Darling's real Mach
transport and implement required input behavior against actual Wayland events.
