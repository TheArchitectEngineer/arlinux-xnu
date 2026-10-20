# Experiment contract

## Current handoff

The active target is a modifiable Darwin/iOS compatibility environment on
Linux/Android. Native ARM64, 16 KiB guest pages and one advertised CPU remain
fixed. Original XNU boot is a behavioral reference, not an application gate.
No third-party iOS app, Linux UI or Android integration has passed.

Current application: **ARHello 1.0** (`io.arlinux.ARHello`), a programmatic
UIKit window, label and increment button. Its interfaces/TBD files contain only
ABI declarations; real implementations come from the selected iOS firmware.
The app's code and bundle metadata are committed under guests/ARHello.

Latest application attempt:
[39351144939](https://github.com/taowen/arlinux-xnu/actions/runs/39351144939),
source `fe9f1d65366642abaede2f3e286f025e0d6d22bc`, dyld job `118395522325`.
The host child connects directly to headless Weston, configures a 1024x640
xdg-shell surface and submits an actual wl_shm buffer. A real frame callback
precedes endpoint registration. This buffer is still blank: no application
content or input has been presented.

The actual bootstrap lookup now grants the registered display endpoint's send
right. Genuine BackBoardServices sends screen-info MIG routine 6001004 and
accepts the 52-byte reply containing configured dimensions, scale 1 and
rotation 0. The prior backboardd connection assertion is passed. The next actual
exception is NSInternalInconsistencyException, `main display is nil`, from
BKSDisplayServicesStart +668; the app also queries the unimplemented
`com.apple.CARenderServer`. The ordinary app still aborts with status 133;
LLDB_COMMAND_STATUS=0 describes evidence capture only. A backend surface is
not a working CADisplay/CoreAnimation render server.

Both independent real-libSystem console journeys still pass: worker return 37,
shared memory 42, input counter 1 and normal exit. Real workloop event delivery
also repeats. Linux UIKit display/input/normal close/relaunch and the ARLinux
integration journey remain incomplete.

### Verified execution

- The freestanding Mach-O console performs real 16 KiB VM operations, TLS,
  fork/COW, exact child status 37, input, parent exit 0 and independent relaunch.
  Original milestone: [39320163457](https://github.com/taowen/arlinux-xnu/actions/runs/39320163457),
  source `809b6abcb14e99224b4ae5c10cbdecce5510778c`.
- Genuine iOS 12.5.7 dyld passes page geometry, TSD setup, entropy, executable
  startup parameters, explicit permissive loading policy and real clocks.
- The cache's three regions are genuinely mapped at their declared addresses
  into private Linux mappings. Version-2 pointer chains are decoded at slide 0.
  1439 Mach-O images are inspected and 4147 TLS instructions are adapted;
  executable permissions are restored only after rewriting and cache flush.
  Aliased image addresses are deduplicated to avoid rewriting an instruction
  twice. This is not general proof for every code/data mixture or later JIT.
- shared_region_check returns an address only after those mappings exist.
  Apple signature authentication is explicitly disabled in this compatibility
  policy; the firmware input is SHA-256 verified. No fake signature-registration
  or shared-region success is used to substitute for mapping/relocation.
- Current-task Mach VM allocate, deallocate and anonymous vm_map reach real
  Linux memory operations. Unsupported targets, flags and map modes fail.
- Host priority-info and Clock service lookup complete actual MIG queue
  send/receive exchanges; the real pthread/libc clients accept their replies.
  Semaphores created by libc have real eventfd counters. A task's empty Bootstrap
  slot returns its actual NULL state; no launchd/XPC service is fabricated.
- Legacy iOS pointer layouts require low guest addresses. Heap/stack mappings
  now stay below the 36-bit commpage, with real alignment masks honored.
  Previously Linux auto-placement at 0xff6a2e0005c0 was masked by Objective-C
  to an unmapped 0x7f6a2e000580. The actual low-address run passes that point.
- The genuine dynamic C main prints DYNAMIC_MAIN through real libSystem write,
  obtains/touches memory through real malloc, creates/joins a worker, verifies
  DYNAMIC_THREAD joined value=37 memory=42, then handles input and prints
  DYNAMIC_CONSOLE counter=1 normal return. No substitute pthread library is used.
- Registered pthread callbacks run with per-thread guest TLS, private Linux
  signal/runtime stacks and a real current-thread port. The child restores the
  parent's pre-SIGSYS signal mask before entering guest code; inheriting the
  temporary handler mask previously killed the child on its first trapped call.
  Darwin ulocks use real Linux futex wait/wake, and thread termination uses
  SYS_exit rather than ending the whole process.
- LLDB resolves actual dyld symbols, hits a real policy-function breakpoint,
  reads registers, steps an instruction and captures the fatal library stack.
  Cache-frame symbolication uses the same firmware's real Mach-O/local symbol
  tables and records the nearest available symbol, not an invented function.

### Current boundary and next action

Continue at QuartzCore/CADisplay and the actual CARenderServer client protocol.
Connect a real display object and rendering path to the Wayland framebuffer;
do not supply a fabricated CADisplay object or mark a blank surface as UIKit
output. The currently supported display RPC is screen geometry only. All
other display routines fail with MIG_BAD_ID. Native lookup replaces launchd
name discovery at the public API boundary; it does not implement launchd's
XPC dictionary protocol. Unsupported names, flags and target-PID lookups fail.

The screen ABI was established from this firmware's actual client and
GSSetMainScreenInfo disassembly (run 39350427317): 24-byte request, 52-byte
reply, four binary32 outputs; scale divides pixel dimensions and rotation
feeds a transform. There are no device-model/version address tables. The
current Wayland client chooses buffer scale 1 and the normal transform.
The backend runs in a separate host process with its own TLS and event loop;
Linux child-state checks reject lookups/queries after it exits. Resize,
input routing, app frame submission and normal window close remain unfinished.
See [execution design](ARCHITECTURE.md) for the service/Wayland boundary.

The earlier special-reply fix establishes actual waiting in ipc_message/ppoll
and workloop delivery. The older 0x1808bf9d4 stack access is absent in later
observed attempts; that is not proof against every lifetime race. Complex
Mach descriptors, full reply/voucher-right transfer, workloop SYNC_WAIT/WAKE,
turnstile inheritance and queue retirement remain incomplete. Forced-interrupt
diagnostics still need an actual hanging-run validation. No run is pending.

ABI sources: libpthread-330.250.2
`b4b8795e1d4030f45efe04947773b265240ce03c`, XNU's pinned ipc_entry/event/voucher
sources, and libdispatch-1173.100.2
`007e3cc2e720293070b6e93b6541595f601cc1d3` for the workloop code absent from the
older public source snapshot. These are references; the selected iOS binary's
actual requests determine required wire behavior.

The macOS reference now completes **two actual GUI journeys**:
[39338697656](https://github.com/taowen/arlinux-xnu/actions/runs/39338697656),
source `2cffea293786eeb28826291a8eba7a5b24ab199a`, job `118354771378`.
The identical iOS input is converted by pinned, unchanged PlayCover code and
ad-hoc signed; no PlayTools is injected. Actual HID mouse events produce the
app callback and a screenshot displaying Count: 1. Normal NSRunningApplication
termination yields inferior exit 0 under LLDB. Relaunch starts at Count: 0,
then a second click and normal exit repeat successfully. All three inspected
clicked/relaunch screenshots agree with the app logs. macOS success does not
establish Linux/Android compatibility.

Original input hash matches the Linux app hash below; converted/signed hash is
`5b08e8cf4c36e5894825b254bc3de4e43058da9fc77c407fff948e6b196e340f`.
Reference receipt: `b2e4e987dea4a4840b0f125b56c425bb42ec05490c0aed87cbae41b76c26bc62`.
Local text/screenshots: build/runs/39338697656. Earlier targeted mouse events
never reached the button; the accepted run uses the actual macOS HID queue.

Latest app failure receipt:
`600f226b072e0d99bc6011103774387f90701f10a3d63cf34c752960269c4217`.
Native runtime SHA-256: `a737f3c8952ebe84f7b5a17ebe6a408bd27dcdf3e8144788c057ae56f35ab51d`.
Local text: build/runs/39351144939/attempt-1/job-118395522325.log
and job-118395345767.log. Both independent real-libSystem
console launches still complete. The earlier first worker run 39340039442
reached the voucher assertion; its receipt is
`71a285b07a8e6423117eaff487e91342f323db11930de04b66d431d6077c12e2`.

Intermediate port-options failure receipt (39342992718):
`269a8ebe54039b15834af66f3e349cc7dc3b5ca573c5ad359cea2f4ebc110edb`.
The preceding 39342902242 attempt failed compilation due to a duplicate C tag;
it did not execute the app and is not runtime evidence.

### Reproducible inputs and evidence

- Firmware: iPhone7,2 / iOS 12.5.7 / 16H81, from the Apple URL in sources.lock.json.
- IPSW SHA-256: `7c10ff0e4cdbe24f33012e47e8458a8ae8c858f2e3dbfce766c196f5d229cadb`.
- OS DMG SHA-256: `7040c19bd5b62182ccf6449ca562bbeb6071f4e404138ace93c5f731d1a9e5a7`.
- dyld SHA-256: `5c5563ec91c78cfe060cb500e3ca8d764350572f124f98144bac175aa25655e9`.
- Cache: 1276837888 bytes, SHA-256 `a60a0653eb730f73b5d1de61a97e86e5a241f7a4ff00c83659f9eefb965cac6f`.
- ARHello 1.0 Mach-O SHA-256: `4b346aa7dd998425990cb9ef282ddb7e5252b4d9d069b12c369d32d407f29b77`.
- Failed app attempt receipt SHA-256: `746ebdd5a489cde0e40c12c10378a11f4bcbd7abb884896e800ace99d721ec4e`.
- Failed application text: build/runs/39336911768/dyld-job.log. Actual LLDB frames
  are symbolicated against the same cache; the stop is libsystem_pthread
  _pthread_workqueue_supported +44, reached from UIKitCore/libdispatch.
- Dynamic C Mach-O SHA-256: `2b66ddec09f799b483a2e97b86b859ed5cc1e0474a9c6afb197dde17cf3327ca`.
- Native runtime SHA-256: `a4b010f189b6edf98b728d79519a5049a60230e2dbffb5f0e652fca4281eed46`.
- Successful dyld receipt SHA-256:
  `1c210c9872d50435a4a75eede5c180d1e2aa30634ccf61dfe321c8d21aef014f`.
- Local text: build/runs/39336200293/dyld-job.log. Run artifacts retain verified
  input/cache metadata, native executable hashes and runtime logs. Apple firmware,
  cache and executable files remain transient on the runner.

ABI reference sources also inspected locally: libpthread-330.250.2 commit
`b4b8795e1d4030f45efe04947773b265240ce03c` and Libc-1272.250.1 commit
`c175362543a5e715f0152c5f0164f12951346375`. Registration fields are read from
actual userland metadata rather than assuming a specific pthread object layout.

The cache format reference inspected locally is Apple dyld-655.1, source
5fe128e4e6a5966ebe01bfaf3bbffdfb17da1390. It informs format interpretation;
the actual 650.3.4 binary/runtime evidence determines observed ABI behavior.
The fcntl command 97 was observed before preloading existed (run 39328635511).
Its variadic wrapper had not yet materialized its third argument in x2; this
must not be mistaken for the raw syscall ABI. Preloading now replaces this
kernel loading responsibility with genuine host mappings.

Earlier milestones/failures remain in Actions and Git history: real dyld entry
39322206750; missing thread ID/entropy/startup path/AMFI; real clocks
39325147072; live debugger 39326720941; real cache reads and Mach VM allocation
39328108372; cache initialization 39328927723; vm_map 39329330536. The original
XNU full-system VM/COW/exit reference is run 39311945725, not Linux adaptation
or application acceptance. The removed kernel candidate is preserved in Git.
macOS reference tracing and the ARHello window are now observed in the run
above; Linux and macOS both use the root build.sh entry.

## Evidence boundaries

| Experiment | Last established result | Does not establish |
| --- | --- | --- |
| `native-userland` | [Native console: VM/COW/TLS/input/normal exit/restart](https://github.com/taowen/arlinux-xnu/actions/runs/39320163457) | dyld, genuine frameworks, iOS UI App or Android integration |
| `dyld-userland` | [Real cached libSystem, malloc, pthread create/join, input, normal exit and independent relaunch](https://github.com/taowen/arlinux-xnu/actions/runs/39336200293) | Third-party App, UI frameworks or Android integration |
| `host-substrate` | [ARM64 ptrace/register and 16 KiB mapping probe](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU VM or Android sandbox operation |
| `macho-userland` | [Freestanding ARM64 Mach-O output/error/exit via host shim](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU execution, dyld or frameworks |
| `xnu-baseline` + `xnu-reference-boot` | [Full-system XNU exec, memory/COW, child exit and parent wait](https://github.com/taowen/arlinux-xnu/actions/runs/39311945725) | Userspace XNU, dyld, iOS frameworks or apps |

These are historical observations at their runs' source revisions, not promises
about current HEAD. No Android app-sandbox journey or third-party iOS app has
been validated. Required gates remain memory/tasks/exceptions/IPC as needed,
dyld and system libraries, frameworks/display/input, then an actual App journey
on Android. Kernel initialization is not a prerequisite when Linux-backed
services supply the required application behavior. Record the highest gate
actually observed; a green probe never advances a later gate.

## Working loop

1. Read this handoff and the exact failing run before changing code. State one
   hypothesis and the observable result that would confirm or reject it.
2. Commit the source, dependency pins and workflow changes together. Dispatch
   only the relevant mode through `build.sh`; `both` explicitly runs all stages.
3. Track the exact run ID and attempt. Inspect its status after interruptions;
   a local wait timeout is not grounds to dispatch another build.
4. Use `build.sh report --run-id RUN_ID` for the existing run's failure,
   exception, original fault and receipt/executable hashes; inspect raw logs
   for causal details. See [diagnostic tool selection](DIAGNOSTICS.md).
   Download a specific artifact only when its contents are needed for
   diagnosis or durable preservation, not as a routine workstation build step.
   Preserve failures; never relax checks to make an unfinished stage green.
5. Replace the current handoff when the blocker changes. Update the table only
   when an acceptance gate changes. Keep trial history in Actions and Git,
   rather than appending run diaries to README or this document.

Avoid changing unrelated layers in one trial. Re-run the affected upstream and
downstream stages when interfaces change. An unchanged bare-metal baseline need
not run for every Linux host edit. Source pins fix source identity, but hosted
runner images/package repositories still change: retain environment versions,
and do not claim bit-for-bit reproducibility from pins alone.

## Evidence lifecycle

Each executed job writes `receipt.json` alongside its artifacts, including the
Git revision, run/attempt/job identity, dependency lock, host identity, execution
outcome before artifact upload and SHA-256 of retained files. The Actions job
conclusion remains authoritative for upload failures/cancellation; a receipt
cannot certify events after it was written. Checkout/runner failures can prevent
receipt creation, so missing evidence is never a passing result.

Artifacts are retained for 30 days. Download useful failures and milestone runs
through `build.sh download --run-id RUN_ID` before expiry; local downloads are
not a durable shared archive. Before relying on a milestone long-term, preserve
its exact artifact ZIPs, receipts and workflow logs in a versioned external
archive or immutable release. Until that archive exists, old Actions links may
lose their binaries. Do not silently replace expired evidence with a new build.

Git stores source, pins and this short handoff. Actions stores run history and
logs. Artifacts store bytes and provenance. These roles should stay separate.
