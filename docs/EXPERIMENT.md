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
`8079b473c62d86b112a472bc9851dd1a4e0743fa`,
[run 39392250655](https://github.com/taowen/arlinux-xnu/actions/runs/39392250655).
Use the sole build.sh entry, `build --mode app --ipc darling`.

Two genuine dyld/libSystem console journeys still pass: malloc, pthread join
(value 37/shared memory 42), real input, normal return and independent relaunch.
Darling/XNU owns real Mach rights/messages; current-task VM/time/BSD remain
Linux-backed. Synchronous anonymous S2C mapping works for observed OOL flows;
file/fixed/executable mappings and asynchronous callbacks remain incomplete.

The independent session process (Linux PID 4406) owns bootstrap and process-info
receive rights. Concurrent ARHello processes 4414 and 4415 both complete real
XPC check-in and register distinct submitted task/listener capabilities. Linux
pidfds pin their identities; kernel audit PID/incarnation identifies each
requester. The session retains real Mach send references while the process is
alive and releases them, its connection rights and record on observed exit.
Both clients have PROCESSINFO_WORKSPACE_REGISTERED and PROCESSINFO_RETIRED
records; a subsequent debugger process 4514 also registers and retires.
No virtual PID 1/root audit identity is fabricated.

The first message is now identified from actual code: BKSWorkspace.init packs
signed `message=11`, `taskPort` and `endpoint`, then calls sendToConnection:.
Run 39391469924 captured the constructor, not merely a guessed wire schema.
Registration now performs that retained-state operation; other process-info
messages still return ENOTSUP. Submitted task rights are capabilities, not
independently verified task-to-PID mappings. Exit-before-check-in races still
need graceful retirement rather than the current identity-failure abort.

**Startup still fails.** Both ordinary apps time out at 30 seconds (124).
The debugger observes UIKit `_run`, but no selected scene launch/initialization
callback. Its main thread is in the GraphicsServices/CoreFoundation event loop.
FrontBoard workspace/systemappservices still return unknown-service 1102;
registration alone does not provide scene creation or launch event delivery.
Wayland submits a blank host buffer, not an application frame. Full process
state, helper spawning, crash isolation and interactive lifecycles are unproven.

Next: provide actual session-owned FrontBoard listeners and inspect their real
check-in/messages; implement scene creation/startup event delivery using actual
framework protocol or an explicit framework adaptation. Do not suppress the
initialization delegate or substitute a host-drawn app. Harden early client-exit
races before claiming process isolation. Retain normal exit/relaunch, concurrent
input, app-requested helpers and ARLinux integration as acceptance requirements.

## Exact evidence and inputs

Latest run jobs: server `118526334339`, native `118526744162`, app
`118526942431`. Downloaded session/app/peer/LLDB textual logs match the receipt.

| Artifact | SHA-256 |
| --- | --- |
| Native ELF | `ffade032f503512f4a879df1bd08f4eaa53aa39e535e86fd26ba744c5a08b3aa` |
| Darling server ELF | `a3fa72fdab34edf5824fba5035558c20525b72e4781419d13a3a58dd0a4c4d7e` |
| App evidence receipt | `b1762d0c4d0652c04d1cf6cbd514099e51cbf502fc4eea65fa6e4beef283f6e3` |
| Native receipt | `4aa61aeafd84cf7cb0dff1b9fe4e6009fb442ed121efebebacd08191553973e2` |
| Server receipt | `957854f1f6b63c62ad4b139fe802643530ce883deaf370f1b4c4cc66a845eb06` |
| Session log | `673e61cd235fbdccf2cf8410b594a8dc70df7bdf74f33f228ebd5eb222699642` |
| Session decoded dictionaries | `f866a6696d54403168471a55fac5bfadce9a44f3ddf5ac9ecba9bce6b4e60bcd` |
| LLDB log | `78fce267130fe8ad2aadf974add24d117c2b45f4d79bb4fd43f164e82d76b972` |

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
