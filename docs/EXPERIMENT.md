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

Full run [39531478920](https://github.com/taowen/arlinux-xnu/actions/runs/39531478920),
source **10a20ef4f8a436bbac8b7b778fb902f11143f4e2**, attempt 1, passed **10/15**
journeys. The collection is still **failed**. Receipt
**1817ca54ae9a32ab83ade79b62ee441ad4633382ed6ef5e130b6eb5c0e2c5b06**
and all 202 retained files were verified. This run took 285 s; app journeys 149 s.

Alongside the four basic apps, Canvas passes actual red-to-green repaint and
normal close; Workers passes independent thread, mmap and asynchronous queue
journeys. Concurrent relaunch passes all 29 steps, with Counter surviving the
other app's normal close/relaunch. Crash isolation passes all 22 steps: Counter
remains usable after Workers is deliberately killed, and a new Workers process
can interact and exit normally. This proves those scenarios, not all process
cleanup or UIKit scene activation-state transitions.

| Remaining journey | Actual failure evidence / next boundary |
| --- | --- |
| Notes | Uncaught NSInvalidArgumentException: NSFileManager fileSystemRepresentationWithPath receives a nil/empty path while UIKeyboardCache initializes. Trace the actual home/cache path and Linux keyboard boundary before changing storage or UI behavior. Text/save/reopen has not passed. |
| Network | Original os_variant internal-security query crashed on a fabricated partial status cache. The cache claim was removed; latest original process reaches further networking code but still faults, with NECP operations observed. No HTTP round trip has passed. |
| Web | JavaScriptCore WTFCrashWithInfo during WebKit initialization; retain original caller/argument evidence before deciding the platform dependency. No DOM output accepted. |
| Share | Original libdispatch reports a Mach semaphore timed-wait API failure during presentation. Extension launch/payload/retirement have not passed. |
| Process | Child-side MIG reply-port construction fails after fork. Fork/session/rights inheritance needs implementation; this console capability is not an ordinary iOS app lifecycle claim. |

Latest runtime source **f7a27d5966ba73523df8abefce12e3f2ce1ff376** was checked in
run [39531756257](https://github.com/taowen/arlinux-xnu/actions/runs/39531756257).
Console, Foundation, Counter and Swift still pass after removing the false OS
variant cache. Network still fails at a different boundary. Receipt
**468d08d9aeb0512b2f0cdf2baf7ed6b525e150b461709204c2928a2e9e2620a5**
and 67 files were verified. The other ten journeys were not rerun at this source;
the 10/15 result above belongs to its explicitly recorded earlier source.

Primary-pointer contacts and Alt-F4 are implemented; keyboard text, scroll and
multitouch are not. Home/container storage, networking, WebKit and extension
services remain incomplete. Session lifecycle activation-state updates, complete
workspace enumeration and broader process cleanup still require work. The next
implementation boundary is the actual Notes keyboard/home/cache failure, while
retaining the independently diagnosed network/semaphore/fork/WebKit failures.
No third-party IPA or Android acceptance has passed. The full goal remains active.

The successful selected run took **135 s** from dispatch to completion, including
queues; the real app journeys took **22 s**. These are measurements, not a latency
guarantee. Firmware remains transient, hash-verified and runner-local.

Apple-host reference run
[39456469593](https://github.com/taowen/arlinux-xnu/actions/runs/39456469593)
previously passed the same four-app subset with native desktop events. It remains
a behavior reference, not evidence of Linux/Android capability. Architecture and
diagnostics procedures live in their own documents; no OCR, model recognition,
readiness marker or direct app/control action establishes acceptance here.
