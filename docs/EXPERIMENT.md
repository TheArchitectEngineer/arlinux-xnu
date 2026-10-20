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

Run [39483936422](https://github.com/taowen/arlinux-xnu/actions/runs/39483936422),
source **89f45fa84fd18cfa97dc83f5bb9ec14ad3e03321**, selected Console, Counter,
Foundation and SwiftCards. Receipt SHA-256
**c4fb521cbef3f0f68eece9894587117f1fa95b18377ed16b2ca655d5618b01e5** and
relied-upon retained text hashes were verified. This is partial Linux coverage.

- Console passes actual allocation, computing pthread, external input, normal
  exit and an independent relaunch. Foundation passes Unicode JSON, Objective-C
  exception unwind, NSTimer computation and normal exit.
- Counter and Swift enter genuine UIApplicationMain but fault in dyld's
  Atlas::Image::ml(). No didFinishLaunching marker, useful composed pixels,
  external GUI interaction or normal GUI close/relaunch has passed.
- The original fault loads the Mach-O sizeofcmds field at 0x300758014 from an
  image address 0x300758000. Complete original-process kernel mappings show
  that address is unmapped. This proves an invalid address, not its cause or
  which image owns the stale metadata.
- Mach traps 76/77 now use the actual Darling port-type/notification APIs.
  Port-type succeeds with actual right masks, eliminating the earlier abort.
- The display service accepts the observed 40-byte message 40232 and sends the
  measured 476-byte complex reply with four descriptors, three strings and the
  decoded fixed tail. Geometry, mode and timing describe the actual Wayland
  surface; absent optional resources stay absent. Actual Mach receive succeeds;
  complete display-property consumption and rendering remain unproven.
- Anonymous mmap now accepts Darwin's VM-tag-only fd encoding and allocates
  real private memory. The previously rejected 0x68000000 request succeeds,
  but the identical Atlas fault remains. This was a real defect, not the
  established cause of the current failure.

## Current diagnostic handoff

Run [39484955937](https://github.com/taowen/arlinux-xnu/actions/runs/39484955937),
source **1d417ea8599ab66dd5094364cb3b18f6511d3212**, adds original-fault retention.
The harness records the fault as failure, then LLDB reads the stopped original
process. An exact-symbol match and writable-map bounds check permit a bounded
192-byte read of the Atlas Image object. Apple executable bytes stay runner-local.
Receipt SHA-256
**e310bc618ae73d0cde276f3af6bfae21252f8f338ac4627bf1a4ea339d36fc62**
and all 43 retained file hashes were verified. Console/Foundation passed; both
UIKit cases correctly failed at the original fault. LLDB collected the actual
Image object: its saved base and mapped pointer both equal 0x300758000. The
file record has a path pointer but no inode/device identity. Read that bounded
path and VM allocation/release ranges next; no mapping-lifetime cause is proven.

Apple dyld source explains the next question: an identity mapper dereferences
Image's saved rebasedAddress. Determine that object's file identity and loader
address from original-process metadata before changing mapping lifetime or
loader behavior. The fault-time maps alone cannot establish which allocation
or image became invalid.

The same-cache reader uses declared slide-v2 pointer metadata for constant
CFStrings, bounded decoded instructions and actual call/branch observations.
The display wire layout comes from the real client's validation and loads,
not a guessed receive-buffer size. Original evidence remains in Actions.

Earlier source e3d5b7d removed bootstrap lookup's in-app QuartzCore server-start
side effect and mixed forwarding path. Runs 39478502832 and 39478733527 still
failed. That cleanup is an ownership simplification, not a proven abort fix;
the later port-type repair has direct runtime evidence. Nearest symbols beside
stripped functions do not by themselves establish call ownership or causality.
The cfprefsd context assertion has not been disabled; its causal role is unproven.

Completion still requires visible Counter pixels, external click/state/pixel
change, normal close and fresh interactive relaunch, with Foundation and Swift
regressions. No compiler, registration, live process or IPC result certifies it.
