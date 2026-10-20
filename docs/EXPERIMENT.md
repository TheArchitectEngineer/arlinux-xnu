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

Run [39480445811](https://github.com/taowen/arlinux-xnu/actions/runs/39480445811),
source **351fff6c0c55123e89a51a6bb7aae94a06441f53**, selected Console, Counter,
Foundation and SwiftCards. Result receipt SHA-256
**c9215eead1262826b74ba1427995c62a3106f6816f52f23714c4f7f2f6c77908** and
retained text hashes were verified. This is partial Linux coverage, not device
or UIKit GUI acceptance.

- Console's full interactive/relaunch journey continues to pass. Both actual
  generations allocate memory, create/join a computing pthread, accept input
  and exit normally with status 0.
- Foundation's Unicode JSON, exception unwind and NSTimer journey continues to
  pass. It computes 10, accepts q and exits normally with status 0.
- Counter and Swift no longer abort at the previous service-port check. Their
  real processes remain alive until the journey fails for missing structured
  UI state / actual composed pixels. Counter has entered UIApplicationMain,
  but its didFinishLaunching ready marker, visible content and interaction
  remain unestablished. Forced cleanup is not successful normal exit.
- The concrete repair is Mach trap 76 (port_type), delegated to the existing
  Darling core together with trap 77 (port notifications). The actual service
  port returns status 0 and its real right-type mask, rather than unsupported
  status 46. This enables the next real client request instead of hiding checks.
- Counter now sends display-info message 40232, length 40, to our actual display
  service. Observed inline arguments are NDR 0/1, display ID 1, final value 0.
  The old 36-byte/two-descriptor handler rejects this request with MIG_BAD_ID;
  no unimplemented properties are reported as successful.
- BackBoard's measured 60-byte screen reply and the 56-byte __CASGetDisplays
  OOL reply use actual Wayland geometry and display storage. Real client parsing
  and display-object construction have run; no useful UIKit frame has passed.

## Corrected diagnosis and source handoff

Source e3d5b7d removed bootstrap lookup's in-app QuartzCore server-start side
effect and mixed forwarding path. Run 39478502832 (receipt
**de7652ee47d600d60b738c53b563245fcd9c1b2fea431195747e44529a90a6dc**) and
run 39478733527 (receipt
**5aefa3f07ada978c6c173bf36852329faa3861601d2544276a6269c9239573bd**) still
failed at the earlier location. Those verified failures contradict treating that
cleanup as the proven cause/fix of the abort. The cleanup remains an ownership
simplification; the subsequent port-type repair has actual runtime evidence.
Nearest cache symbols, especially beside stripped functions, are context only.

The same-cache reader now decodes declared constant CFStrings through their
actual mapping's slide-v2 pointer metadata, and retains original ARM64 call
instructions and bounded branch-stub observations. Run 39479884455, source
cd353f28d86be001883296670050f4a9a70c69ef, receipt
**700c39d33d3738c8c98e93afa75cd325ac68df059c869f78206760175d809c63**, verified
that the display client uses kCADisplayName, kCADisplayDeviceName, kCADisplayId
and compares names with LCD / TVOut / External. No Apple binaries are exported;
only bounded decoded text is retained. All relied-upon receipt/text hashes were
verified. The previously observed cfprefsd.m:171 context-validity assertion has
not been disabled; its relation to concurrent startup failure remains unproven.

Source **90dd7d6b4d200b61fcea91e555e331473bb5b469** maps the actual Wayland
surface to the LCD main-display protocol role; its backend is still explicitly
Wayland. It also collects the measured Display::update decoder and consumer in
a bounded 4,096-byte diagnostic window even when the app remains alive.
Run [39481604703](https://github.com/taowen/arlinux-xnu/actions/runs/39481604703)
is its exact four-app regression; runtime effects remain unverified. Poll this
handle rather than restarting after an observation delay.

Next implement the observed current display-info reply: four descriptors
(three OOL and one port), three variable strings, fixed scalar tail, real screen
geometry/mode/timing and genuine absent optional resources. Use the actual client
validation and field consumption to define layout and meaning. Then follow the
original remaining startup/service/input failures. Completion still requires
visible Counter pixels, external click/state/pixel change, normal close and a
fresh interactive relaunch. No compiler, registration, live process or IPC result
certifies that goal. History remains in Git and Actions.
