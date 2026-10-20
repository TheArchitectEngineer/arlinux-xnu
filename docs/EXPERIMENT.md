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
using real pinned UIKit/frameworks. Activation milestone source:
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

The pthread read/write-lock gap is now exercised by real UIKit startup:
[run 39411131655](https://github.com/taowen/arlinux-xnu/actions/runs/39411131655),
source `1c90164f63f6a2807cd10f1d56464586f5c1d1e1`, sends actual read-lock
and unlock operations to the pinned Darling/XNU queues. Both return status 0
and update 0x100, accepted by real libpthread. This is actual queue behavior,
not a zero-result stub. Write-lock routing is implemented but not independently
observed in that trace. Server Darwin errors and Linux transport errors retain
separate ABI handling. The local backend remains unsupported for these calls.

[Run 39418764026](https://github.com/taowen/arlinux-xnu/actions/runs/39418764026),
source `1e0fc46e8deedc5af8d821a3007f9fc3f628735c`, builds and starts the unified
session. SESSION_MACH_CORE_STARTED and UNIFIED_SESSION_PID both identify PID
3864; real Mach check-in and port setup precede SESSION_READY. Apps 4277 and
4278 independently register real Wayland backends and audited process-info
connections. Both genuine dyld/libSystem console journeys complete: pthread
join value 37, shared memory 42, input and normal return. This establishes
shared session startup and separate app processes, not interactive apps.

Real ARHello passes trampoline remap and instruction-cache maintenance, then
exits 125 at `unsupported display request rights/body`. The observed request
on render port 0x503 has ID 0x9d0b (40203), 108 bytes, bits 0x80001112, voucher
0 and five Mach descriptors. The simple display service rejects the complex
body before decoding it. Preserve real rights, shared memory and rendering
semantics when adding support; a successful registration reply alone is not
rendering. No ARHELLO_READY, UIKit frame, real input or complete GUI lifecycle
passes yet. Keep ordinary and debugger attempts separate.

[Run 39420355213](https://github.com/taowen/arlinux-xnu/actions/runs/39420355213),
source 4ea2cef5343fdfb33ef786694c6825e1233e2c55, decodes the actual failure:
four port descriptors (first two NULL, two real send rights), then one 149-byte
OOL mapping. The trailing NDR/scalars are at offsets 92, 96, 100 and 104 with
values 0, 1, 0 and 149. The blocked guest stack is in QuartzCore's
Context::connect_remote overloads. The OOL payload content is not decoded;
do not guess that all five resources are shared memory or assign port roles.

Exact candidate run: [39421509125](https://github.com/taowen/arlinux-xnu/actions/runs/39421509125),
source 97b00aeae004cfabb875a2ccdc22ef8255b2c14f. It resolves genuine
CARenderServerStart/GetPort from the current cache and calls them on the
original guest-TSD lookup thread. Measured Wayland display metadata stays in
the adapter; other actual render requests are forwarded to the real framework
port with the original reply right and transferred resources. No registration
reply, context or rendered frame is fabricated. Observe NATIVE_RENDER_FRAMEWORK_
and NATIVE_RENDER_FORWARD, real startup errors and app progress before claiming
this candidate works. Continue this exact run while it remains live.

Primary ABI reference: WebKit's
[QuartzCoreSPI.h](https://github.com/WebKit/WebKit/blob/ebaa3acb4ca036ced8011e8bfb2e951dd2199696/Source/WebCore/PAL/pal/spi/cocoa/QuartzCoreSPI.h#L287-L299)
declares these entry points and a CoreGraphics rendering path. This is an
implementation candidate, not evidence that the selected iOS runtime can render
on Linux. Older community 40202/four-port examples do not define the observed
40203/four-port-plus-OOL protocol.

The runtime source now lives in components/darling, with native client and
server-side support identified by the same enclosing Git commit. ORIGINS.json
retains import history/licenses; no Darling submodule is fetched. The first
import run 39415413747 failed building MIG due to missing original header
forwarding paths. Those source symlinks are included at 4582c23; that run builds
both server and client and reaches the real app failure above.

The standalone Darling desktop main and executable are removed. A linked
Mach-core library runs its event loop in the persistent native session process,
which also owns bootstrap/FrontBoard. App address spaces stay independent.
Internal host socket RPC and same-task guest VM routing are not simplified by
this consolidation. Complete crash isolation, helpers and GUI remain unpassed.
The first shared-library link retained unused exported XNU functions; limiting
exports to the host entry point restores section GC without fake kernel stubs.
Run 39418258026 compiled both components but did not start them due to an omitted
native artifact download. Its dependency and pre-execution hash check are fixed
in 39418764026; continue from the actual render request above.

The S2C memory implementation maps real low-address, 16 KiB anonymous storage
with actual fixed overwrite/non-overwrite and requested R/W/X permissions.
Darling owns source reads and copying; after its final RPC reply the client
flushes copied executable ranges. Already-adapted process code is not rewritten
twice. General guest JIT remains unsupported. The core's upstream RWX/current
protection limitations remain. The dynamically loaded trampoline library's one
instruction section required zero TLS instruction changes, so its loading does
not establish dynamic-library TLS rewriting on a library that needs it.

References: Apple's [objc4 trampoline allocation](https://github.com/apple-oss-distributions/objc4/blob/objc4-750/runtime/objc-block-trampolines.mm#L287-L334)
uses fixed overwrite with copy=TRUE. The imported memory.c implementation
already performs a real copy. libffi's copy=FALSE sharing variant cannot replace
that private-copy operation.

The prior diagnostic run 39410444685 stopped earlier in libdispatch at
0x1808c4878 and did not reproduce the pthread abort; preserve that distinct
interleaving. Actual app progress, not one selected successful trace, remains
the acceptance criterion.

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

Latest failure evidence is run 39420355213; downloaded ordinary/debugger runtime
and session logs match its receipt. Receipt SHA-256:
`b4503275d2d6e688ab8086b26f14a493829fbb7746ee183c25046d2f22b700d8`.

| Artifact | SHA-256 |
| --- | --- |
| app.runtime.log | `77c4e2a1c45da967be903ba36b7fc908d734b9b086094d2cdc9558968b130360` |
| debugger.runtime.log | `755eb8ae3afd503b004888162e1b80c1532f3745b7274f8a9bb74bf2d6c36ce1` |
| session.log | `763d3d5fb097ef68e8b80320efb914a338566266503363dadc8f9984168fe5b0` |

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
