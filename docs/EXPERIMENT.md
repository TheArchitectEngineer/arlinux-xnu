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

## Latest complete collection

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

The next candidate selects WebKit's own software rendering preferences at the
WKWebView configuration boundary, through actual feature enumeration, typed
method checks and readback. It keeps original DOM/JS/layout, does not advertise
an IOSurface driver, and does not modify app source/actions. Feature selection
and resulting pixels still require runner validation. Source/API references:
[WKPreferencesPrivate.h](https://github.com/WebKit/WebKit/blob/main/Source/WebKit/UIProcess/API/Cocoa/WKPreferencesPrivate.h),
[_WKFeature.h](https://github.com/WebKit/WebKit/blob/main/Source/WebKit/UIProcess/API/Cocoa/_WKFeature.h).
Current upstream interfaces guide binding; only actual pinned-cache/runtime
observations and app effects establish compatibility.

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
