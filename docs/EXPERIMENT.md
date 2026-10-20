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
`bd26a47d491cadb2f4e2518cbeaaa03fdc90e18c`,
[run 39389693704](https://github.com/taowen/arlinux-xnu/actions/runs/39389693704).
Use the sole build.sh entry, `build --mode app --ipc darling`.

Two genuine dyld/libSystem console journeys still pass: malloc, pthread join
(value 37/shared memory 42), real input, normal return and independent relaunch.
Darling/XNU owns real Mach rights/messages; current-task VM/time/BSD remain
Linux-backed. Synchronous anonymous S2C mapping works for observed OOL flows;
file/fixed/executable mappings and asynchronous callbacks remain incomplete.

The independent session process (Linux PID 4273) owns bootstrap and process-info
receive rights. Two concurrently launched ARHello processes (4281 and 4282)
reach it with kernel-audited requester identities and each completes actual
process-info XPC check-in. A separate debugger attempt (4380) also checks in.
This is cross-process service connectivity, not concurrent working applications
or app-requested helper spawning. No virtual PID 1/root audit identity is used.
The adapted libxpc predicate authenticates the real session provider; original
requester PID/incarnation echo checks remain.

The forked community libspc codec now decodes transferred endpoint rights.
Both clients' first process-info dictionary has `message=11`, a `taskPort`
send right and an `endpoint` send right. The adapter explicitly returns ENOTSUP;
process-state semantics and peer retirement remain unfinished. FrontBoard
workspace/systemappservices are absent and return unknown-service 1102.

**Startup still fails.** Both ordinary app attempts reach the 30-second timeout
without ARHELLO_READY. PEER_PROCESS_STATUS is 124. This revision mistakenly
prints the aggregate result as APP_PROCESS_STATUS=1; it is not the raw primary
exit status. The workflow fix preserves that status separately in the next run.
The debugger is interrupted while the main thread waits for a Mach reply;
its frames alone do not identify which unsupported service caused the wait.
Wayland still submits a blank host buffer, not an application frame.

Next: correlate message 11's real framework caller and expected response with
community code, implement required process-info state/endpoint behavior, and
identify the outstanding Mach request/reply. Do not infer success from a live
connection or suppress framework errors. Helper launch, service ownership,
crash/exit cleanup and identity reuse remain required subsequent work.

## Exact evidence and inputs

Latest run jobs: server `118517946876`, native `118518260536`, app
`118518583988`. Downloaded session/app/peer/LLDB textual logs match the receipt.

| Artifact | SHA-256 |
| --- | --- |
| Native ELF | `0aeca9ed3c261031cea0d4dd36e5148a7214865cd50de2d68dc23d0524c6a347` |
| Darling server ELF | `b821fb0284e01f1ccf1d2df47bbdc0a86d86462ebda7158c871e7481883c848c` |
| App evidence receipt | `6f5c033ca8946840fb37a1f71d607b130ffe1c366b81510fc346078a3ee0b519` |
| Native receipt | `8f89153a0907c45b4eaae1c5a894978a318164e5707536d92d09953184b74960` |
| Server receipt | `344aa075daa5f98d2a59aea3029887d22d52f87a8f62877e619bbe11f75e913d` |
| Session log | `b346e2a74eb18b678d5417daf918e2d2556f18d2729bdcef12c94ea1e04ca53b` |
| Session decoded dictionaries | `169b73a520e974cff19707d5d2402f31554d4e7a790853fa921d4ca472487a81` |
| LLDB log | `2a2e442fd16e87176154fbd7bb1827909d9aa53c0b8bcaf5fbb029ee6593dc51` |

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
