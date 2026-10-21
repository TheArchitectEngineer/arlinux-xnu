# Current evidence and next boundary

## Goal and fixed input

Run basic real iOS apps on Linux ARM64 using selected Apple framework semantics
and Linux-backed platform services. Counter must show real UIKit content,
respond to external input with changed state/pixels, close normally and relaunch
interactively. Console/Foundation and Swift/UIKit are independent regressions;
the existing app collection and multi-process scenarios remain required.
Basic Linux GUI flows below pass; the full collection, third-party IPA and
Android acceptance remain incomplete.

sources.lock.json pins iPhone10,1 (iPhone 8/A11), iOS 16.7.16 build 20H392,
6,035,280,776 bytes, SHA-256
601cb23e740dacf8b61271fe02ffd5b3c146b518abb5a6e6b286e5f0475fe43b.
Actual OS and Cryptex1,SystemOS volumes form the read-only filesystem. Apple
executable data stays runner-local. Compilation/execution use hosted runners
through build.sh. The collection has 11 source-owned apps, 13 short journeys
and three cross-app scenarios; selected runs never certify the whole collection.

## Verified focus and reactivation

Selected run [39543092531](https://github.com/taowen/arlinux-xnu/actions/runs/39543092531),
source **9a875fe712eea34ec3005de68bc0bda8878f716c**, attempt 1, passes all 22 steps
of session--focus-activation. Receipt
**b06feda94ad95ae620a8a66f203c1adbbc4229691267ef63feca24877cd1dcca**
and all 29 retained files were verified against the producing log and hashes.
Counter **5831/48779** and Workers **5865/49140** (PID/startTicks) follow actual
compositor focus. Original UIApplication state changes active/inactive with real
notifications. Counter reaches didBecomeActive=3 / willResignActive=2, remains
interactive with Count 1/2 composed pixels, and both apps exit normally with 0.
This proves visible-window focus, not minimize/background, resize or Android.

The original scene updater receives immutable settings/diffs on its own queue;
its opaque completion object is not acceptance. Original HangTracer also needs
shared anonymous memory on reactivation. Actual Linux MAP_SHARED backing fixes
that shared runtime boundary without a HangTracer patch. The dedicated flow takes
12.362 s, 134 s including preparation/queues. Compiler cache: 167 hits, one miss;
toolchain setup 38 s and userland preparation 43 s dominate preparation. These
are measured costs for this run, not a general latency guarantee.

## Working adaptation boundaries

Notes now passes its complete edit/save/reopen journey in selected run
[39537811333](https://github.com/taowen/arlinux-xnu/actions/runs/39537811333),
source **45c9b1f54025bb66d5fd5fec9f0b3abeb8c4a3d4**, attempt 1. Console,
Foundation, Counter and Swift/UIKit also pass at that source. Receipt
**ed4215d621f1d1231096ec4c811038ff23836ddec6fa23a714054ee7a86dd6c4**
and all 74 retained files were verified. This remains a partial collection.
Notes starts empty, receives actual keyboard input `app-note-42`, saves through
original Foundation atomic file writing, closes with exit 0, and reads the
content after a new-process launch before another exit 0. Process identities are
**6113/92769** and **6197/93216** (PID/startTicks). The saved label region hash
**c2480e721003b4d440cdf1b11d286c22e6fde6355850acf930dcc4d1507817cb**
matches after relaunch; initial blank frames remain rejected. Actual Linux
temporary-file creation and rename are retained in the runtime log. This run
took 156 s overall and 30 s in journeys.

- Genuine FBSScene agent creation enters original UIKit launch. Local CAContext
  and CPU CALayer backing preserve original layout/text/drawing. Actual resilient
  file mappings load Helvetica/SFUI. This is software composition, not GPU proof.
- NSURL file reachability uses real Linux stat and real Cocoa/POSIX errors;
  original CoreUI resource parsing produces systemBlueColor. Extra filesystem
  metadata support alone had not fixed that failure. No fixed color is supplied.
- Actual Wayland seat pointer transitions enter a per-process bounded queue.
  The guest main loop creates IOHID/UITouch events, uses original hit testing and
  UIApplication event dispatch. The harness sends only external desktop input.
- Host Alt-F4/xdg-close enters original UIApplication termination. Counter and
  Swift verify normal process exits and fresh identities. The input harness
  checks actual pointer coordinates rather than waiting for movement when the
  pointer is already at its target.
- Ineffective NSError-construction observers are removed; original-process
  fault/unsupported-call/LLDB evidence remains available. Historical diagnosis
  failures and their repairs remain in Git/Actions rather than a growing diary.

- The pinned libspc fork **654ecfd5fb9ffbea2e86242bc4d87cf83a274459** now
  decodes bounded inline/OOL dictionary spans with owned values and transferred
  rights. Web's actual 32,768-byte CPX5 body was decoded, then released through
  the real Mach VM deallocation RPC with status 0. The session continued handling
  later requests. Unknown bootstrap operations remain unsupported; no helper
  launch is claimed. Decoder changes live in the public fork, not a build patch.
  The temporary raw framing probe was removed after establishing this layout.

## Complete collection and remaining failures

Earlier full baseline [39547510074](https://github.com/taowen/arlinux-xnu/actions/runs/39547510074),
source **cd29bf9626899d583147a10a2f0214d53edf6ce3**, attempt 1, passed **13/16**
journeys. The collection is still **failed**. Receipt
**ad95702ad12daf4a283cf92a6c3518273d4308089d10c402804b49c465b26eec**
and all 239 retained files were verified. Individual journeys total 172.536 s,
including 23.980 s of diagnosis.

Console, Foundation, Counter, Swift, Notes, Canvas, Process and all three Workers
journeys pass in this full run. Concurrent relaunch passes all 29 steps; crash
isolation and focus activation each pass all 22 steps. These scenarios do not
establish arbitrary helper cleanup or background lifecycle behavior.

| Capability | Current evidence / next boundary |
| --- | --- |
| Network | Foreground HTTP/JSON now passes twice across normal close/new-process relaunch in the selected run below. General HTTPS, redirects, cancellation and background networking are not yet accepted. NECP and arbitrary BSD sockets remain unimplemented. |
| Web | Original WebCore::IOSurface::maximumSize() reaches IOSurfaceClientGetPropertyMaximum and aborts because the IOSurface kernel service is absent. WebContent launch is also unimplemented. No DOM/helper/presentation acceptance. |
| Share | The flow waits without displaying EchoExtension. Extension launch/payload/retirement have not passed. |

Workers queue remains intermittent: earlier full run 39545899351 recorded actual
submit/background value=3 but no main stage. No scheduling repair is claimed.
The strengthened queue journey now requires two clicks, Queue 3 then Queue 6,
normal exit, a fresh process, and the same two deliveries and normal exit again.
Selected run [39548964193](https://github.com/taowen/arlinux-xnu/actions/runs/39548964193),
source **5a3d835c43c37b4f469de8e696941bf9ccb6d785**, attempt 1, passes all three
Workers journeys. Receipt
**609a79057061c77b65e580258bc8d1e6f23419f9feac365560bcb9d36a3fad46**
and 48 files were verified. Queue generations are **5864/19727** and
**5927/20124** (PID/startTicks). Journeys total 15.579 s, including 8.947 s for
queue; overall feedback takes 142 s. This partial pass neither establishes the
intermittent cause nor certifies the other apps with this changed source.

Current selected run [39552858185](https://github.com/taowen/arlinux-xnu/actions/runs/39552858185),
source **bbcea4052d92bff88e3a1c63a88764b38f2daa03**, attempt 1, passes **4/4**:
Counter, Foundation, Swift and the strengthened Network journey. Receipt
**b1d38e02bd75f2972047a703e3633eee859b7bda0d3c21e2deb8c7a41b6982e4**
and 67 files were verified. Network processes **6163/19520** and **6211/19866**
(PID/startTicks) each receive a real status 200 / 13-byte HTTP response through
the Linux transport and original NSURLSession completion. Original JSON parsing
and main-queue delivery produce Network: 42 in both composed screenshots;
the real peer records two requests and both processes exit 0. Both label regions
have SHA-256 **f644f4534ec9f43dced20328a20848908027283d18ce86692efbf40c4af929d4**;
the second composed screenshot was also visually inspected. The 14-step Network
journey takes 7.215 s; all four total 25.986 s, with 161 s overall feedback.
This is a partial Linux result, not whole-suite, HTTPS or Android acceptance.

The last completed full candidate,
[39552256606](https://github.com/taowen/arlinux-xnu/actions/runs/39552256606), source
**02fb54c6521a32b97cf9849fd6dee7445e224d2a**, regressed to **2/16**: extra kmsg
ownership fields had reduced the original inline capacity, breaking real port
preallocation and terminating shared sessions. Receipt
**9470367e04640c6315b2b8c3b6dc2473ebcf0559aee1b6dae1113d5d7062d1c5**
and 165 files were verified. The current correction adds auxiliary ownership
storage to the allocation, preserving the original capacity; the four-app run
above verifies that boundary again. Full revalidation of that same corrected
runtime is [39553178774](https://github.com/taowen/arlinux-xnu/actions/runs/39553178774).
At this update it is still running in toolchain preparation; do not replace its
unknown result with the earlier 13/16 baseline or the selected 4/4 result.

Each launch has an explicit Linux-backed private home/container. Firmware and
bundles remain read-only; writes target the actual private container. Primary
pointer contacts, Alt-F4 and actual host keyboard text commits/deletion are
implemented. Notes verifies basic keyboard text entry and persistence;
scroll, IME composition, key repeat and multitouch remain unsupported. Scene
resize/background behavior, broader networking, WebKit and extensions require further work.
No third-party IPA or Android acceptance has passed. The full goal remains active.

The sole entry can select exact app journeys and scenarios through --case;
SDK compilation and execution use the same dependency selection. Comparison
reports require both runs' verified receipts and unchanged journey definitions.
Selected runs remain partial. Feedback measurements belong to the exact run,
not a latency guarantee; firmware stays transient, hash-verified and runner-local.

The latest fork guard is verified separately in selected run
[39546263777](https://github.com/taowen/arlinux-xnu/actions/runs/39546263777), source
**de54ab793d74584732bdb501aaa58f7ae9190b6d**, attempt 1. Receipt
**aa1d6447dd52f4c88d054224faf204ff531b160a001b4ca4f0de2eb64462cbd5**
and 16 files were verified. Parent **6040/270974** and child **6045/271192**
(PID/startTicks) use distinct actual tasks; TASK_AUDIT_TOKEN matches each real
PID, and each allocates/releases its own receive port. Parent private memory
stays 11 while the child's private write is 99; shared memory becomes 42.
The child exits 37, the parent reaps it, accepts external stdin and exits 0.
Full run 39545899351 also passed this strengthened flow (parent **6097/17298**,
child **6098/17543**). The selected run takes 125 s overall / 2.337 s journey.

The previous child failed at original MIG reply-port construction with
MACH_SEND_INVALID_DATA (0x10000002), not MACH_SEND_INVALID_DEST. That identifies
a child RPC/memory boundary failure, not a proven invalid destination right.
The fix renews the child's exact-core memory-access authorization and sockets,
refreshes actual task/host/thread rights and libpthread registration, and
registers its real COW dyld metadata. A pipe holds the parent until registration.
The complete boundary repair is verified; an isolated cause among those missing
initialization steps has not been established.
This proves the single-threaded pre-workqueue console boundary; live framework
sessions/kqueues, multithreaded fork and arbitrary helper lifecycle remain
unsupported. It is not an ordinary iOS application lifecycle claim.

Apple-host reference run
[39456469593](https://github.com/taowen/arlinux-xnu/actions/runs/39456469593)
previously passed the same four-app subset with native desktop events. It remains
a behavior reference, not evidence of Linux/Android capability. Architecture and
diagnostics procedures live in their own documents; no OCR, model recognition,
readiness marker or direct app/control action establishes acceptance here.
