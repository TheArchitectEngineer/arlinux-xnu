# Current evidence and goal

Run a basic real UIKit app on Linux ARM64 with iPhone 8/A11 iOS 16.7.16:
visible content, actual input, changed state and pixels, normal exit and fresh
relaunch. Console, Foundation and Swift/UIKit are independent regression layers.
No OCR, AI recognition, replacement framework or fabricated success is used.
No complete Linux GUI, third-party IPA or Android acceptance has passed.

## Fixed input and acceptance

sources.lock.json pins iPhone10,1, build 20H392, 6,035,280,776 bytes, SHA-256
601cb23e740dacf8b61271fe02ffd5b3c146b518abb5a6e6b286e5f0475fe43b.
Runner validation checks actual download bytes, BuildManifest and mounted system
version. Real OS and Cryptex1,SystemOS volumes form the read-only filesystem.
Apple executable data stays runner-local; retained downloads are text,
screenshots and owned products.

The collection has 11 source-owned apps, 13 short journeys and two cross-app
scenarios. Xcode 16.4's real SDK cross-compiles ARM64 apps on a hosted Intel Mac;
Linux execution and optional Apple reference execution remain native ARM64.
Input products use the exact producer receipt SHA-256 and per-file hashes.
Read-only UIKit/DOM inspection never invokes actions; GUI acceptance additionally
requires external input, actual composed pixels and normal lifecycle effects.

Apple reference run [39456469593](https://github.com/taowen/arlinux-xnu/actions/runs/39456469593),
source 31cdc44086cf2acb681b396ec1ba6b06830d84dd, passed Console, Counter,
Foundation and SwiftCards in 10 seconds (partial collection). Counter input
changed genuine UIKit text and the composed label-region hash; close/relaunch
returned to the original hash. Its receipt and relied-upon screenshots/result
hashes were verified. This is a behavior reference, not Linux acceptance.

## Latest verified Linux result

Run [39477341682](https://github.com/taowen/arlinux-xnu/actions/runs/39477341682),
source **e88cc6e8ac8c6ce0f96185d298b816095eb59588**, selected Console, Counter,
Foundation and SwiftCards. The result receipt SHA-256 is
**7befc8cd7598efc7a51f94081f72e1ac8d3c504ada84502e28f4ca3c9327441c**;
the receipt and retained text artifact hashes were verified locally.
No UIKit GUI journey has passed. This is a partial collection, not device acceptance.

- Console's complete interactive/relaunch journey passes, as it has since source
  d75e519. Both generations perform genuine malloc and pthread computation,
  accept input, print Counter: 1 / Goodbye and exit normally with status 0.
- Foundation's complete objects/exception/timer journey passes, first established
  in run 39473090148 at source db152bc. Actual NSJSONSerialization preserves
  Unicode/collections, real exception unwind succeeds, and NSTimer runs its
  callback. The computed result is 10; q produces normal exit status 0.
- The loader uses the actual 43-member cache, 2,705 images, slide-v2 relocation,
  TLS adaptation, immutable commpage, cache-file identity, dyld image-info,
  main-stack metadata and kernel entropy. Real descriptor paths and read-only
  Darwin openat now reach actual files; absent files retain genuine errors.
- Modern dispatch uses actual workers/event callbacks. Workqueue returns replace
  saved guest contexts through Linux signal return; ordinary completed workers
  enter real libpthread cleanup. Fallback QoS and optional adaptive-spin flags
  no longer reject otherwise-valid actual worker/futex operations.
- Real task_info/host_info and vector messages use Darling Mach/MIG queues.
  Sync-receive translation now carries the actual remote send right in the
  legacy priority slot. Run 39476150717 verified option 0x40700420e returns 0,
  replacing MACH_RCV_INVALID_NOTIFY; it did not certify the whole XPC service.
- BackBoard request 6001009 is a measured 24-byte request / 60-byte reply,
  not the 68-byte receive allocation. Actual Wayland dimensions are 1024x600;
  the client passes its original BackBoard initialization assertion.
- __CASGetDisplays request 40231 is 36 bytes, including NDR and a zero selector.
  Its real decoder validates the 56-byte complex reply and single OOL descriptor.
  Serving the actual display list now delivers 250 bytes through real Mach
  copyout/rights. Counter's original frame chain reaches query_displays +928
  and CA::Display::Display::update +128. This is actual parsing/display-object
  progress, not visible pixels or successful controls.
- A concurrent earlier run also observed cfprefsd.m:171 client_context_finalizer
  asserting !context->_valid. Its relation to main-thread startup failure remains
  unproven; the assertion has not been disabled.

## Current runner handoff

The latest frame chain exposes mixed ownership: bootstrap lookup started a real
QuartzCore RenderServer inside the application, while the declared display
service is our Linux/Wayland producer. CA::Display::update then enters the
framework's local-server path instead of consistently querying that producer.
Source **e3d5b7d94318273610a6da1ebd71030aa80749b1** removes that lookup side
effect and its mixed in-app forwarding path/API binding. Real UIKit/QuartzCore
client code and CoreGraphics rendering remain in use; unknown service methods
still fail. No replacement views, controls or draw results are supplied.
Run [39478502832](https://github.com/taowen/arlinux-xnu/actions/runs/39478502832)
is its exact four-app regression; runtime effects remain unverified.

Source **a9db7db00b93f6534224f0bae87864d22ec87f2d** additionally retains up to
2,048 decoded bytes around measured display callers, because Display::update
inlines its variable MIG decoder. Original fault frame records now symbolize
saved return addresses at the actual ARM64 call instruction, avoiding false
names at the following function boundary. The first three original callers,
bounded referenced literals and the explicitly measured query_displays client
remain available even if another thread aborts first. This is read-only evidence.
Run [39478733527](https://github.com/taowen/arlinux-xnu/actions/runs/39478733527)
is the current exact four-app regression. Poll the existing runs; do not restart
because observation timed out.

Next verify display-owner routing and implement the actual current display
properties request/reply. The observed Display::update client builds a 40-byte
request 40232 and validates four descriptors; the existing producer handles the
legacy 36-byte/two-descriptor layout. Its detailed decoder is the next evidence
source, not a reason to return unmeasured success. Resolve original lifecycle
failures, then require visible Counter pixels, external click/state/pixel change,
normal close and fresh relaunch. No compilation, console, registration or IPC
result certifies that goal. History stays in Git and Actions.
