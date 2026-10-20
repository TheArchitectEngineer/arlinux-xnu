# Current evidence and goal

Run a basic real UIKit app on Linux ARM64 with iPhone 8/A11 iOS 16.7.16:
visible content, actual input, changed state and pixels, normal exit and fresh
relaunch. Console, Foundation and Swift/UIKit are independent regression layers.
No OCR, AI recognition, replacement framework or fabricated success is used.
No complete Linux GUI, third-party IPA or Android acceptance has passed.

## Fixed input and acceptance

sources.lock.json pins iPhone10,1, build 20H392, 6,035,280,776 bytes, SHA-256
601cb23e740dacf8b61271fe02ffd5b3c146b518abb5a6e6b286e5f0475fe43b.
Apple CDN metadata and the independent index matched; actual runner downloads,
BuildManifest and mounted system version also passed validation. Real OS and
Cryptex1,SystemOS volumes form the read-only filesystem. Apple executable data
stays runner-local; retained downloads are text, screenshots and owned products.

The collection has 11 source-owned apps, 13 short journeys and two cross-app
scenarios. The real SDK builds C/Objective-C/Swift for iOS 16. Exact same-run
inputs use producer receipt SHA-256 and per-file hashes. Read-only UIKit/DOM
inspection never invokes actions; external input and composed pixels are required.

Apple reference run [39456469593](https://github.com/taowen/arlinux-xnu/actions/runs/39456469593),
source 31cdc44086cf2acb681b396ec1ba6b06830d84dd, passed Console, Counter,
Foundation and SwiftCards in 10 seconds (partial collection). Counter input
changed genuine UIKit text and the composed label-region hash; close/relaunch
returned to the original hash. Receipt
1dda013907147b5c219de1a85d209a14f640d5526e936106b6332e891a652e04 and relied-upon
screenshots/result hashes were verified. This is not Linux acceptance.

## Verified Linux progress and current blockers

The loader maps 43 UUID-declared cache members and all 2,705 indexed images,
including the header-declared MH_DYLINKER. It relocates each actual slide-v2
mapping and adapts code TLS accesses; the standalone interpreter is above the
canonical cache range. VM collisions still fail with NOREPLACE. XNU's legacy
and distinct immutable commpages contain the configured 16 KiB page size.

Run [39460893484](https://github.com/taowen/arlinux-xnu/actions/runs/39460893484),
source 584536d1dbe3f8d89c12aff58b32eb010b7ae268, verified the cache's separately
declared dynamic configuration mapping at 0x22acf4000. Apple dyld-1066.10's
SharedCacheRuntime.cpp creates the same mapping with actual cache device/inode
identity and read-only protection. Real dyld now transfers into its cache copy.
Receipt df9bd602d728f29c588bed5d7db485028a3bfe3ed2e43c1b892255442c2db0a3 and
retained text/JSON hashes were verified.

Run [39461736971](https://github.com/taowen/arlinux-xnu/actions/runs/39461736971),
source 84577f34baf9bb6e8fa4b982cf66371b504c5c0a, verified actual name2oid and
kern.bootargs reads. No Darwin boot flags were configured. Console/Foundation
then abort because opening / with O_DIRECTORY|O_NOFOLLOW_ANY is unsupported.
The restored split-cache/local-symbol reader resolves the original fault to
dyld's _abort_with_payload_wrapper_internal. Receipt
8212dd83c6a050ea6ae418c3ff55f824353526190d621f373b1b0a33323dbbd4 and retained
text/JSON hashes were verified. All four selected app cases still fail.

Run [39462328929](https://github.com/taowen/arlinux-xnu/actions/runs/39462328929),
source fd17d36d6b6fdceaad696cbfbbc31b2954123cc8, retained original libxpc
predicate context: __xpc_interface_routine now loads audit PID into w0 and uses
different PID/euid rejection branches, so the old instruction matcher rejects
the layout. Requester-echo checks follow it. Receipt
cb16408d798633b04979f1e18cbebff2e2c8cbabcf13735430e7c6f8dc91354c and retained
text/JSON hashes were verified. No predicate repair has passed yet.

## Current runner handoff

Source 49d7eaa00cba12c13b308c930b89d6320a852b84 fixes / to resolve to the guest
root descriptor's dot entry, and enforces O_NOFOLLOW_ANY with actual Linux
openat2 RESOLVE_NO_SYMLINKS. Run
[39462845920](https://github.com/taowen/arlinux-xnu/actions/runs/39462845920)
was authoritatively queued on the macOS app-build runner at last inspection.
Poll that exact run; do not restart merely because observation is delayed.

Source 4fa847f extends only the bounded predicate disassembly to include the
actual rejection blocks; that extension has not run yet. Next use actual branch
targets to adapt provider authentication while preserving real session PID, UID,
incarnation and requester-echo checks. Do not invent PID 1 or bypass identity.
Real UIKit startup, input/rendering and normal close remain unestablished.

See APPS.md, ARCHITECTURE.md and DIAGNOSTICS.md. Replace this handoff when new
evidence changes the next action; keep historical detail in Git and Actions.
