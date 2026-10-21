# Current evidence and next boundary

## Scope and input

Run real iOS apps with selected Apple framework semantics and Linux-backed
platform services. Native ARM64, 16 KiB guest pages, one actual CPU, non-arm64e,
software framebuffer. Compilation/execution happen only on hosted runners via
build.sh. The collection has 11 source-owned apps, 14 short journeys and three
cross-app scenarios. Selected runs never certify the whole collection.

sources.lock.json pins iPhone10,1 (iPhone 8/A11), iOS 16.7.16 build 20H392,
6,035,280,776 bytes, SHA-256
**601cb23e740dacf8b61271fe02ffd5b3c146b518abb5a6e6b286e5f0475fe43b**.
Firmware and bundles are read-only; writes use each app's actual private Linux
container. Apple executable data stays runner-local.

## Latest full recovery

[Run 39621212426](https://github.com/taowen/arlinux-xnu/actions/runs/39621212426),
source **f80aa41777cbb7ed00261e423bf7001c389db0a0**, attempt 1, restores
**15/17** passing journeys. Web still fails after external input at WebCount 1;
Share still cannot display EchoExtension. The collection remains failed.
Execution receipt **5c3ea3fc886dfcc8f95e2ca17eefcee4f696f0b2e79d1010dee84dc025077a7e**
and all 264 evidence files were verified. Journeys total 215.0 s including
35.2 s diagnosis. Counter, Notes, Canvas, Swift, Network, Workers and all three
session flows pass again. Original Calculator is being added as a separate,
unverified firmware-binary input and cannot inherit these fixture results.

## Earlier complete baseline

[Run 39614075257](https://github.com/taowen/arlinux-xnu/actions/runs/39614075257),
source **e5541829df527be52376c5120bca7cca3ba3f633**, attempt 1, passes **15/17**
journeys. The collection remains **failed**: Web and Share fail. Receipt
**c0a780292df53a1036fe01c9e6e021427682d2121d500747d831e85073c2114e**
and all 348 retained files were verified against the producing log and hashes.
The same-run app input receipt is
**eaca4c5af625fd14c7e8b22ef10009940225ec183b4c7118ec051c8da6f4c4c9**.
Detailed steps, input hashes, PID/start ticks, screenshots and logs live in each
apps/CASE.json and its sibling directory inside ios-app-results-linux.

| Passed capability | Observed app effect / process identities (PID/startTicks) |
| --- | --- |
| Counter | External click changes Count 0 to 1 and composed pixels; normal exit, new-process reset and click. 5990/40574 -> 6041/40971, session 5987/40568. Binary SHA-256 f1ca366860b429e3991af4b8ff4aecd403e8e0bec0c26694aa41b323aa1a67ca. |
| Console, Foundation | Console input/normal exit/new-process relaunch; real Foundation JSON/Unicode, exception unwind and timer. Console 5980/40140 -> 5982/40353; Foundation 6088/41302. |
| Swift/UIKit | Actual Deal action computes 6, close and reset/relaunch. 6348/45001 -> 6393/45306. |
| Notes, Canvas, Workers | Actual typed text/atomic save/new-process readback, red-to-green drawing, independent pthread/memory/dispatch flows. Queue flow requires successive 3 and 6 in both generations. |
| Network | Two real foreground HTTP requests, original NSURLSession completion/JSON, answer 42 in pixels and normal exits. 7874/58306 -> 7923/58671. |
| Process | Fork/COW/shared writes, real task audit identity/ports/wait; separate named-memory alias/fork command passes after dropping the named port. Named command 6225/42744, session 6222/42739. |
| Multiple apps | Concurrent relaunch, crash isolation and focus/activation pass with separate real processes. These do not establish arbitrary helper or background lifecycle behavior. |

Feedback is 348 s including queues; journeys total 192.0 s with 29.2 s of
failure diagnosis included. Toolchain setup takes 62 s, userland preparation
47 s and integrated core compilation 9 s. Compiler cache: 170 hits, zero misses.
These measurements describe this run, not a general latency guarantee.

## Web: shared IPC works, Apple graphics backend does not

Web **6430/45590**, owned session **6427/45585**, still has no DOM/pixel or click
acceptance. Both shared-memory fixes are exercised by the original engine:

- WebContent exports its 2,097,408-byte stream using protection **0x500043**
  (READ/WRITE, IS_MASK, VM_SHARE, USE_DATA_ADDR). The named entry uses real Linux
  memfd backing and actual XNU Mach-port lifetime.
- Independent GPU processes map the transferred named handle with flags
  **0x100001** (ANYWHERE | RETURN_DATA_ADDR), current/maximum protection 3.
  Actual S2C shared file mapping is recorded at **0x30ed08000**. Original
  RemoteRenderingBackend consumes a stream CreateImageBuffer request.
- GPU workers then abort in IOSurface initialization: the original assertion
  reports that it cannot locate the IOSurface kernel service. Same-cache frames
  include ImageBufferIOSurfaceBackend::calculateSafeBackendSize,
  ImageBufferShareableMappedIOSurfaceBackend::create and
  RemoteRenderingBackend::createImageBuffer. The abort PC is **0x18dcad82c**;
  several original GPU generations fail and are replaced.

Earlier selected run 39613216515 instead stopped in original helper run-loop
waits before export/GPU creation. Therefore startup progress is not stable, and
preferences are not established as its cause. The current full run is stronger
proof of actual cross-process mapping, not proof of a working rendering backend.

Selected [run 39615956625](https://github.com/taowen/arlinux-xnu/actions/runs/39615956625),
source **e4f8b5c914ec1370bd9fb2180f9a908e9b09f52e**, attempt 1, passes Canvas
and Counter (2/3 partial). Receipt
**508c628e4179cdeb2b3f61aa39f63eb5e41b91a4517694a096bbe456074a3d81**
and all 91 files were verified. Web **5964/27974**, session **5961/27968**,
now produces original DOM inspection plus actual visible WebCount: 0 and a gray
Advance button. Its three actual features (UseGPUProcessForDOMRenderingEnabled,
UseGPUProcessForCanvasRenderingEnabled, AcceleratedDrawingEnabled) read back
from 1 to 0. The original software path produces those pixels; no IOSurface
kernel service is advertised. DOM/JS/layout and app configuration remain original.

The click step fails before sending input: the old observer requires near-white
pixels over half the button, but this genuine gray control has 717 ink and 782
near-white pixels out of 6,204. The candidate instead requires ink plus a majority
of light pixels, using the complement of the existing ink threshold on both
Linux and Apple hosts. Uniform blank frames still fail, and changed text still
requires changed composed pixels. No direct callback/action or app-specific rule
is introduced. External click, WebCount: 1 and normal close remained unaccepted in that run.

Selected [run 39617863207](https://github.com/taowen/arlinux-xnu/actions/runs/39617863207),
source **2588e77e3e4caba53eed9c889eca21e23e33c85c**, attempt 1, retains Canvas
and Counter passes (2/3 partial). Receipt
**44790e6c0a64330df031997a61ad52813a50c176cb983c2cf5683f89cfd5f863**
and all 59 files were verified. Web **6042/53538** now passes real button
observation and external click at **78,202**, but WebCount: 1 is not observed.
Pointer delivery records a UIView on begin and nil on end; no DOM callback
reports a changed value. Normal close remains blocked, and forced cleanup is
not acceptance. Feedback is 199 s; journeys 56.6 s including 19.5 s diagnosis.

The event-before-hit-test candidate does not fix the DOM input failure:
[run 39618854476](https://github.com/taowen/arlinux-xnu/actions/runs/39618854476),
source **02dc71694a5aec612a30973127affe6a41ed3887**, attempt 1, passes Canvas,
Counter and Notes (3/4 partial), but Web **6185/39098** still shows Count 0 after
external input. Receipt
**f08da97441ba68390799e3f0a873d1b667b112500e8716d2592393a02da70d62**
and all 80 files were verified. Journeys take 76.2 s, including 21.0 s diagnosis;
total feedback is 202 s. The ineffective hit-test candidate is removed.

The gesture-view candidate is rejected by the pinned userland:
[full run 39619598512](https://github.com/taowen/arlinux-xnu/actions/runs/39619598512),
source **d1908f92c14ce5c4c490d439960280b397b10602**, attempt 1, regresses to
**4/17**. Its required setGestureView: selector is absent on actual UITouch;
input initialization aborts before any pointer is delivered. Receipt
**33b9e1654f5e396898fd233ebf7f3e9eb769f620c9809d5acd218279a4245b69**
and all 242 files were verified. This is an adapter regression, not an app fault.
Feedback is 392 s; journeys 242.0 s including 128.2 s of diagnosis.

The unsupported selector requirement and call are removed. The existing opt-in
lifecycle profile now reads bounded actual touch/event gesture selectors, view
geometry/ancestors, recognizer counts and states before/after original dispatch.
The failure collector reads the actual _addTouch:forDelayedDelivery: and
_clearTouches consumers. No recognizer/action is invoked or state rewritten.
Run 39621212426 verifies recovery of the 15/17 fixture baseline; the DOM input
effect still fails. Original Calculator has its own required journey and is
not covered by that recovery result.

This run's journeys take 67.8 s (diagnosis 21.7 s included); total feedback is
534 s, including delayed GitHub finalization after execution/evidence upload.
The report entry can now read completed-step evidence before job finalization
when logs are available, retaining the real pending status and hash checks.
The delayed run became terminal during verification; the provisional-status
branch is not independently exercised by that completed-run result.

Interface references:
[WKPreferencesPrivate.h](https://github.com/WebKit/WebKit/blob/main/Source/WebKit/UIProcess/API/Cocoa/WKPreferencesPrivate.h),
[_WKFeature.h](https://github.com/WebKit/WebKit/blob/main/Source/WebKit/UIProcess/API/Cocoa/_WKFeature.h).
Actual typed runtime binding and app effects, not current upstream declarations,
establish this pinned userland's observed behavior.

## Share and limits

Share **6232/42968** cannot display the bundled EchoExtension. Original extension
discovery still requests unsupported com.apple.extensionkitservice. No extension
PID/payload/retirement/restart has passed. Do not recreate all SpringBoard or
FrontBoard services to satisfy this boundary.

The software backend does not establish GPU/Metal/IOSurface compatibility.
Named-memory conversion retains the original snapshot/rebind race with concurrent
writers; per-unmap registry retirement, unaligned data-address extraction, parent
entries and COW/fixed named mappings remain unsupported or limited. The Process
fork flow is a runtime capability, not an ordinary App Store lifecycle requirement.
HTTPS/redirect/cancellation/background networking, scene resize/background,
IME/key repeat/multitouch, third-party IPA and Android acceptance remain unverified.
Required failures stay red; no readiness marker, successful lookup, empty frame,
OCR/model recognition or direct app/control invocation establishes acceptance.

Architecture and diagnostic procedures live in their own documents. Historical
investigations and receipts remain in Git and their exact Actions runs rather
than expanding this status page. The full goal remains active.
