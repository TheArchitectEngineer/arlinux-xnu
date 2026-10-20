# Experiment contract

## Current handoff

Target: a modifiable Darwin/iOS compatibility environment on Linux/Android,
native ARM64, 16 KiB guest pages, one advertised/scheduled CPU, non-arm64e.
Current app: **ARHello 1.0** (`io.arlinux.ARHello`), source in guests/ARHello,
using real pinned dyld/UIKit/frameworks. The sole build/report entry is build.sh;
see README for operations and docs/DIAGNOSTICS.md before runtime changes.

**No complete Linux display/input/GUI lifecycle or Android acceptance has passed.**
Acceptance still includes two concurrently interactive independent applications,
normal close/relaunch without disrupting the other, app-requested helpers/XPC
services, audited identity/resource cleanup after crashes, then ARLinux/Android.

Latest runtime evidence: [39439676234](https://github.com/taowen/arlinux-xnu/actions/runs/39439676234),
source **d4eccc4b608976c41994dd63325b6910e19638c0**. Both independent UIKit
apps reach ARHELLO_READY and remain alive until timeout 124. Actual UIWindow
and CALayer bounds are **1024 x 640**; real CALayer rendering into shared
CoreGraphics buffers now produces a white background and readable ARHello 1.0
Count: 0 text in both captured PNGs. **The image is now upright; the Increment button is still absent.** This is real application pixel progress, not a
complete display/input/lifecycle pass or physical-presentation proof.

Dyld receipt SHA-256
705aa4933eed680bbd1caf8aac82fb734437d115e47bed0a8d292ebec100c6a2
matches both runtime logs, app outcomes and both PNGs. PNG SHA-256:
932783331031675bb3dd16a01f0475094a320b4cb18b1c5bbd3df2e728be531b.
No separate diagnostic replay replaces the original processes.

Decisive fixes: missing scene frame (0 x 0 windows) now passes actual registered
Wayland geometry through setting 3 (NSValue -> bs_CGRectValue -> setFrame:),
without forcing an app window. The following libpthread parallelism assertion
was caused by our bsdthread_ctl constant incorrectly naming 364 (lchown).
Correct syscall 478 reaches the existing real single-CPU-affinity implementation
and the assertion no longer occurs in either original process.

Current functional blockers:

- Diagnose the absent Increment button from genuine UIKit view/layout
  evidence. Upright label pixels and Wayland callbacks do not prove a usable UI.
- BackBoard HID focus has an audited discovery endpoint only. Neither client
  has invoked its lazy XPC connection. Actual semantic requests still fail
  explicitly; real focus/input behavior is incomplete.
- Normal GUI close/relaunch, two independently interactive apps, helpers,
  crash cleanup and Android acceptance remain unverified.
- Workloop sync wait/wake/return is verified; turnstile priority inheritance
  and workloop control remain unsupported.

The coordinate fix uses genuine CGContextTranslateCTM/ScaleCTM once per bitmap
context; the upright PNG above verifies it. The following diagnostic run
[39440314033](https://github.com/taowen/arlinux-xnu/actions/runs/39440314033),
source **86918ac585f06aed85d3126809bcf77f0f5fc1a0**, is terminal failure:
both original apps reach readiness and time out 124. UIKit's own bounded
recursiveDescription reports the UIButton at (20,180;280,60) and its real
UIButtonLabel at (106,21;69,18), text Increment. Thus button creation and
layout exist; the missing title pixels need color/layer-content diagnosis.
No layout was changed or control action invoked by this capture.
Receipt d55e7fab8d6e498b37eb66ec549b5e904735162f203c61c8a6ad0e114a7541b1
matches both runtime logs and app outcomes. Next: inspect actual button-label
text/tint colors and layer contents before changing rendering behavior.
No runner is currently live for this work.

Scene-frame codec evidence: run 39437411133, receipt
862d641fc074e2e8f0fc68498339cf2c06500ff591fb61b6c9799925a894227a.
Rectangle archive references at Foundation commit
[b40c41e](https://github.com/swiftlang/swift-corelibs-foundation/tree/b40c41e73612385c24be821bdfe95a67fdb157a5):
NSGeometry.swift, NSSpecialValue.swift and CoreFoundation/CFPropertyList.c.
The actual iOS framework decoding and observed bounds validate this integration;
community serialization definitions alone would not.

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

The latest verified runtime receipt and screenshot hashes are above. Firmware
identity/URL/IPSW SHA-256 live in sources.lock.json: iPhone7,2 / iOS 12.5.7 /
16H81. Apple executables, firmware, cache and cores remain transient on runners,
never workstation artifacts. Current ARHello input SHA-256 is recorded in the
exact run's app-input.json and receipt.
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
