# Current evidence and goal

Active goal: run a basic real UIKit app on Linux ARM64 using iPhone 8/A11 iOS
16.7.16 (20H392): visible content, actual input, updated state/pixels, normal exit
and fresh relaunch. Console, Foundation and Swift/UIKit are independent layers;
none alone certifies that GUI goal. No OCR or AI recognizer is used.

The fixed input is iPhone10,1, 6,035,280,776 bytes. Apple's actual CDN HEAD response
and the independent firmware index both publish SHA-256
601cb23e740dacf8b61271fe02ffd5b3c146b518abb5a6e6b286e5f0475fe43b.
URL, exact version/build/bytes/hash live in sources.lock.json. Runner preparation
must verify the downloaded bytes, BuildManifest version/device and actual mounted
OS/cache layout. Firmware and extracted Apple executable data stay runner-local.

The source-owned collection has 11 independently built apps, 13 short owned
journeys and two cross-app scenarios. C/Objective-C/Swift use Apple's real SDK.
Inputs are authenticated across jobs by the producing job's receipt hash plus
per-file SHA-256, exact source/run/attempt. App UI/accessibility or actual WebKit
DOM data is read-only evidence; real external input and composed pixel checks
are still required. No fixture action RPC or replacement framework is used.

## Exact current evidence

Run [39456469593](https://github.com/taowen/arlinux-xnu/actions/runs/39456469593),
source **31cdc44086cf2acb681b396ec1ba6b06830d84dd**:

- All 11 source-owned apps compile with the real SDK targeting iOS 16.
- Firmware bytes/hash, BuildManifest and actual system version passed. The OS and
  Cryptex1,SystemOS images mount as a genuine read-only union. Preparation took
  59 seconds. Actual ARM64 cache metadata contains 44 files including symbols;
  no SDK, firmware or Apple executable artifact was downloaded to the workstation.
- Apple-host reference passed Console, Counter, Foundation and SwiftCards in
  10 seconds, fullSuite=false. Real counter input changed both UIKit text and its
  composed label-region hash; normal exit/relaunch returned to the original hash.
  Receipt **1dda013907147b5c219de1a85d209a14f640d5526e936106b6332e891a652e04**
  matches result.json and all relied-upon screenshots. Count: 1 was visually read.
- Linux failed all four selected cases before guest entry, at the old cache table
  assumptions. Original stderr, cache mapping metadata and input manifests were
  verified against receipt
  **cf389a7f0d322c8bebf0dfb4c3502d73c7b569ddb8d071bc706df16f42e8eb17**.
  This is not a Linux app pass, nor complete collection or Android acceptance.

## Current Linux loader boundary

Source bb692655a0d5cc54aa055f74e84a99c6f6d8902f at run
[39457568952](https://github.com/taowen/arlinux-xnu/actions/runs/39457568952)
replaces single-file/three-region cache loading with primary-header membership,
UUID-validated files, each actual VM/slide mapping and mapped image linkedit.
The independent interpreter moves above the canonical cache/low heap ranges;
all VM collisions still fail with NOREPLACE. The original-process evidence
confirms mapping all 43 declared members and reaching real framework export
binding; this does not establish guest dyld entry or an app pass. New fields are
selected by format structure, never an iOS minor-version branch.

Run [39458778361](https://github.com/taowen/arlinux-xnu/actions/runs/39458778361),
source ace1f1c2feaa10538bac4a63eb42a293fda12fcd, identified the next rejection:
the indexed /usr/lib/dyld is a valid ARM64 MH_DYLINKER, not MH_DYLIB. Its actual
address is 0x1a460c000, with 15 load commands and 2,024 command bytes. Receipt
**498554f2d85ec2333e84920c2e539d78654c781354a40214193a3d0688c167b9** and the
original Console/Counter stderr and result hashes were verified. All four cases
failed before app entry.

Source bd1e3eac402c16d44f676dc2e4750ea887cf7364 accepts only the primary-header
declared MH_DYLINKER identity. Run
[39459382184](https://github.com/taowen/arlinux-xnu/actions/runs/39459382184)
verified all 2,705 images and reached real dyld execution for Console/Foundation.
They fault reading 0xfffff4037: the immutable commpage kernel-page-shift field
defined in XNU 8796.141.3 arm/cpu_capabilities.h, which the adapter did not map.
Counter/Swift stop earlier at the old libxpc trust-predicate layout adapter.
All four remain failed. Receipt
**8dcebed1f800b63eb62ad4f164797cc544c8f9b609f2f3aa3f0caf5e814069fb** and all
retained text/JSON hashes were verified.

Source a64d1cb477dcd68e3a6357fd50b7ddc9d3ffc9db maps XNU's distinct immutable
commpage with actual configured guest page sizes, preserving the legacy page.
Run [39460017720](https://github.com/taowen/arlinux-xnu/actions/runs/39460017720)
verified that dyld gets beyond the original commpage access and several further
real syscalls. Console/Foundation now fault at dyld PC 0x400001fa0 reading
0x22acf4000, immediately after the last declared cache mapping. This new access
needs its original data/format provenance diagnosed; do not map arbitrary missing
memory to hide the fault. UIKit's trust-predicate blocker remains unchanged.
Receipt **416da72b2f054483f9ae3e0b333e5098ff832a33c2c0af33b4846f916222951a**
and retained text/JSON hashes were verified. All four selected cases still fail.
The symbolicator also needs modern member-aware symbols.

Next: read the exact original-process failure, repair that owned mechanism and
rerun the basic app subset. Real dyld bootstrap, framework binding/private IPC,
input/rendering and normal close remain unestablished on iOS 16. Do not relax
required effects or restore retired probes to turn the result green.

Read APPS.md for independent effects, ARCHITECTURE.md for ownership, and
DIAGNOSTICS.md for original-process evidence. No complete Linux GUI, third-party
IPA or Android acceptance has passed. Keep history in Git/Actions rather than
appending an unbounded experiment diary here.
