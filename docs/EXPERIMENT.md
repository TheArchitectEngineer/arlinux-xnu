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

Full run [39545899351](https://github.com/taowen/arlinux-xnu/actions/runs/39545899351),
source **4865c2f7b7b53cfb4aa3423200d28b82738f6f84**, attempt 1, passed **12/16**
journeys. The collection is still **failed**. Receipt
**891bf73bd2f2f4ab0f2c2bebf8b74618ab9fb0aceb79e3a6902584d3ed877cc8**
and all 233 retained files were verified. This run took 319 s; the journey step took 187 s. Individual journeys total
185.267 s, including 32.111 s of diagnosis.

Console, Foundation, Counter, Swift and Notes pass. Canvas passes actual
red-to-green repaint and normal close; Workers passes thread and mmap journeys,
but its asynchronous queue journey fails in this run. Concurrent relaunch
passes all 29 steps, with Counter surviving the
other app's normal close/relaunch. Crash isolation passes all 22 steps: Counter
remains usable after Workers is deliberately killed, and a new Workers process
can interact and exit normally. Focus activation also passes all 22 steps in
this full run. These scenarios do not establish arbitrary helper cleanup or background lifecycle behavior.

| Remaining journey | Actual failure evidence / next boundary |
| --- | --- |
| Network | The process no longer crashes on the fabricated OS-variant cache or thread-capacity failure. It times out without Network 42; actual NECP operations remain unsupported. No HTTP round trip has passed. |
| Web | The guest page-size query and 32 KiB OOL dictionary decoding now work. Original WebCore::IOSurface::maximumSize() reaches IOSurfaceClientGetPropertyMaximum and aborts because the IOSurface kernel service is absent. Requests for com.apple.WebKit.WebContent also lack a launch implementation. No DOM/helper/presentation acceptance. |
| Share | Real timed semaphore forwarding passes the earlier dispatch failure. The flow now waits without displaying EchoExtension. Extension launch/payload/retirement have not passed. |
| Workers queue | Times out without Queue 3. The actual app records submit and background value=3, but no main-stage execution. Investigate main-queue delivery/wakeup; button dispatch and background computation have been observed. No scheduling repair is claimed. |

The runtime now forwards timed semaphore operations to the existing Darling
implementation and creates workloop workers on actual epoll readiness instead
of eagerly allocating one per registered queue. Idle workers enter original
libpthread cleanup. Receipt-verified comparison against run 39543376499 retains
unchanged passing cases and the three other failures. Process and queue journey
definitions changed, so their comparison receives no progress verdict. The full
result remains failed.

Each launch has an explicit Linux-backed private home/container. Firmware and
bundles remain read-only; writes target the actual private container. Primary
pointer contacts, Alt-F4 and actual host keyboard text commits/deletion are
implemented. Notes verifies basic keyboard text entry and persistence;
scroll, IME composition, key repeat and multitouch remain unsupported. Scene
resize/background behavior, networking, WebKit and extensions require further work.
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
The full run also passed this strengthened flow (parent **6097/17298**, child
**6098/17543**). The selected run takes 125 s overall / 2.337 s journey.

The child formerly kept cached parent task handles without creating the required
rights in its own namespace. The fix reconnects the existing core, refreshes
actual task/host/thread rights and libpthread registration, and registers the
child's real COW dyld metadata. A pipe holds the parent until registration.
This proves the single-threaded pre-workqueue console boundary; live framework
sessions/kqueues, multithreaded fork and arbitrary helper lifecycle remain
unsupported. It is not an ordinary iOS application lifecycle claim.

Apple-host reference run
[39456469593](https://github.com/taowen/arlinux-xnu/actions/runs/39456469593)
previously passed the same four-app subset with native desktop events. It remains
a behavior reference, not evidence of Linux/Android capability. Architecture and
diagnostics procedures live in their own documents; no OCR, model recognition,
readiness marker or direct app/control action establishes acceptance here.
