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

Run [39511482501](https://github.com/taowen/arlinux-xnu/actions/runs/39511482501),
source **e92132e2a30265a9037b47dcbb333752e3857935**, selected Counter. Receipt
**aa844eff64c391ce48a831b4719280adf56e46dd11d4b76641a925e6e7d5271f**
and all 21 files were verified by the single build --wait --evidence command.
The run fails; useful composed app content and input are not accepted.

- The guest reads actual host window state: 1024 x 600, 60000 mHz, configured,
  active, backend-alive, audited PID/incarnation. Kernel child status also masks
  stale alive/active flags; abrupt backend termination itself remains untested.
- The genuine FBSScene initializer returns with original specification/settings
  objects and an actual FBSSerialQueue. Creation now goes through FBSScene's
  agent notification, which binds the agent before entering UIKit. The old
  direct workspace callback and its `_scene == scene` failure are removed.
- Original App delegate execution reaches UIWindow makeKeyAndVisible. Run
  39510457131's original stacks identified CAContext remote connection request
  40203; no render-server reply was fabricated. The current software backend
  selects Apple's actual localContextWithOptions: constructor instead. The
  original CAContext is successfully created; this is not displayed pixels.
- The input adapter now owns actual UIKit key-window focus locally. The prior
  remote keyboard NSXPC token-encoding failure is gone; the local token still
  rejects remote capability export. External input/keyboard are unimplemented.
- Genuine App delegate execution returns: COUNTER_READY, original label/button
  view tree, Count: 0 and 1024 x 600 viewport are observed. The scene creation
  notification returns. These establish startup, not GUI acceptance: the
  composed screenshot is still black and no app frame has been presented.
- The current fatal boundary is Apple's CALayer backing-store update:
  CABackingStorePrepareUpdates_ -> init_max_renderable_iosurface ->
  IOSurfaceClientGetPropertyMaximum -> _iosConnectInitalize assertion
  "unable to locate IOSurface kernel service". Adapt the software backing-store
  path or supply a real host-backed resource; do not suppress the assertion or
  advertise an imaginary IOSurface driver. Inspect the original rendering path
  before choosing an alternative; preserve layer/text rendering semantics.
- Counter's journey now also clicks after relaunch and requires Count: 1.
  Original-process fatal-service diagnosis preserves waiting caller stacks;
  default non-diagnostic fatal exits remain unchanged.

Session scene registration, complete workspace enumeration, compositor focus
updates, input, host-process/remote endpoint support and normal close remain
unfinished. No app/control callback is invoked by the harness or adapter.

## Verified regression and reference limits

Run [39510457131](https://github.com/taowen/arlinux-xnu/actions/runs/39510457131),
source d0ecaf5031e08d01e165e469987ecf2755f65227, passed Console and Foundation
(fullSuite=false). Receipt
772a735292885abe6304ff820004fe7ad667967a012e388f9d3e071fa228055c and all
33 files were verified. Counter failed separately. Console covers real allocation, computing pthread,
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
