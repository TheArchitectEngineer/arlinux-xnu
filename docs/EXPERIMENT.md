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

Run [39507197204](https://github.com/taowen/arlinux-xnu/actions/runs/39507197204),
source **b5251c60f56101b0dbf0646ac19d0c41a5b1c2c8**, selected Counter. Receipt
**8deff26a85336b0d29272f8646152278950ee33592fe73fdd876a9e005d891f4**
and all 20 files were verified by the single build --wait --evidence command.
The run fails; no Counter launch delegate, useful content or input is accepted.

- The guest reads actual host window state: 1024 x 600, 60000 mHz, configured,
  active, backend-alive, audited PID/incarnation. Kernel child status also masks
  stale alive/active flags; abrupt backend termination itself remains untested.
- The platform adapter now enters the real FBSScene initializer with original
  identity/specification/settings/client-settings objects and an actual
  FBSSerialQueue/updater definition. This is not successful scene creation.
- The original trap PC is 0x19b93237c, within that initializer's explicit
  "settings class differs from specification" failure path. A mutable settings
  object was passed where the exact immutable specification class is required.
- The current change copies configured settings back to their immutable form
  and schedules scene initialization/delivery on the genuine callout queue.
  Those changes await hosted validation. Session scene registration, complete
  enumeration, focus updates, input, host-process/remote endpoint support and
  normal close remain unfinished.

The prior run
[39506273079](https://github.com/taowen/arlinux-xnu/actions/runs/39506273079),
source 9617a69fbe2c921626b1ebce2267fa245f1edbb2, failed before scene creation.
Its verified receipt is
ed63db08eb4b4a21d5c08f47b536a7a1adff7dd8af3e71bb1ea4d3934fcd39b6 (20 files).
Same-cache token-constructor code rejects nil endpoints and validates the remote
service identity. The replacement local token is an immutable subclass with
real local identity/copy/equality/hash, no remote endpoint and explicitly
unsupported remote encoding. It passed those early constructor checks in the
current run. No remote Apple capability or service success is fabricated.
Local exception logging retains the original name/reason and rethrows.

## Verified regression and reference limits

Run [39504697195](https://github.com/taowen/arlinux-xnu/actions/runs/39504697195),
source 2326e771feaa4ba5bc9b67f9ec63965858259bfb, passed Console and Foundation
(fullSuite=false). Receipt
fbed9e7d32538114fb9a03ccdff762720b37d22bf166fd6396ce7b6e8a876e45 and all
23 files were verified. Console covers real allocation, computing pthread,
external stdin, normal exit and a fresh relaunch. Foundation covers Unicode JSON,
exception unwind, NSTimer computation and normal exit. They have not been rerun
against the newly introduced scene adapter. Swift/UIKit has no passing Linux
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
