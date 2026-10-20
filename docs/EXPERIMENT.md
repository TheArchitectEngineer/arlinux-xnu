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

Run [39473535129](https://github.com/taowen/arlinux-xnu/actions/runs/39473535129),
source **692b4eb5797880af1b882989c9b10f386090b19c**, completed in 242 seconds.
The result receipt SHA-256 is
**5f2153fa25f2e5ec9f29459bb99585ec0460cfe38a2d960b011c679207b8e8ff**;
the receipt and retained text artifact hashes were verified locally.
This is the partial Console/Counter/Foundation/SwiftCards selection.

- Console's full interactive/relaunch journey passes, as it has since source
  d75e519. Both generations perform genuine malloc and pthread computation,
  accept input, print Counter: 1 / Goodbye and exit normally with status 0.
- Foundation's complete objects/exception/timer journey passes, first established
  in run 39473090148 at source db152bc. Actual
  NSJSONSerialization preserves Unicode/collections; real exception unwind
  succeeds; NSTimer runs its callback. It prints the computed result 10, accepts
  q and exits normally with status 0. It does not certify a graphical app.
- The loader handles 43 UUID-declared cache members and 2,705 images, actual
  slide-v2 relocation, TLS adaptation, immutable commpage, cache-file identity,
  dyld image-info registration, main-stack metadata and real kernel entropy.
  Descriptor paths resolve through actual Linux file descriptors.
- Actual task_info/host_info requests return through Darling Mach/MIG. Vector
  receives use real message delivery and the XNU no-auxiliary EOF contract.
  Modern dispatch reaches real workers. Workqueue returns replace saved guest
  contexts instead of returning into libpthread's fatal path; actual event
  callbacks and ordinary worker exit use its genuine upcalls. Fallback QoS
  requests create real workers rather than being rejected as unknown flags.
- Counter and SwiftCards now get beyond _BKSDisplayServicesStart. Counter
  reaches UIApplicationMain and creates subsequent threads, then its real unfair
  lock wait rejects the adaptive-spin flag (syscall 515, operation 0x1050002).
  libsystem_platform traps on that error. No visible UIKit content or actual GUI
  interaction has passed on this firmware.
- The libxpc audit predicate adapter preserves actual credentials and original
  echo/rejection branches. Runtime provider-authentication success is unproven.

## Current runner handoff

Original-fault disassembly follows bounded named display callees on the runner.
Run [39472863245](https://github.com/taowen/arlinux-xnu/actions/runs/39472863245),
source **5e1496532c5d589a77455fb86ebc2652154609aa**, captured the real
__BKSDisplayGetMainScreenInfo client. Receipt
**e86f9b7aca08966eaf89975d3b4840c39be06611b76ec29697093a777f2a68ba** and
retained text hashes were verified. MIG 6001009 has a 24-byte request and a
60-byte normal reply: floats at 36/40/44/48 and Boolean bytes in 32-bit slots at
52/56. The client's 68-byte receive allocation includes trailer space. Its
wrapper uses either Boolean to allow otherwise-invalid empty bounds.

Source **692b4eb5797880af1b882989c9b10f386090b19c** serves that exact layout
with actual Wayland geometry, scale 1 and normal rotation. The configured
nonempty display enables neither empty-bounds policy. The latest verified run
records request 6001009, reply length 60, real dimensions 1024 by 600 and actual
Mach return 0; the original BackBoard assertion is gone. Unknown messages still
return MIG_BAD_ID. Neither dimensions nor successful IPC certify a useful frame.

Source **eff52e6d52b3cbfbaa614eb0fd65a780f6cf70d5** accepts the optional
adaptive-spin wait flag while preserving actual futex comparison, parking,
wakeup, timeout and remaining-waiter accounting. With single-CPU guest affinity,
a different lock owner cannot also be on-core while the waiter executes, so
blocking directly follows XNU's fallback behavior.
Run [39474334338](https://github.com/taowen/arlinux-xnu/actions/runs/39474334338)
is the exact four-app regression; its runtime effects remain unverified.

Poll that run rather than restarting after an observation delay. Next follow
Counter's original failure toward visible content and real input. Storage, IPC,
identity, input and lifecycle effects remain required. No compilation, console
result or registration certifies the UIKit goal. History stays in Git and Actions.
