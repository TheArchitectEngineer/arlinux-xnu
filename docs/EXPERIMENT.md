# Current evidence and next boundary

## Goal and fixed input

Run basic real iOS apps on Linux ARM64 using selected Apple framework semantics
and Linux-backed platform services. Counter must show real UIKit content,
respond to external input with changed state/pixels, close normally and relaunch
interactively. Console/Foundation and Swift/UIKit are independent regressions;
the existing app collection and multi-process scenarios remain required.
No complete Linux GUI, third-party IPA or Android acceptance has passed.

sources.lock.json pins iPhone10,1 (iPhone 8/A11), iOS 16.7.16 build 20H392,
6,035,280,776 bytes, SHA-256
601cb23e740dacf8b61271fe02ffd5b3c146b518abb5a6e6b286e5f0475fe43b.
Actual OS and Cryptex1,SystemOS volumes form the read-only filesystem. Apple
executable data stays runner-local. Compilation/execution use hosted runners
through build.sh. The collection has 11 source-owned apps, 13 short journeys
and two cross-app scenarios; selected runs never certify the whole collection.

## Current failed GUI effect

Run [39524469630](https://github.com/taowen/arlinux-xnu/actions/runs/39524469630),
source **9511fd0e77ccaa519978648278c192d8f112823e**, selected Console, Foundation
and Counter. Receipt
**bb23988b3836a36dcb65719dba4ae33ece91dafb4bc7bb3508da1d1fee9a6f42**
and all 38 files were verified by build --wait --evidence. Console and Foundation
passed. Counter's original label and blue Increment button appear in actual
composed pixels. Its external click was submitted, but Count remained 0; eight
later close/relaunch steps are blocked. No full GUI journey is accepted.

- The original FBSScene agent path launches UIKit; local CAContext and CPU
  CALayer backing preserve original drawing/layout/delegation. Actual resilient
  file mappings load Helvetica/SFUI instead of LastResort glyphs.
- Expanding getattrlist, filesystem enumeration and policy-query support did
  not change the failed button pixels. Original CoreUI catalog initialization
  first calls NSURL checkResourceIsReachableAndReturnError:. That advisory
  check is now backed by real Linux stat, retaining URL objects, subsequent
  opens and original CoreUI parsing. LightStandard.car is actually reachable;
  original systemBlueColor resolves and draws. No color is substituted.
- The button target now contains 419 nonwhite pixels instead of zero. The
  failed-after-click screenshot SHA-256 is
  **22789c0d44af252ce045ca030b2363bab2f291d421fa2a06412ecc5fab6e0cc6**.
  The screenshot visibly shows the label and original blue button.
- Actual loaded UITouchesEvent methods _clearTouches and
  _addTouch:forDelayedDelivery: were measured; their absence on UIEvent alone
  was not evidence that UIKit lacked these operations. The implementation now
  queues actual Wayland primary-pointer transitions and constructs real
  IOHID/UITouch events for UIKit dispatch on its main loop. This next source
  requires hosted end-to-end verification; it has not demonstrated input yet.
- Host close now enters original UIApplication termination; normal exit and
  interactive relaunch likewise remain unverified. Focus/activation updates,
  keyboard/multitouch, complete workspace enumeration and other session/process
  capabilities remain unfinished. The harness never invokes app/control actions.
- The ineffective NSError factory/initializer observers are removed. No new
  filesystem property or syscall is added to chase the former color failure.

## Feedback measurements

All three runs selected Console, Foundation and Counter with lifecycle context;
Console/Foundation passed and Counter retained the same required pixel failure.

| Run | Change | Dispatch to completion | Firmware preparation | Counter diagnosis |
| --- | --- | --- | --- | --- |
| [39519310964](https://github.com/taowen/arlinux-xnu/actions/runs/39519310964) | Baseline | 167s | 57s | 10.334s |
| [39519911112](https://github.com/taowen/arlinux-xnu/actions/runs/39519911112) | Trim recommended packages and repeated private-code dumps | 169s | 59s | 13.432s |
| [39520403966](https://github.com/taowen/arlinux-xnu/actions/runs/39520403966) | Also overlap firmware download with toolchain installation | 151s | 39s | 10.214s |

The final download took 16.059s inside toolchain setup; subsequent preparation
recorded downloadedBeforePreparation=true and still verified the complete
firmware size/hash. The 151s total is about 10% below the baseline, a single-run
comparison, not a sustained latency guarantee. Package/network/runner times vary;
the first optimization alone did not improve overall latency. Default reporting
now prints bounded evidence while retaining complete observations in the saved
report and job log. Diagnostic collection had no errors; all actual screenshot,
input, close and relaunch acceptance requirements remain unchanged.

## Verified regression and reference limits

The same run passed Console and Foundation (fullSuite=false); Counter failed
separately. Original-process diagnosis returned status 0 with no collection
errors. Console covers real allocation, computing pthread,
external stdin, normal exit and a fresh relaunch. Foundation covers Unicode JSON,
exception unwind, NSTimer computation and normal exit. Swift/UIKit has no passing Linux
journey and must be included after Counter startup progresses.

Earlier loader/Mach/runtime repairs retain actual file identities, mapped
trampoline code, message rights and synchronous wake behavior. They do not prove
GUI effects. The removed legacy FrontBoard wire serializer is not restored.
[Architecture](ARCHITECTURE.md) owns selective replacement boundaries;
[diagnostics](DIAGNOSTICS.md) owns the shortened feedback procedure and measurements.
Historical source/run/receipt details remain in Git and Actions, not this handoff.

Apple-host reference run
[39456469593](https://github.com/taowen/arlinux-xnu/actions/runs/39456469593),
source 31cdc44086cf2acb681b396ec1ba6b06830d84dd, passed Console, Counter,
Foundation and SwiftCards (partial collection). External Counter input changed
actual UIKit text and composed pixels; close/relaunch restored the initial hash.
Its relied-upon receipts/files were verified. This remains a behavior reference,
not Linux or Android acceptance. No OCR, AI recognition or injected app/control
actions establish acceptance.
