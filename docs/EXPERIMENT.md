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
`baa76908d9ba96a5a849bb9ed0b56239e6815d9e`,
[run 39394942049](https://github.com/taowen/arlinux-xnu/actions/runs/39394942049).
Use the sole build.sh entry, `build --mode app --ipc darling`.

Two genuine dyld/libSystem console journeys still pass: malloc, pthread join
(value 37/shared memory 42), real input, normal return and independent relaunch.
Darling/XNU owns real Mach rights/messages; current-task VM/time/BSD remain
Linux-backed. Synchronous anonymous S2C mapping works for observed OOL flows;
file/fixed/executable mappings and asynchronous callbacks remain incomplete.

The independent session (Linux PID 4290) owns bootstrap, process-info and two
FrontBoard listeners. Concurrent ARHello PIDs 4298 and 4299 both check in,
register actual task/listener rights, and associate their real workspace
process metadata. Kernel audit PID/incarnation binds each connection; Linux
pidfds drive release of retained rights/records on exit. Submitted task rights
remain capabilities, not independently verified task-to-PID mappings.
No virtual PID 1/root audit identity is fabricated. Early exited/recycled
check-ins now release their rights instead of aborting the session; this race
branch has not yet been forced in the complete app journey.

BKSWorkspace.init's signed message 11 registration is identified from actual
constructor code. FrontBoard workspace message 0 carries
FBSWorkspaceConnectEvent/FBSProcessHandle; association requires its PID to match
the audited sender. The session retains this connection's application metadata.
System-app facility registration/operations remain explicitly unsupported.

Run 39393971710 establishes the real UIApplication workspace dispatcher:
message 1 routes to _queue_handleLaunch:, with an FBSUIApplicationLaunchEvent
whose base event encoding has no additional fields. The session now delivers
that event over each actual client connection and provides a genuine send-once
reply path. Both ordinary app PIDs receive it and reply; debugger PID 4405 also
replies. Its actual callbacks include _queue_handleLaunch:,
workspace:didLaunchWithCompletion: and
_handleApplicationLaunchEventWithCompletion:. Replies contain
FBSUIApplicationLaunchResponseEvent/FBSUIApplicationLaunchResponse with
taskSuspend=true and taskSuspendOnLock=true. No process-suspension behavior is
claimed from decoding those flags.

**Startup still fails.** Both ordinary apps time out at 30 seconds (124),
without ARHELLO_READY. No main-scene creation/_runWithMainScene: or application
initialization delegate has been observed. The launch handshake alone updates
UIKit launch state and returns policy; it does not create the main scene.
Wayland still submits a blank host buffer, not an application frame. Helpers,
full state/crash isolation, interactive lifecycle and Android remain unproven.

Next: implement real main-scene creation/activation through the workspace,
using the actual create-scene dispatcher and event/settings codecs. Preserve
the separate launch-reply evidence; do not manufacture scene state or invoke
the app delegate directly. Diagnose actual UIKit/QuartzCore requirements as
its genuine scene path advances, then verify display/input/exit/relaunch,
concurrent apps, requested helpers and ARLinux integration.

## Exact evidence and inputs

Latest run jobs: server `118535237409`, native `118535673592`, app
`118535958267`. Downloaded session/app/peer/LLDB textual logs match the receipt.

| Artifact | SHA-256 |
| --- | --- |
| Native ELF | `3c99de98e7481abf67f8a876d7de360307b7b8fcb40a0dde66efa6bbbccce53c` |
| Darling server ELF | `cb0ef60765aba1e190f2a355a3f64028ebe8285493e43293d700e005abf0852c` |
| App evidence receipt | `e4ca94e92b1d1ec926232205db7f975c405790cb5f9ac1a41bf46520ca6fbda2` |
| Native receipt | `71fb387c8a18939525643d68e7de9e3e2b5c72162601fbb067a9539e1ffc60bb` |
| Server receipt | `f5bcefa8b5319d45a399cbc1a92a363e3168b447d4c2638f153193db0e21ec01` |
| Session log | `1c431f76acebce964be59c671db5865300d162d8b69b2f819d32f9b756155e83` |
| Session decoded dictionaries | `ffa0de930c7a20165c69661e37beebc5fd36ce7007641db012392ae10c00fb62` |
| LLDB log | `e46db857533ecec8b81e0db583ba930bd9411ea2e91e341ae530eda8ccf67ac6` |

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
