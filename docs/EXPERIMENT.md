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
`5c8d22d5a66af81458f949d4521e243569438b9b`,
[run 39399351865](https://github.com/taowen/arlinux-xnu/actions/runs/39399351865).
Use the sole build.sh entry, `build --mode app --ipc darling`.

Two genuine dyld/libSystem console journeys still pass: malloc, pthread join
(value 37/shared memory 42), real input, normal return and independent relaunch.
Darling/XNU owns real Mach rights/messages; current-task VM/time/BSD remain
Linux-backed. Synchronous anonymous S2C mapping works for observed OOL flows;
file/fixed/executable mappings and asynchronous callbacks remain incomplete.

The independent session owns bootstrap, process-info and two FrontBoard
listeners. Actual kernel audit identities and Linux pidfds bind/retire client
connections and their real Mach rights. BKSWorkspace message 11 registration,
workspace connect metadata and genuine UIApplication launch events/replies
remain implemented. Submitted task rights are capabilities, not independently
verified task-to-PID mappings. No PID 1/root identity is fabricated.

Main-scene event routing is now established from actual code: workspace message
5 -> _queue_handleCreateScene:. The event carries a per-process sceneID and
UIApplicationSceneSpecification, then settings/clientSettings diffs. Both
ordinary apps (4290/4289) reach this stage but exit 133 without ARHELLO_READY.
The independent debugger process (4386) confirms actual create-scene delivery,
FBSWorkspace client:handleCreateScene:withCompletion:, then throws
NSInternalInconsistencyException: `Invalid condition not satisfying: settings != nil`.
Main-scene objects, app initialization, geometry/activation, frames and input
are not established.

The live codec reads FBSSettingsDiff's changes/settingsClass. Its concrete
settingsByApplyingToMutableCopyOfSettings: reads class setting 1 from changes
before constructing the first settings object. The current payload supplies
BSSettings.objects["1"] as a raw class-name string, but the actual object-map
callback calls a further typed decoder and still produces the nil-settings
failure. Do not claim the current encoding creates settings. Inspect that
actual decoder rather than adding speculative fields or bypassing assertions.
The latest ordinary fault PC is 0x1809bbf6c; debugger and ordinary attempts stay
separate. Older run 39396795755's fault also killed the session on a notification.

That session defect is fixed and verified: an unused reply send-once right
produces header-only MACH_NOTIFY_SEND_ONCE (71) with kernel audit token zero.
Only the exact notification on an outstanding event reply is accepted as
cancellation; it is never a successful scene reply. Logs show cancellation and
client retirement while the shared service remains alive for later clients.
System-app facility operations, complete crash isolation, helper spawning,
normal GUI exit/relaunch and Android remain incomplete.

Pending exact run: [39400670557](https://github.com/taowen/arlinux-xnu/actions/runs/39400670557),
source `d201bac04eb1f09ed42665def485818823c845e6`, narrows actual codec block
identification and captures nearby direct decoder callees. Continue observing
this same run; do not restart on a local timeout. The previous block reader
incorrectly included string literals because executable cache mappings also
contain strings. Use the actual stack-block storage/prologue and resulting
code; old APP_SETTINGS_CODEC_BLOCK labels alone do not prove callable blocks.
In the pinned cache the real object callback reaches 0x183696318; this address
is diagnostic context, not a runtime version-specific hook.

Next: read the pending run's actual typed object decoder, correct initial scene
settings encoding, and verify that the genuine scene constructor progresses.
Then derive actual display geometry/activation and rendering/input requirements.
Keep normal exit/relaunch, two concurrent interactive apps, requested helpers
and ARLinux integration as acceptance requirements. Do not call the app delegate
directly, remove settings assertions or substitute a host-drawn app.

## Exact evidence and inputs

Latest run jobs: server `118549883649`, native `118550192389`, app
`118550430829`. Downloaded session/app/peer/LLDB textual logs match the receipt.

| Artifact | SHA-256 |
| --- | --- |
| Native ELF | `dfad36a0820c5e0956da1479f99191956fcefbad641da5eec8322aaf03fc34ac` |
| Darling server ELF | `7864a8ed318cab5ea78cec751b9c4623a07176a2222bc6ac91f2f1db4d2e1c9a` |
| App evidence receipt | `04c98e651a22d602bdfccc6b6cec2e4bda9c3eaeb27dfcb11a8ea7c2afb64e7f` |
| Native receipt | `028ce19bbaab3cb4c94b354f5487b48638cf1cb6edf6f858ac578c4a0e1db568` |
| Server receipt | `f42d3b0c7e80a70c6ff581090bd88c3bd3fa521eac22ab598506112487f36838` |
| Session log | `c784eb4aeda45bdcf4b331a8d5fefd9c4966538f9277a63dea1d9d6a60219e7c` |
| Session decoded dictionaries | `df6852a70ab1e0985d7f0f1a912fd40127b3e1890c8535ec6bc717baf19ed2ee` |
| LLDB log | `9ff1086f3623905d3f9b07431507d8adf997c726f06de1b03b626336ebc8c459` |

Firmware identity/URL/IPSW SHA-256 live in sources.lock.json: iPhone7,2 /
iOS 12.5.7 / 16H81. Apple executables, firmware, cache and cores remain
transient on runners, never workstation artifacts. ARHello Mach-O SHA-256:
`4b346aa7dd998425990cb9ef282ddb7e5252b4d9d069b12c369d32d407f29b77`.
Dependency commits, input/cache and remaining file hashes are in the exact
run's receipts. Keep trial history in Actions/Git rather than a growing diary.

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
