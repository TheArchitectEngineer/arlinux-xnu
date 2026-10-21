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

Run [39518111587](https://github.com/taowen/arlinux-xnu/actions/runs/39518111587),
source **02d016dec9003714d2048ae9bc2d99cba115e869**, selected Counter. Receipt
**11be997783cf98e3469ed6e1f47feb71e7ea325f2be3ce95d757893ff84ea0d2**
and all 52 files were verified by build --wait --evidence. The process renders
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
  UIKit falls back to clearColor after that failed resource lookup. Next connect
  the missing filesystem operation to its actual path, masks and caller chain;
  changing the app's color or display gamut would hide this evidence.
- Frame evidence now writes a temporary file and atomically publishes complete
  frames. This run's encoder passes; incomplete temporary files never replace
  the last completed frame. The failure summary samples four screenshots while
  preserving all 32 observations and their files in the full journey evidence.
- Counter's journey now also clicks after relaunch and requires Count: 1.
  Original-process fatal-service diagnosis preserves waiting caller stacks;
  default non-diagnostic fatal exits remain unchanged.

Session scene registration, complete workspace enumeration, compositor focus
updates, input, host-process/remote endpoint support and normal close remain
unfinished. No app/control callback is invoked by the harness or adapter.

## Verified regression and reference limits

Run [39514010952](https://github.com/taowen/arlinux-xnu/actions/runs/39514010952),
source 6f7fade465a035f261b08a8af8233af00257e49c, passed Console and Foundation
(fullSuite=false). Receipt
1fbe7255461ff6fc8c7c79928e0375334375d6701c3fc12488e33df0a431734c and all
65 files were verified. Counter failed separately. Console covers real allocation, computing pthread,
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
