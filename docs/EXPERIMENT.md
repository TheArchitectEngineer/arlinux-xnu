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
using the real pinned UIKit/frameworks. Latest executed source:
`d9bf13a9f62e50b4e6835215a2ff1cc40843e386`,
[run 39406168887](https://github.com/taowen/arlinux-xnu/actions/runs/39406168887).
Use the sole build.sh entry, `build --mode app --ipc darling`.

Two genuine dyld/libSystem console journeys still pass: malloc, pthread join
(value 37/shared memory 42), real input, normal return and independent relaunch.
The independent session owns bootstrap, process-info and FrontBoard listeners;
Darling/XNU owns actual cross-process Mach rights and messages. Kernel audit
identities and Linux pidfds bind and retire connections. Submitted task rights
are capabilities, not independently verified task-to-PID mappings. No PID 1/root
identity is fabricated. System-app facilities and app-requested helper spawning
remain incomplete.

Actual launch and create-scene messages reach UIKit. Both ordinary apps register
separate real Wayland backends at 1024x640/60 Hz; session records are keyed by
kernel audit PID/incarnation and pinned by app/backend pidfds. Backend parentage
is validated; either process exit retires its display record. No fallback
geometry is invented. Scene identifiers are unique to the app incarnation.

The initial scene payload now supplies class setting 1 and display configuration
setting 2. Real BSSettings decoding writes a genuine FBSDisplayConfiguration;
its identity and mode constructors run. Missing pixelSize/bounds are derived by
the actual configuration decoder from currentMode. UIKit reaches
workspace:didCreateScene:withTransitionContext:completion:; the ordinary app
4218 and traced app 4330 send actual FBSWorkspaceCreateSceneResponseEvent replies.
This passes the previous nil display identity exception in the traced journey.
The other ordinary app 4219 sends a scene event but no reply is observed; do not
claim all concurrent scene operations completed.

Current blocker: the ordinary attempt times out (124), while the traced process
returns its create-scene response and waits in the main run loop. No exception
is observed in that trace. It does not enter _runWithMainScene or ARHELLO_READY;
no UIKit frame or real input passes. Scene creation is not app initialization or
rendering acceptance. Next inspect canvas creation and lifecycle/activation
before changing more scene fields. Do not invoke the app delegate directly,
remove assertions or substitute a host-drawn app.

Canvas diagnosis: [run 39406917636](https://github.com/taowen/arlinux-xnu/actions/runs/39406917636),
source `369fdf6dc5dcb59a27d4f27efd58db33a9b4e687`, reaches genuine
_UICanvas class factories, _UIApplicationCanvas initWithScene:,
scene:didUpdateWithDiff:transitionContext:completion: and
_transitionLifecycleStateWithTransitionContext:completion:. Thus canvas creation
is established. The current sender omits transitionContext; the actual
lifecycle block reads _lifecycleActionType from it before choosing activation
or deactivation handlers. No ARHELLO_READY or window journey passes.
LLDB log SHA-256 `c204a5037e9d1bf05a9abdbd1f93a1f4462c8944b0661a8bb95be6ac38854899`
was verified against its receipt.

[Run 39407602453](https://github.com/taowen/arlinux-xnu/actions/runs/39407602453),
source `fb3f1e7b5cf4ac814e8de4fb15375d82c38b8ceb`, resolves the missing
transition protocol. UIApplicationSceneTransitionContext._lifecycleActionType
reads otherSettings object key 7 as unsignedIntegerValue. The actual canvas
branch table maps 1 to activateEventsOnly:NO, 2 to events-only activation, and
3/4 to deactivation. Nil context selects action 0/non-launch-specific actions.
LLDB log SHA-256 `3dba51aa1a104356c73af7034439529e41e8d926ff33f4ac0726763ee2147ada`
was verified against its receipt.

[Run 39408194802](https://github.com/taowen/arlinux-xnu/actions/runs/39408194802),
source `62b41c913ca690dbf70e033ce417b082453be336`, delivers and decodes
UIApplicationSceneTransitionContext in the genuine create-scene path. UIKit calls
_lifecycleActionType and selects compatabilityLifecycleMonitorForCanvas:.
It still waits without _runWithMainScene or ARHELLO_READY. Base-monitor
activation breakpoints do not hit; this does not prove the subclass received no
activation. The decoded action's actual return value is not yet observed.
LLDB log SHA-256 `a7d6df168c54df764a94505c63f9dd39e65cf5cf24228b5734f296ee5393fa4a`
was verified against its receipt.

[Run 39408950013](https://github.com/taowen/arlinux-xnu/actions/runs/39408950013),
source `ac9c62eb4c1d2fce898bd285085b645507a1d165`, observes the actual
_lifecycleActionType returning 0 at both its diff-action caller and the canvas
activation dispatcher. This is not successful action-1 decoding. The real
factory class globals identify __UICanvasLifecycleMonitor_Compatability and
__UICanvasLifecycleMonitor_I; concrete activation methods are traced. No
activation handler or ARHELLO_READY is observed in this journey.
LLDB log SHA-256 `bcf30e05dae70a3757f0bac5668bc7213a2f5c838dbb9819d3a49cf03cfb6dc8` was receipt verified.

Exact pending run: [39409669685](https://github.com/taowen/arlinux-xnu/actions/runs/39409669685),
source `57c4705f38e9a83889f6419eeaaa1acc79eddad8`, changes the NSNumber
payload from XPC uint64 to int64 (the BSSettings key remains uint64), traces
setting 7 writes and reads the actual Foundation XPC conversion code.
Hypothesis: the selected Foundation bridge does not convert the unsigned value.
Verify the actual decoded object and _lifecycleActionType return value 1, then
normal activation and _runWithMainScene. If it remains 0, inspect decoded
otherSettings before altering UIKit control flow. Continue this exact run;
local observation timeouts do not justify restarting it. No GUI acceptance passes.

Header-only MACH_NOTIFY_SEND_ONCE cancellation is accepted only on outstanding
reply ports with the exact kernel notification and zero audit identity; it is
never a successful scene response. Client retirement is observed. Complete
crash isolation, helpers, normal GUI exit/relaunch, concurrently interactive
apps and the ARLinux/Android journey remain acceptance requirements.

Useful research: [ios-rendering-docs](https://github.com/EthanArbuckle/ios-rendering-docs)
explains scene/settings/context/render-server boundaries. The generated
[iOS runtime interfaces](https://github.com/nst/iOS-Runtime-Headers/tree/master/PrivateFrameworks)
help identify display and canvas methods; they are not codec implementations.

## Exact evidence and inputs

Downloaded app/peer/LLDB/session textual logs match the latest app receipt.
App evidence receipt SHA-256:
`7fa101fe63bd8f9f3b534eb67e90aab715983984b34020311a24b81bddf90190`.

| Artifact | SHA-256 |
| --- | --- |
| app.runtime.log | `25f34528919eb7e501bd8d619bd3d80f900f24f1a00014b4a3b246dce8b3553e` |
| lldb.log | `01c72745c66d538a16ba08d46fef85ff095f05ccfe4598c095513ccd0f3e8f38` |
| peer.runtime.log | `41e451af2d3d7018cc991b3f9461b99d07870f5c8b599a634a45e2414af155df` |
| session.log | `c3fd42ea55856a2515e31b75dfd1453fbd990e56b4c6dbf2c6622f82f8825869` |
| session.stdout | `9c88772b679928be5698ee0578f4fa12b264ce1923d294350adef8621c85fa84` |

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
