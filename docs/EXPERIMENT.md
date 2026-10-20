# Experiment contract

## Current handoff

Target: a modifiable Darwin/iOS compatibility environment on Linux/Android,
native ARM64, 16 KiB guest pages, one advertised/scheduled CPU, non-arm64e.
Current app: **ARHello 1.0** (`io.arlinux.ARHello`), source in guests/ARHello,
using real pinned dyld/UIKit/frameworks. The sole build/report entry is build.sh;
see README for operations and docs/DIAGNOSTICS.md before runtime changes.

**No Linux pixel/input/complete GUI lifecycle or Android acceptance has passed.**
Acceptance still includes two concurrently interactive independent applications,
normal close/relaunch without disrupting the other, app-requested helpers/XPC
services, audited identity/resource cleanup after crashes, then ARLinux/Android.

Latest runtime evidence: [39431342283](https://github.com/taowen/arlinux-xnu/actions/runs/39431342283),
source **89b7f4f8010d7a950ccd627f315f843b301006ac**. Both genuine dyld/libSystem
console journeys pass (malloc, pthread join 37/shared memory 42, input, normal
return/relaunch). Both original UIKit app processes now exercise real workloop
synchronous wait/wake/return, with no original-process BRK. The peer reaches
ARHELLO_READY; the original does not. Both later exit 0 through a framework
invalidation path, so the app journey correctly remains failed.

In the original process, waiter TID 5516/ident 0x703 sleeps on workloop
0x30022aae0; TID 5595 wakes one futex waiter, then TID 5516 returns state 1/status
0. The peer independently completes three matching wait/wake/return chains,
including reciprocal waits between its main and worker threads. These are real
process logs, not a diagnostic rerun or a source assertion. Dyld receipt SHA-256
81943741391b7482ae36f522a8ca3a6935e567858a737c1d13f7bbfbe21c3679
matches downloaded app outcomes, both runtime logs and peer stdout.

Current functional blockers:

- BackBoard HID focus: the actual NSXPC client requests
  `com.apple.backboard.hid.focus`; rejecting the missing service causes exit.
  Exit caller 0x183822e34 belongs to its invalidation handler (literal:
  "backboarrd must be going down, exiting"). Implement real focus/input behavior
  backed by Wayland; suppressing exit or holding an empty endpoint is not a fix.
- Window readiness is not pixel delivery. Genuine QuartzCore context creation
  and input must reach the real Wayland backend, then support complete lifecycle
  and two concurrent interactive apps. That end-to-end connection is unproven.
- The new sync-wait path does not supply XNU turnstile priority inheritance or
  the workloop control syscall. Continue diagnosing required scheduling behavior
  from actual app contention. A wait registration alone is not a wake proof.

Current diagnostic candidate adds an audited receive endpoint for HID focus,
with a fatal unimplemented-operation path before any semantic reply. In run
[39432695199](https://github.com/taowen/arlinux-xnu/actions/runs/39432695199),
source b6d8038cc098978d8e97cba7bc4aac210bfa4180, lookup succeeds for both apps;
neither opens the lazy XPC connection or submits an invocation. Both reach
ARHELLO_READY and remain alive until timeout 124. This demonstrates service
discovery/liveness only, not a focus implementation. Receipt
f1c4149f228bd50466cb5ad63b90b3ebc812381e6a69fef8d243243fbe848961
matches outcomes, both stdout files and session logs. The public runtime header
[BKSEventFocusIPCInterface](https://github.com/nst/iOS-Runtime-Headers/blob/9b00d90cdccdc95b8029441919bcfb7fb44c8816/protocols/BKSEventFocusIPCInterface.h)
declares flush(NSSet), but this cache's actual invocation is still unobserved.
Next exact run [39433710099](https://github.com/taowen/arlinux-xnu/actions/runs/39433710099),
source 0e810ff4c948bcfdd3a74a5f88f6eafcef12ca28, captures the original live app
after readiness, real Wayland activation changes and actual CARender exports.
Continue that handle while live; no pixel/input/lifecycle pass is claimed.

Verified substrate milestones:

| Evidence | Established behavior | Limit |
| --- | --- | --- |
| [39418764026](https://github.com/taowen/arlinux-xnu/actions/runs/39418764026), source 1e0fc46e8deedc5af8d821a3007f9fc3f628735c | Unified session PID owns the linked Mach core plus bootstrap/FrontBoard; independent app processes exchange genuine Darling/XNU rights/messages | Helpers, task-right-to-PID verification and full crash isolation remain incomplete |
| [39420355213](https://github.com/taowen/arlinux-xnu/actions/runs/39420355213), source 4ea2cef5343fdfb33ef786694c6825e1233e2c55 | CA request 40203 has four port descriptors, one 149-byte OOL mapping, original reply right | Old 40202 community examples do not define this wire protocol |
| [39425899111](https://github.com/taowen/arlinux-xnu/actions/runs/39425899111), source 21d9a8e3b648e4847cbf57c29e0a321fa7e71567 | Real CARenderServerStart/GetPort, port-set move, framework receive and actual replies; peer reaches window callback | No screenshot/pixel/input proof |
| [39431342283](https://github.com/taowen/arlinux-xnu/actions/runs/39431342283), source 89b7f4f8010d7a950ccd627f315f843b301006ac | Disabled per-waiter knotes, real sleep/wake and debounce/owner handling progress past the earlier libdispatch assertion | Framework service invalidation still exits the app |

The independent session authenticates audit PID/incarnation and keeps Linux
pidfds. Submitted task rights are capabilities, not independently verified
PID mappings. No PID 1/root identity is fabricated. Runtime source is a hardfork
under components/darling; ORIGINS.json preserves upstream identities/licenses.
The standalone Darling main/executable is removed; internal host socket RPC and
same-task guest VM routing remain. See docs/ARCHITECTURE.md for boundaries.

S2C maps real low-address, 16 KiB storage with fixed overwrite/non-overwrite and
requested permissions. Darling performs real copying; the client flushes copied
executable ranges after the final RPC reply, without adapting code twice.
General guest JIT remains unsupported; upstream VM protection limitations remain.
The observed objc trampoline section needed no TLS rewrites, so its successful
loading does not verify every dynamically loaded library's TLS adaptation.

Useful source references: WebKit's pinned
[QuartzCoreSPI.h](https://github.com/WebKit/WebKit/blob/ebaa3acb4ca036ced8011e8bfb2e951dd2199696/Source/WebCore/PAL/pal/spi/cocoa/QuartzCoreSPI.h#L287-L299),
Apple's [objc trampoline allocator](https://github.com/apple-oss-distributions/objc4/blob/objc4-750/runtime/objc-block-trampolines.mm#L287-L334),
and the imported XNU bsd/sys/event.h and kern_event.c for synchronous workloops.
Historical investigation details remain in Git and exact run artifacts; keep
this handoff limited to current behavior, decisive evidence and next blockers.

## Exact evidence and inputs

Latest failure evidence is run 39422532342; downloaded ordinary runtime,
original-process attach and session logs match its receipt. Receipt SHA-256:
`1cf16c75faaf3d4f84f704fb1b05a53e954803bc9a20da94bc23761afa150eaf`.

| Artifact | SHA-256 |
| --- | --- |
| app.runtime.log | `611f982fef91da7ea1e88b2e263b406315e65b7bc26cab38321cdc2f8dcadcaf` |
| ordinary-wait.log | `2771d975344a5db4554609cabb85a3b1bdfad64f74ab28fd3250e8ae2b559b69` |
| session.log | `ed2cf1606d6e3dbdb0c18d2376f41cc4183115a1163423c721015d05ae5c4973` |

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
