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

[Run 39411849622](https://github.com/taowen/arlinux-xnu/actions/runs/39411849622),
source `9d7a01962dbc7c3db3856dae227677a70abd8759`, passes the F_CHECK_LV
composition policy and reaches the next explicit dyld error:
`mmap() errno=45 validating first page of /usr/lib/libobjc-trampolines.dylib`.
This confirms the file-mapping gap after Library Validation, not successful
loading. Runtime log SHA-256 `643737f7621cff45b77b91340b5c7ad859f5e02dccf34db85759f86607e66c65`
was receipt verified. The trusted-input runtime does not enforce Apple Library
Validation or claim to register/authenticate signatures; descriptor/argument and
actual file/mapping errors remain real.

Exact pending run: [39412764242](https://github.com/taowen/arlinux-xnu/actions/runs/39412764242),
source `52ef647fe61d838b00165c52f2b0328665babb93`, implements real low-address
16 KiB file mappings, shared/private data and fixed placement. Private executable
mappings are parsed as bounded non-arm64e ARM64 Mach-O (thin or FAT32); only
explicit instruction-section intersections are adapted before cache flush and
requested protection. The same instruction adapter is shared with the initial
loader. Source files and data sections are not rewritten. Shared executable
maps, anonymous executable/JIT allocation and general executable mprotect remain
unsupported. Check actual DARWIN_FILE_MMAP arguments/results and
NATIVE_DYNAMIC_TLS_REWRITE, dyld's next error and real app progress. Bounds or
mapping failures stay visible; no address is returned without actual storage.
Continue this exact run; observation timeouts do not justify restarting it.
No ARHELLO_READY, UIKit frame, real input or complete GUI lifecycle passes yet.

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

Latest failure evidence is run 39411131655; downloaded LLDB/runtime/session
logs match its receipt. Receipt SHA-256:
`c7a904a0113438a28c2713814244c837f86cedbc735386653777627d19a5cc09`.

| Artifact | SHA-256 |
| --- | --- |
| debugger.runtime.log | `7263964427d6da2fd4eb555d2ad4749fac64ba4f0bfaeaf1206bf66d7880891b` |
| lldb.log | `20c041305c54b28fca3dd9d5e45badcde0332f22c8ae0502e40b439d525e856c` |
| session.log | `693d5bc20971762199264032acc9eb10edb26d6b022e593b6fdcf68895509e52` |

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
