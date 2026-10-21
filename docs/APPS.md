# App-driven compatibility

The acceptance unit is a named, versioned, independently built app. Each directory
under apps owns app.json, its real sources and journeys.json. The files declare
build inputs and user effects, never implementation-presence checklists. All
ordinary apps compile against Apple's actual SDK. The runtime combines selected
pinned Apple userland with production Linux-backed adapters and replacements.
apps/shared/Screen.h is fixture UI layout, never a runtime shim.

| App | Stack / actual behavior | Required end-to-end result |
| --- | --- | --- |
| Console | C / real dyld, libSystem, malloc, pthread, stdio | Interactive increment, genuine return 0, independent relaunch |
| Foundation | Objective-C / Foundation collections, JSON, Unicode, exception unwind, timer/run loop | Computed result after real callbacks, console input and return 0 |
| Process | C / 16 KiB mmap, fork/COW, wait | Parent retains 11 while actual child exits 37; return and unmap |
| Counter | Traditional Objective-C / UIKit app delegate, label, system button | Real pointer -> Count 1, normal close, fresh Count 0 |
| Notes | Objective-C / text input, Foundation document URLs, atomic UTF-8 file write | Typed note saved, visible text survives close/relaunch |
| Workers | Objective-C / UIKit plus pthread, mmap and dispatch | Separate thread, memory and asynchronous queue journeys, results visible |
| Canvas | Objective-C / UIView drawRect, CoreGraphics colors and geometry | Real red pixels, click -> real green pixels, normal close |
| Network | Objective-C / NSURLSession, HTTP, JSON, main queue | Genuine socket request logged by local HTTP peer plus visible answer 42 |
| Web | Objective-C / WKWebView, WebKit helper/rendering, JavaScript, DOM | Actual DOM click changes WebCount 0 -> 1 in composed pixels |
| SwiftCards | Swift 5 language / UIKit, generics, closures, Objective-C action dispatch | Real Deal click computes 6; close/relaunch resets state |
| Share | Objective-C / UIActivityViewController, NSItemProvider, NSExtensionContext | App requests bundled iOS sharing extension; payload round trip, independent PID, retirement/restart |

Console programs are Darwin bottom-layer apps, not normal home-screen iOS apps.
In particular fork is not an ordinary iOS App Store lifecycle requirement. They
do not certify UIKit. Sharing uses the actual public iOS extension APIs. The retired macOS-only
initWithServiceName/serviceListener fixture and its private declarations were
removed after the real iOS SDK rejected them; no availability bypass is used.

The selected firmware is iPhone 8/A11 iOS 16.7.16 (20H392), with non-arm64e
ARM64 binaries. Fixtures retain the ordinary UIApplicationDelegate lifecycle;
scene-based lifecycle and SwiftUI require separate app journeys before support
can be claimed. Swift/UIKit isolates language/runtime behavior from SwiftUI's
additional framework dependencies. Legacy Swift 4 packages may bundle another
runtime and remain a separate future input.

## Independent execution and shared mechanisms

Workers owns three short journeys, not one combined pass. Each other app owns a
single bounded user flow. apps/scenarios contains concurrent relaunch,
crash-isolation and focus-activation flows using Counter and Workers. The common runner owns process,
external desktop input, structured read-only UI state, screenshots, stdout for console apps, bounded diagnostics
and cleanup; it knows no app selectors, fake callbacks or app-specific outcomes.

No app is a prerequisite for another app's acceptance. Each journey owns a fresh
actual Mach session and new state directory. Its relaunch preserves that state.
A failing button cannot suppress Foundation, Swift, Web or later apps. Build
failures are retained and fail that app rather than disappearing from selection.
The all selection requires every case including the three session scenarios.
Subsets explicitly record fullSuite=false. Forced cleanup never proves close.

Linux input goes through Xvfb -> Weston X11 backend -> real Wayland seat.
The normal fullscreen window policy gives an actual configured output geometry;
focus is checked against real compositor activation events. The Apple-host
reference uses native window bounds and desktop events. The source-owned UIKit
apps publish read-only public view/accessibility properties and actual frames;
Web uses its genuine DOM and WebKit script-message transport. The observer never
calls actions, sets view state or implements a framework. No OCR, Vision model,
image recognition model or template matcher is used.

The focus-activation journey switches actual compositor focus between two app
processes and reads public UIApplication.applicationState plus real UIKit
did-become-active/will-resign-active notifications. An unfocused visible window
must become inactive and reactivate when focused again. This is passive fixture
observation; the runner never posts notifications or calls lifecycle/delegate
methods. The same journey requires real Count 1/2 pixels after reactivation and
both normal exits. State/notification checks alone cannot pass that journey.

Clicks use actual exported control frames transformed through actual host window
geometry. Acceptance combines the real widget/DOM value with unmodified composed
screenshots and simple pixel statistics. A changed label must change that label's
pixel-region hash; a missing/blank button cannot pass merely by being in the view
tree. These checks prove visible content/redraw and color effects, not recognition
of every rendered glyph. Canvas requires actual red/green pixels. Network requires
both the app-visible response and actual HTTP peer log. The Apple-host loopback
peer may require system network consent; this stays an explicit infrastructure
failure and is ordered last so it cannot obscure unrelated cases.
Console stdout is accepted only for console apps; every process exit is checked.
Original PID/start identity, binaries/input hashes, stdout/stderr/runtime streams,
screenshots, step status, bounded original-process stacks and cleanup are retained.
Blocked steps have not run. Failure diagnoses never replay a different app process.

## Extending actual compatibility

This collection targets current architecture risks, not every iOS API. Add a new
independent app when implementing a new behavior, with an observable effect:

| Next family | Required effect before claiming support |
| --- | --- |
| SQLite/Core Data | Edit, commit, close/reopen and observe the same records; isolated containers |
| Auto Layout/navigation/storyboards | Real bundled resources, navigation, constraints, scrolling and gesture effects |
| OpenGL ES 2 / SpriteKit | Animated scene, real GPU-backed presentation and touch changes, not callback counts |
| Audio | App-generated tone captured at actual output; delegate success alone is insufficient |
| TLS/keychain/network recovery | Controlled HTTPS peer, persisted secret, disconnect/reconnect and actual response |
| Older Swift / third-party IPA | Exact version/architecture/decryption/hash, its complete real use case, no per-title runtime branch |
| Android | Fresh ARLinux instance, real touchscreen/keyboard, close/relaunch and multi-app isolation |

Unsupported camera, push, purchases, Metal or modern SwiftUI are not implied by
any current app passing. Current results belong in EXPERIMENT.md, not a growing
list of optimistic feature claims. Build source and tooling paths are exclusively
in README and the single hosted workflow.
