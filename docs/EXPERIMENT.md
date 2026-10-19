# Experiment contract

## Current handoff

The active target is a modifiable Darwin/iOS compatibility environment on
Linux/Android. Native ARM64, 16 KiB guest pages and one advertised CPU remain
fixed. Original XNU boot is a behavioral reference, not an application gate.
No third-party iOS App, UI or Android integration has passed.

Current application: **ARHello 1.0** (`io.arlinux.ARHello`), a programmatic
UIKit window, label and increment button. Its interfaces/TBD files contain only
ABI declarations; real implementations come from the selected iOS firmware.
The app's code and bundle metadata are committed under guests/ARHello.

Latest application attempt:
[39336911768](https://github.com/taowen/arlinux-xnu/actions/runs/39336911768),
source `4e4c98aa071f1c4e12fbb8ba4e0978ba3414b390`, dyld job `118348800479`.
It enters real UIKitCore UIApplicationMain/UIApplicationMainPreparations, then
libdispatch's root-queue initialization calls _pthread_workqueue_supported.
The real libpthread deliberately traps because basic registration returned 0
and its supported-features state remains unset. The process exits 133 (SIGTRAP).
No window/display/input callback has passed.

The same run first completes both independent real-libSystem console launches:
worker return 37, shared memory 42, input counter 1 and normal exit. Previous
standalone console regression is
[39336200293](https://github.com/taowen/arlinux-xnu/actions/runs/39336200293).

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

Implement actual dispatch/workqueue support or adapt genuine userland to use
real pthread-backed scheduling. Do not return an invented feature bitmap just
to get past _pthread_workqueue_supported: registration currently implements only
basic pthread creation. The actual app already loads real UIKit dependencies;
this concrete scheduler failure comes before a window or input can be tested.
General Mach descriptors, port sets, notifications and cross-process namespaces
are incomplete. No actual iOS UI journey or ARLinux integration has passed.

The macOS reference uses the identical iOS app binary as input, PlayCover's
unmodified Macho.swift conversion at the source-lock commit, ad-hoc debug
signing and LLDB on the real macOS host. It does not yet inject PlayTools or
claim PlayCover's complete installation/GUI workflow. First reference run:
[39337252053](https://github.com/taowen/arlinux-xnu/actions/runs/39337252053),
source `35fd78da6238b6e01a3fc931aaffb9ae1256dbe8`, job `118349837480`.
The actual macOS 14.8.9 ARM64 runner hits UIKitCore UIApplicationMain in LLDB,
then logs ARHELLO_READY. Its screenshot shows the real white app window,
ARHello 1.0 — Count: 0 and Increment button. The observation ends at timeout
124; button interaction, normal close and relaunch remain unverified. This
confirms a usable native reference/debugger/GUI path, not a completed app journey.
Original input hash matches the Linux app hash below; converted/signed hash is
`5b08e8cf4c36e5894825b254bc3de4e43058da9fc77c407fff948e6b196e340f`.
Reference receipt: `b4675a7aa0aa55cdd9d42a4bb8123eeff5ae41ce5de1c827bfee2a30d0c44ada`.
Text and inspected screenshot: build/runs/39337252053/{playcover-job.log,screen.png}.

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
4. Inspect that run's Actions logs, exit/result and printed receipt/executable
   hashes. Download a specific artifact only when its contents are needed for
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

The default `build.sh build` now selects the real kernel path. Standalone early
diagnostics require `--diagnostics`; host/shim and bare-metal runs require their
explicit modes. The VM startup task links the complete declared service graph and attempts
execution. The former full-link job duplicated the exact same link and exited;
it has been removed. Optional diagnostics do not turn failed kernel execution
into a passing result.
