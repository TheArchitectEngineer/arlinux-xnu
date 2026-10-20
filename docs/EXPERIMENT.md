# Current evidence and next boundary

## Goal and fixed input

Run a basic UIKit app on Linux ARM64 with visible content, external input,
changed state and pixels, normal close and a fresh interactive relaunch.
Console, Foundation and Swift/UIKit are independent regression layers. No OCR,
AI recognition or fabricated success is used. Selective framework adaptation
and replacement are allowed under the [architecture](ARCHITECTURE.md).
No complete Linux GUI, third-party IPA or Android acceptance has passed.

sources.lock.json pins iPhone10,1 (iPhone 8/A11), iOS 16.7.16, build 20H392,
6,035,280,776 bytes, SHA-256
601cb23e740dacf8b61271fe02ffd5b3c146b518abb5a6e6b286e5f0475fe43b.
Real OS and Cryptex1,SystemOS volumes form the read-only filesystem. Apple
executable data stays runner-local. Compilation/execution use hosted runners
through build.sh. The collection has 11 source-owned apps, 13 short journeys
and two cross-app scenarios; a selected subset cannot certify the whole suite.

## Latest verified Linux result

Run [39501339522](https://github.com/taowen/arlinux-xnu/actions/runs/39501339522),
source **0172b0c377eff295613fd62ec7c56649470eb95b**, selected Console, Counter,
Foundation and SwiftCards. Receipt SHA-256
**6df270f5f13eb0fc620bf5f74ff47745e38f8ef29363e65b945d0ba2937f5b28**
and all 43 retained file hashes were verified. This is partial coverage and the
run fails GUI acceptance.

- Console passes allocation, a computing pthread, external input, normal exit
  and fresh relaunch. Foundation passes Unicode JSON, exception unwind,
  NSTimer computation and normal exit.
- Counter and Swift/UIKit stay alive but have no accepted UI state or composed
  pixels. Counter has no didFinishLaunching marker. The original main thread
  waits in GSEventRunModal / CFRunLoopRun. An installed main-loop observer and
  live process do not establish launch.
- Actual Mach/bootstrap traffic reaches com.apple.frontboard.systemappservices.
  The measured BSXPC channels request com.apple.uis.applicationSupportService
  with initializeClientWithParameters:completion: and
  com.apple.frontboard.workspace-service with handshakeWithRemnants:.
  Those service operations remain unsupported; channel IDs are dynamic.
- Earlier Atlas/dyld, trampoline loading and libdispatch synchronous-wake faults
  no longer stop this path. fsgetpath resolves real mapped file identities;
  CodeDirectory inspection supplies actual coverage without signature trust;
  repeated synchronous kevent wake preserves real owner updates and wakeups.
  Actual receipt/rights ownership remains in Darling/XNU, not a mock IPC path.

The version-2 platform API is compiled and exercised by the real injected
library. Counter reads the actual host window as 1024 x 600, 60000 mHz, configured,
active and backend-alive (flags 11), with its audited PID/incarnation. Explicit
host close propagation is implemented in the snapshot but has not been exercised
by a passing close journey. No UIKit transition is inferred from these facts.

Loaded metadata disproves the historical FBSUIApplicationWorkspace,
FBSWorkspaceClient and FBSSceneImpl interfaces: those classes are absent.
FBSScene instead declares
_initWithUpdater:identityToken:identity:specification:settings:initialClientSettings:.
UIApplication declares workspace:didCreateScene:withTransitionContext:completion:.
Focused run [39502256463](https://github.com/taowen/arlinux-xnu/actions/runs/39502256463),
source b7314f882a94a5cf996c4afdf5b66715d64888df, verified all 20 files and receipt
be629a20f35668b79fb1aae06a685ef995ce83f2d6ee5f4fc2e4b0f940e398fb.
Counter still fails GUI acceptance. The actual initializer calls updater's
callOutQueue and definition; it validates FBSSerialQueue, FBSSceneIdentityToken,
FBSSceneIdentity, FBSSceneSpecification and matching settings/client-settings
classes. The UIKit callback also validates the transition-context class.
FBSSceneSpecification exposes settingsClass, clientSettingsClass and
transitionContextClass. These contracts define the next host-backed scene
implementation; no scene construction or UIKit lifecycle delivery is implemented.

The subsequent window-state correction consults real Linux child status when
reading backend liveness, so an ungraceful backend exit cannot leave a stale
alive/active snapshot. Run [39503145757](https://github.com/taowen/arlinux-xnu/actions/runs/39503145757),
source 6b3bda4b0b612edeecab08b494c80d7b78c67fef, compiled and exercised the
initial snapshot; its receipt
96791ee1da826d551d92b184711c979df58f56ec322f93c564325f68ae67ed48 and all 20
files were verified. Abrupt backend termination itself remains untested.

## Direction and next implementation

Preserve original Apple application semantics where useful; replace platform
services with Linux-backed behavior. This changes the default next step from
reconstructing every FrontBoard wire operation to choosing a lifecycle boundary
in the existing injected platform adapter. No replacement is implemented or
validated merely by changing this plan.

1. Locate the framework-facing startup/scene entry points and their required
   state using the original app stacks and bounded same-cache diagnostics.
   Choose a shared lifecycle contract, rather than adding one app's bypass.
2. Implement real scene creation/configuration, activation and normal close
   against session-owned process/scene identity and host window state. Keep
   original UIKit controls/layout; allow replacing dependency-heavy internals.
3. Route actual Wayland input through the guest event path. Render genuine app
   content, then prove Counter click/state/pixel change and close/relaunch.
4. Regress Foundation and Swift/UIKit, then independent app and multi-process
   journeys. Remove superseded service paths only after the replacement works.

Wayland supplies surfaces and host input, not UIKit or a GPU implementation.
The current software renderer has not completed a UIKit journey on this input.
Do not call delegate/control actions from the harness, force readiness, or
report successful service operations without implementing their effects.

## Cleanup verified by this run

The legacy FrontBoard launch/scene serializer, workspace registration and
launch/scene reply state have been deleted, together with their build inputs.
Automatic BSXPC symbol enumeration and fixed protocol-disassembly requests were
removed from every failed-app diagnosis. General same-cache symbolication and
original-process stack collection remain available, including explicit targeted
lookups when a diagnosis needs them. The runtime no longer emits the old codec
class-export diagnostics. This cleanup does not implement host-backed lifecycle.

The runner compiled the modified runtime and executed the four selected
journeys after this cleanup. Console/Foundation retain their passing baseline;
Counter/Swift retain the unsupported lifecycle negotiation failure. This does
not validate GUI functionality or the unselected app collection.

## Evidence and reference limits

Apple reference run
[39456469593](https://github.com/taowen/arlinux-xnu/actions/runs/39456469593),
source 31cdc44086cf2acb681b396ec1ba6b06830d84dd, passed Console, Counter,
Foundation and SwiftCards (partial collection). Counter's genuine UIKit text
and composed label-region hash changed after external input; close/relaunch
restored the initial hash. The relied-upon receipt and file hashes were verified.
This is an Apple-host behavior reference, not Linux or Android acceptance.

Original-process stacks, bounded metadata/instruction diagnostics and actual
Mach traffic explain failures; they do not constitute working app effects.
Historical protocol investigations and receipts remain in Git/Actions instead
of an accumulating diary.
