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

## Verified Linux baseline

Run [39490481773](https://github.com/taowen/arlinux-xnu/actions/runs/39490481773),
source **6d4073c499db534a2f08d49e1fa1777c42aadf44**, selected Console, Counter,
Foundation and SwiftCards. Receipt SHA-256
**9f11b1cf8a9076b21dc557ecb12ed77d213009ffbc023b12d2aa658cdd79f505**
and all 43 retained file hashes were verified. This is partial Linux coverage.

- Console passes real allocation, computing pthread, external input, normal
  exit and a fresh relaunch. Foundation passes Unicode JSON, exception unwind,
  NSTimer computation and normal exit.
- Counter and Swift remain alive after UIApplicationMain but fail structured
  UI state / actual composed pixels. Counter has no didFinishLaunching marker.
  Its original main thread waits in GSEventRunModal / CFRunLoopRun. The genuine
  before-waiting observer is installed; this is not successful GUI startup.
- The original Atlas::Image::ml crash is repaired. Actual fsgetpath now recovers
  the cache's guest path from kernel mappings, re-stats device/inode and never
  fabricates paths for unknown identities. The stopped process's 672-byte
  compact snapshot records the real shared-cache base, 2705 bitmap bits and
  only two non-cache images. Both addresses match the real image-info table.
  Previously the cache was unrecognized: individual image deltas lost low bits
  in 16K units, saving 0x300758000 for a dylib actually mapped at 0x300984000.
- Mach port-type/notification traps use the real Darling core. The display
  service accepts measured message 40232 and returns the decoded complex reply
  with real Wayland geometry/mode/timing. Receipt and IPC success alone do not
  establish complete display-property consumption or visible UIKit frames.

## Current failed effect and handoff

The measured lookup uses packed bootstrap message 0x40000324 for
com.apple.frontboard.systemappservices: type 7, flags 512, handle and lookup-handle
zero. Unlike the legacy request, its CPX5 dictionary omits subsystem/routine.
Packed lookup is now supported as verified below. The initial empty-dictionary
request 0x40000388 remains unsupported; no launch success is inferred from it.

Run [39491337714](https://github.com/taowen/arlinux-xnu/actions/runs/39491337714),
source 5b8c67fe0b7d75e09341968af3f0b14a79b452ef, verified receipt
**154e6d4188118fdcdfa0f69d15aba4392cf920526c6a3bf5ba828fa8309cef51**
and all 43 retained files. The actual __xpc_pipe_pack_message selects packed
0x40000000 request IDs and uses 0x20000000 for replies extracted from a reply
dictionary. The private interface routine itself tail-branches to its next
named implementation; diagnostics now follow bounded named tail calls too.

Source d91d76f routes measured packed lookup 0x324 through existing real
service/rights ownership with verified reply framing and original requester
audit echo. Type-7/0x200 lookups are limited to actual session-provider endpoints;
arbitrary dynamic registrations and unknown flags remain unsupported.
Run [39492462364](https://github.com/taowen/arlinux-xnu/actions/runs/39492462364),
source **d91d76f0a2b449486de6b7141cd013feac3b5163**, verified receipt
**6cf3676cbb6ee380ef0b68a683a98616d576bc7629ced55dbd134124a14b0b6e**
and all 39 retained files. The actual client accepted the bootstrap reply,
checked in and exchanged two real systemappservices requests. Console and
Foundation still pass; UIKit fails and no GUI/lifecycle acceptance is claimed.

Those requests use a BSXPC connect envelope, including client context for
com.apple.uis.applicationSupportService. Existing plain-XPC service handling
returns unsupported; connection semantics are still to be implemented from the
actual decoder. A subsequent packed bootstrap request 0x400002c8 contains one
received send right and a dictionary with service-port. The generic dispatch
misclassifies that complex bootstrap message as an unsupported display request,
then exits the provider. The app also records a worker BRK at _start_wqthread
after __pthread_wqthread returns; do not assume causality from coincidence.

The current fix routes CPX5 bootstrap port descriptors to the actual XPC decoder,
sharing the existing port-only descriptor validation and retaining unsupported
routine failures. Received dictionary rights are released normally. It does not
invent service-port registration or BSXPC connect success. Bounded symbol-name
collection from actual BaseBoard/BoardServices and libxpc will locate the real
connect decoder/registration caller without repeated guessed selector lookups.
The harness reports a dead provider explicitly rather than an opaque /proc/exe
error. Run [39493707546](https://github.com/taowen/arlinux-xnu/actions/runs/39493707546),
source **ad25af35ddf22b0727f89566eaff7cc678414dfc**, is the pending exact four-app
regression. Poll it and continue from original process evidence. This subsequent
document-only correction does not change that run's source identity.

## Evidence limits

Same-cache diagnostics read declared slide metadata, bounded decoded
instructions, original heap metadata and actual Mach requests. They do not
execute injected guest calls or export Apple executable bytes. A collected stack,
received message or running process cannot replace app-visible effects.

Earlier removal of bootstrap lookup's in-app QuartzCore server-start side
effect simplified ownership but did not fix the abort. The later port-type
repair has actual runtime evidence. Nearest symbols beside stripped functions
are context, not proof of call ownership. The cfprefsd assertion is not disabled;
its causal role remains unproven. Historical runs and receipts remain in Git
and Actions rather than an accumulating experiment diary.

Completion still requires visible Counter pixels, external click/state/pixel
change, normal close and a fresh interactive relaunch, with Foundation and Swift
regressions. No complete Linux GUI or Android acceptance has passed.
