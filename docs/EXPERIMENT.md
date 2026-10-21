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
and two cross-app scenarios; selected runs never certify the whole collection.

## Verified basic app flows

Run [39530266534](https://github.com/taowen/arlinux-xnu/actions/runs/39530266534),
source **a81c909075056764ac65c4a1fa2df7d8268b7ff7**, attempt 1, passed Console,
Foundation, Counter and Swift/UIKit in the ordinary non-diagnostic path.
This is a **partial collection**, not full-suite or Android acceptance.
Receipt **05826d17d82fc7aeb33a7561f30c603398cddce67a5df0b32e2f732c00dbf10d**
and all 55 retained files were verified against the producing log and SHA-256.

| App | Actual verified effect |
| --- | --- |
| Counter | Original UIKit label and blue button; external click changes Count 0 to 1 and its pixels; normal exit 0; new process resets to 0, accepts another external click to 1, normal exit 0 |
| Swift/UIKit | External Deal click computes 6 through Swift generics/closure/Objective-C action dispatch; pixels change; normal exit 0; new process resets to 0 and closes normally |
| Console | Real allocation/thread computation, external stdin, normal exit and independent relaunch |
| Foundation | Original Unicode JSON objects, exception unwind, actual NSTimer computation, external input and normal exit |

Counter identities are PID/startTicks **5705/13315** and **5751/13680**;
Swift identities are **5803/14533** and **5845/14858**. Counter's original label
region hash changes from
**4b468bb8438044978505d8866114b3afa2ad4a000bdb36179513a4c8f39cab8a**
to **2b551edea55b6c1d0d0e31dc6dd7945e4afadfd06cf65153ebcccabd8c2c93f5**,
then repeats the reset/change after relaunch. Intermediate blank captures remain
in evidence and do not satisfy the composed-pixel requirement.

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

## Complete collection and remaining failures

Full run [39538117840](https://github.com/taowen/arlinux-xnu/actions/runs/39538117840),
source **45c9b1f54025bb66d5fd5fec9f0b3abeb8c4a3d4**, attempt 1, passed **11/15**
journeys. The collection is still **failed**. Receipt
**78f3b512556e4a57e8e2c545166a0e668c335aa9da436659eda120b82c13b635**
and all 218 retained files were verified. This run took 295 s; app journeys 167 s.

Alongside the four basic apps, Canvas passes actual red-to-green repaint and
normal close; Workers passes independent thread, mmap and asynchronous queue
journeys. Concurrent relaunch passes all 29 steps, with Counter surviving the
other app's normal close/relaunch. Crash isolation passes all 22 steps: Counter
remains usable after Workers is deliberately killed, and a new Workers process
can interact and exit normally. This proves those scenarios, not all process
cleanup or UIKit scene activation-state transitions.

| Remaining journey | Actual failure evidence / next boundary |
| --- | --- |
| Network | The process no longer crashes on the fabricated OS-variant cache or thread-capacity failure. It times out without Network 42; actual NECP operations remain unsupported. No HTTP round trip has passed. |
| Web | Original fault arguments identify WTF::pageSize() during JavaScriptCore initialization, immediately after a failed system query. Investigate the guest page-size query, consistent with WebKit PageBlock.cpp, before assuming a JIT failure. No DOM output accepted. |
| Share | Real timed semaphore forwarding passes the earlier dispatch failure. The flow now waits without displaying EchoExtension. Extension launch/payload/retirement have not passed. |
| Process | Child-side MIG reply-port construction fails after fork. Fork/session/rights inheritance needs implementation; this console capability is not an ordinary iOS app lifecycle claim. |

The runtime now forwards timed semaphore operations to the existing Darling
implementation and creates workloop workers on actual epoll readiness instead
of eagerly allocating one per registered queue. Idle workers enter original
libpthread cleanup. This full run retains the earlier ten passing journeys and adds Notes; it
does not establish complete scheduling or arbitrary helper cleanup.

Each launch has an explicit Linux-backed private home/container. Firmware and
bundles remain read-only; writes target the actual private container. Primary
pointer contacts, Alt-F4 and actual host keyboard text commits/deletion are
implemented. Notes verifies basic keyboard text entry and persistence;
scroll, IME composition, key repeat and multitouch remain unsupported. Session
activation-state updates, networking, WebKit and extensions require further work.
No third-party IPA or Android acceptance has passed. The full goal remains active.

The sole entry can select exact app journeys and scenarios through --case;
SDK compilation and execution use the same dependency selection. Comparison
reports require both runs' verified receipts and unchanged journey definitions.
Selected runs remain partial. Feedback measurements belong to the exact run,
not a latency guarantee; firmware stays transient, hash-verified and runner-local.

Tooling verification run
[39536560764](https://github.com/taowen/arlinux-xnu/actions/runs/39536560764),
source **4f6b591e209f25b583cf6a1bb724efeff748fd12**, selected only Notes,
Workers queue and concurrent relaunch, compiling Counter/Notes/Workers. It took
175 s overall and 62 s in journeys, versus 315/171 s for the full baseline.
This saves unrelated work, not an equivalent full-suite speedup. Both receipts
and all retained files were verified; the selected artifact has 69 files.
Concurrent relaunch passed; Notes failed at the same CoreEmoji assertion.
Workers queue passed in the baseline but timed out here despite pointer
begin/end delivery to its UIButton. Its original-process stacks are retained;
the cause remains unresolved. The runtime source was unchanged by this tooling
commit, so this is a changed result, not evidence that selection introduced a
runtime defect. Twelve omitted journeys are explicitly untested. The comparison
correctly preserves both failures and writes their effect/diagnostic timings.

Apple-host reference run
[39456469593](https://github.com/taowen/arlinux-xnu/actions/runs/39456469593)
previously passed the same four-app subset with native desktop events. It remains
a behavior reference, not evidence of Linux/Android capability. Architecture and
diagnostics procedures live in their own documents; no OCR, model recognition,
readiness marker or direct app/control action establishes acceptance here.
