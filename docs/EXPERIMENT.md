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

Run [39523544166](https://github.com/taowen/arlinux-xnu/actions/runs/39523544166),
source **0a0528edfee5283ff3df787aad956a7cc7f977fb**, selected Counter. Receipt
**53ca2fa7234003a83c5de509a6360ebaf16c35aec0e37af4305c290df8c94dea**
and all 53 files were verified by build --wait --evidence. The process renders
the original Counter label in actual composed pixels, but its button is still
invisible. The click step fails its pixel requirement before submitting input;
nine later steps are blocked. No complete GUI journey is accepted.

- The guest reads actual host window state: 1024 x 600, 60000 mHz, configured,
  active, backend-alive, audited PID/incarnation. Kernel child status also masks
  stale alive/active flags; abrupt backend termination itself remains untested.
- The genuine FBSScene initializer returns with original specification/settings
  objects and an actual FBSSerialQueue. Creation now goes through FBSScene's
  agent notification, which binds the agent before entering UIKit. The old
  direct workspace callback and its `_scene == scene` failure are removed.
- The genuine local CAContext succeeds. CPU CGImage backing replaces the default
  CALayer _display storage, retaining original public display/delegate dispatch,
  layerWillDraw, drawInContext and CoreGraphics composition. The prior IOSurface
  kernel-service assertion is gone; no driver or remote-render reply is faked.
- Resilient private file mappings now load real Helvetica/SFUI bytes. The prior
  EINVAL -> LastResort missing-glyph boxes are gone. Composed text reads
  "Counter 1.0 — Count: 0" at the expected real UIKit label rectangle.
- The input adapter owns local key-window focus and gates Apple remote keyboard
  focus/connection IPC. The token still rejects remote capability export.
  External input and keyboard delivery are unimplemented.
- The current button boundary is UIColor resolution: systemBlueColor resolves
  to RGBA [0,0,0,0] with the actual label traits (light style 1, displayGamut 0).
  Its layer is visible with opacity 1; the 138 x 36 CPU backing has zero alpha
  pixels. Static black resolves to alpha 1 and draws 2832 alpha pixels in the
  560 x 120 main label backing. Supplying a balanced UIKit graphics context and
  actual current traits did not fix this color lookup. Do not hardcode blue.
- The actual resolver calls CUIDesignLibrary, then its composite catalog.
  The original call reports Cocoa error 256, underlying POSIX error 78 (Darwin
  ENOSYS), for `/System/Library/PrivateFrameworks/CoreUI.framework/DesignLibrary-iOS.bundle/iOSRepositories/LightStandard.car`.
  UIKit falls back to clearColor after that failed resource lookup. The same
  actual path appears in successful getattrlist (220), with five attribute
  groups recorded: common=0xd03fde0d, volume=0, directory=5, file=1, fork=0x40.
  File metadata, declared filesystem enumeration, no-Seatbelt queries and
  invalid-volume-attribute packing now return their Linux-backed results, but
  the composed frame is unchanged (SHA-256
  0f4856ad6a56f5189249b94c7c76af2e8d6cd3fa0ba1f2d50d48517953202e07).
  No nearby ENOSYS or actual resource open establishes the read rejection.
  NSError's factory observer captured no construction stack. Next follow the
  actual catalog initializer and error initializer to choose a shared file-read
  adaptation boundary; do not add unrelated syscalls or hardcode a color.
- Frame evidence now writes a temporary file and atomically publishes complete
  frames. This run's encoder passes; incomplete temporary files never replace
  the last completed frame. The failure summary samples four screenshots while
  preserving every observation and its files in the full journey evidence.
- Counter's journey now also clicks after relaunch and requires Count: 1.
  Original-process fatal-service diagnosis preserves waiting caller stacks;
  default non-diagnostic fatal exits remain unchanged.

Session scene registration, complete workspace enumeration, compositor focus
updates, input, host-process/remote endpoint support and normal close remain
unfinished. No app/control callback is invoked by the harness or adapter.

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
