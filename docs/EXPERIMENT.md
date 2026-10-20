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

The obsolete iOS 12 runner 39454604689 was cancelled while queued when the input
changed. Its unexecuted new observation path is not a pass. The preceding legacy
source e99bf5e at run 39452517381 built all 11 apps, passed the Console lifecycle,
and failed the remaining 14 cases. Its SDK-compiled Process app exposed a genuine
mmap fd-width bug; narrowing the signed fd advanced from allocation failure to
the later fork/COW stage, which remains failed. Old results do not certify iOS 16.

The current cache mapper still assumes the old three-region/slide-v2 cache.
Modern split-cache mapping, dyld bootstrap, framework imports/private service
protocols and real input/normal close require actual iOS 16 evidence and fixes.
Do not guess offsets from version numbers or relax required effects. Record the
next exact run and its original-process failure before changing those mechanisms.

Read APPS.md for independent effects, ARCHITECTURE.md for ownership, and
DIAGNOSTICS.md for original-process evidence. No complete Linux GUI, third-party
IPA or Android acceptance has passed. Keep history in Git/Actions rather than
appending an unbounded experiment diary here.
