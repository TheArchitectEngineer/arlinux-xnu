# Experiment contract

## Current handoff

Target: a modifiable Darwin/iOS compatibility environment on Linux/Android,
native ARM64, 16 KiB guest pages, one advertised/scheduled CPU, non-arm64e.
Real display, input, normal exit and relaunch have not passed on Linux.
Acceptance includes two concurrently interactive applications in one shared
session, exit/relaunch without disrupting the other, and app-requested helper
or XPC-service launch, communication and cleanup. Repeat through ARLinux;
none of that complete multi-process/Android journey has passed.

Current app: **ARHello 1.0** (`io.arlinux.ARHello`), source in guests/ARHello,
using real pinned UIKit/frameworks. Latest executed source:
`57c4705f38e9a83889f6419eeaaa1acc79eddad8`,
[run 39409669685](https://github.com/taowen/arlinux-xnu/actions/runs/39409669685).
Use the sole build.sh entry, `build --mode app --ipc darling`.

Two genuine dyld/libSystem console journeys still pass: malloc, pthread join
(value 37/shared memory 42), real input, normal return and independent relaunch.
The independent session owns bootstrap, process-info and FrontBoard listeners;
Darling/XNU owns actual cross-process Mach rights and messages. Kernel audit
identities and Linux pidfds bind and retire connections. Submitted task rights
are capabilities, not independently verified task-to-PID mappings. No PID 1/root
identity is fabricated. System-app facilities and requested helpers remain
incomplete.

Actual launch and create-scene messages reach UIKit. Separate Wayland backends
register real geometry/refresh. The service validates sender incarnation and
backend parentage, retains both pidfds and retires display records on exit.
Initial scene changes provide class setting 1 and display configuration 2.
Genuine FBSDisplayIdentity/Mode/Configuration decode and UIKit scene creation
have been observed; no fallback geometry or display identity is fabricated.

The activation context now decodes correctly. The observed bug was an XPC
uint64 NSNumber payload yielding lifecycle action 0. Using int64 causes a real
setting-7 write, then _lifecycleActionType returns 1 at both the diff-action
caller and canvas activation dispatcher. __UICanvasLifecycleMonitor_Compatability
receives activateEventsOnly:NO, executes launch-option/action preparation and
calls actual UIApplication._runWithMainScene:transitionContext:completion:.
This is framework-driven activation, not an injected app delegate call.

Current failure: the traced app reaches that main-scene startup and then traps
at 0x1809bbf6c in libc abort. The actual chain includes libpthread abort,
libpthread locking code, localtime_r, CF logging/NSLog, and UIKit's
SetAppPriorityForcingFixedForcingThread. Nearest-symbol labels alone do not
identify the failed lock predicate. The ordinary attempt times out (124), a
separate process from this trace. No ARHELLO_READY, UIKit frame, real input,
normal GUI exit/relaunch or concurrent interactive journey passes.

[Run 39410444685](https://github.com/taowen/arlinux-xnu/actions/runs/39410444685),
source `304e804614cc2b2bf22323189e4b74dbf780067a`, installs the real
pthread abort-reason breakpoint but stops earlier in libdispatch at 0x1808c4878.
The targeted abort is not reproduced; no pthread reason is captured. Preserve
this distinct interleaving rather than interpreting the missing breakpoint as
proof that locking works. LLDB log SHA-256 `529f244096a21f2fe2243c87afb232a7fa8ade78d6614af5d26650986311be57`
was receipt verified.

A concrete missing operation is independently established in run 39409669685:
actual syscall 306 returns ENOSYS during the UIKit/localtime locking path.
XNU syscalls.master identifies 306/307/308 as psynch_rw_rdlock/wrlock/unlock.
The pinned Darling server exposes all three through its existing XNU queues.

Exact pending run: [39411131655](https://github.com/taowen/arlinux-xnu/actions/runs/39411131655),
source `1c90164f63f6a2807cd10f1d56464586f5c1d1e1`, routes those operations
to generated community RPCs. Actual returned generation/update bits are
preserved; positive Darwin BSD errors use carry without Linux errno remapping,
while negative Linux transport errors retain the existing conversion. Check
DARLING_PSYNCH_RW, actual libpthread reasons, activation and subsequent app
progress. No lock success is fabricated. The local backend remains unsupported
for these operations. Continue this exact run; observation timeouts do not
justify restarting it. No GUI acceptance passes.

Header-only MACH_NOTIFY_SEND_ONCE cancellation is accepted only on outstanding
reply ports with the exact kernel notification and zero audit identity; it is
never a successful scene reply. Client retirement is observed. Complete crash
isolation, helpers, normal GUI exit/relaunch, concurrently interactive apps and
the ARLinux/Android journey remain acceptance requirements.

Useful research: [ios-rendering-docs](https://github.com/EthanArbuckle/ios-rendering-docs)
explains scene/settings/context/render-server boundaries. The generated
[iOS runtime interfaces](https://github.com/nst/iOS-Runtime-Headers/tree/master/PrivateFrameworks)
help identify display and canvas methods; they are not codec implementations.
Apple's [libpthread support source](https://github.com/apple-oss-distributions/libpthread/blob/libpthread-330.230.1/src/pthread_support.c)
explains abort-reason/abort routing; compare it with the actual cache code.

## Exact evidence and inputs

Downloaded app/peer/LLDB/session textual logs match the latest app receipt.
App evidence receipt SHA-256:
`316df971de499e10546838eef0226b4d2c87f4e1cc6ec0ab46fb71252303e0c9`.

| Artifact | SHA-256 |
| --- | --- |
| app.runtime.log | `3c2193406cef498b305567f9b8a8cb3f5d63289e8543e4705f7a803f704a32c2` |
| lldb.log | `9ccffe8df5cc5b7b9d12691db4d7605f14517bd577ae950cbc25b3fe38df6078` |
| peer.runtime.log | `a5c4154b8a6ee708cece829b8e3dcabaf99af203500d8c3dc01ba446d38f53b7` |
| session.log | `2e93ef7cade235892208971d2d34b4ed9ca251adb22b061bcc9e3d83abd3e003` |
| session.stdout | `3056dda8b3ca9c32160e78ed276bbb50dffd887b5395ca8daa8deedebbb24d8d` |

Firmware identity/URL/IPSW SHA-256 live in sources.lock.json: iPhone7,2 /
iOS 12.5.7 / 16H81. Apple executables, firmware, cache and cores remain
transient on runners, never workstation artifacts. ARHello Mach-O SHA-256:
`4b346aa7dd998425990cb9ef282ddb7e5252b4d9d069b12c369d32d407f29b77`.
Dependency commits and other input/output hashes belong to the exact run's
receipts. Keep trial history in Actions/Git rather than a growing diary.

## Evidence boundaries

| Experiment | Established at the linked source/run | Does not establish |
| --- | --- | --- |
| Darling/native dyld | [Two real libSystem console journeys](https://github.com/taowen/arlinux-xnu/actions/runs/39370264712) | UIKit GUI, general IPC/S2C/OOL, Android or all 16 KiB VM behavior |
| Local native runtime | [VM/COW/TLS/input/normal exit/restart](https://github.com/taowen/arlinux-xnu/actions/runs/39320163457) | The same lifecycle on the Darling backend or Android |
| PlayCover macOS reference | [Identical iOS input, real HID click, Count: 1 screenshot, normal exit and relaunch](https://github.com/taowen/arlinux-xnu/actions/runs/39338697656) | Linux/Android compatibility; macOS supplies its own runtime |
| Full-system XNU reference | [Exec, VM/COW, child exit and parent wait](https://github.com/taowen/arlinux-xnu/actions/runs/39311945725) | Userspace XNU, dyld, iOS frameworks or app compatibility |

These observations belong to their exact source revisions. No Android
app-sandbox journey or third-party iOS app has been validated. A green probe
never advances a later acceptance gate. Architecture/ABI reasoning belongs in
[ARCHITECTURE.md](ARCHITECTURE.md); diagnostic evidence selection belongs in
[DIAGNOSTICS.md](DIAGNOSTICS.md), not a duplicated run diary here.

## Working loop

1. Read this handoff and the exact failing run. State one hypothesis and the
   observable result that would confirm or reject it.
2. Commit source, dependency pins and workflow changes together; dispatch only
   relevant work through build.sh. Compilation/execution remain runner-only.
3. Track exact run ID and attempt. A local observation timeout is not grounds
   to restart a job; inspect its authoritative status first.
4. Use `build.sh report --run-id RUN_ID` for status, exceptions, original fault,
   receipts and executable hashes, with raw logs for causal details. LLDB status
   0 describes evidence collection, never an app pass. Keep ordinary and traced
   attempts distinct. Preserve failures and do not relax checks to turn green.
5. Replace this current handoff when the blocker changes; retain trial history
   in Actions/Git. Re-run affected upstream/downstream stages when interfaces
   change. Source pins do not freeze hosted images/packages or guarantee
   bit-for-bit binary reproducibility; retain actual environment versions.

## Evidence lifecycle

Each executed job records source/run/attempt/job identity, source lock, host
versions, outcome before upload and retained-file SHA-256 in receipt.json.
Actions conclusion remains authoritative for later upload/cancellation failures;
missing evidence never passes a gate. Download only needed text/screenshots or
open-source artifacts, never transient Apple executables/cores.

Artifacts expire after 30 days. Before relying on a milestone long-term,
preserve its exact artifact ZIPs, receipts and workflow logs in a versioned
external archive or immutable release. Local downloads are not a shared durable
archive. Until preservation exists, links may lose binaries; never silently
substitute a new build for expired evidence.
