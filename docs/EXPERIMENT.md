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

## Remaining scope and next experiment

Run the complete 11-app, 13-journey collection and both multi-process scenarios
against the same adapters. Storage persistence/text input, networking, WebKit,
extension processes, concurrent focus and crash isolation are not certified by
these four passes. Primary-pointer contacts and Alt-F4 are implemented; keyboard
text, scroll and multitouch are not. Session lifecycle focus updates, complete
workspace enumeration and broader process cleanup still require work.
No third-party IPA or Android acceptance has passed. The goal remains active.

The successful selected run took **135 s** from dispatch to completion, including
queues; the real app journeys took **22 s**. These are measurements, not a latency
guarantee. Firmware remains transient, hash-verified and runner-local.

Apple-host reference run
[39456469593](https://github.com/taowen/arlinux-xnu/actions/runs/39456469593)
previously passed the same four-app subset with native desktop events. It remains
a behavior reference, not evidence of Linux/Android capability. Architecture and
diagnostics procedures live in their own documents; no OCR, model recognition,
readiness marker or direct app/control action establishes acceptance here.
