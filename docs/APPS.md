# App-driven compatibility

The acceptance unit is a named, versioned, independently built app. Each directory
under apps owns app.json, its source or original input and journeys.json. The files declare
build inputs and user effects, never implementation-presence checklists. All
source-owned apps compile against Apple's actual SDK. The runtime combines selected
pinned Apple userland with production Linux-backed adapters and replacements.
apps/shared/Screen.h is fixture UI layout, never a runtime shim.

PureFrame (净影工坊) 1.2.3 is the next candidate after the Things Apple
comparison also remained white without observer injection (run 39799642134).
Its existing public decrypted cache was downloaded without login; the manifest
pins the archive and original executable hashes. Static input inspection found
thin ARM64, minimum iOS 16.4 and cryptid 0 in the main executable and five bundled
frameworks, including React/Hermes. This is not a simple UIKit-only app.
The first-screen/reopen journey dismisses the actual 知道了 introduction once,
then clicks the observed combined label 转码记录, 查看历史结果 and requires
the history page's 尚无转码记录 content, then normal close/relaunch and repeat.
It does not certify media import,
editing or export. Runner-side source
availability is also required; a workstation download is not a runner cache.
Run 39804175373 failed before app execution: the public cache endpoint returned
HTTP 403 to runner curl. That exact downloaded IPA is now stored under
apps/pureframe/input in Git, retaining the same archive/executable hashes.
An attempted Git LFS upload was rejected because the account's LFS budget was
exceeded; the 22.8 MB input therefore uses a normal binary Git object. No browser
downloader or transferred workstation session is required.
Run 39805815468 (a48ebda) prepared the exact repository input in 0.109 seconds
and exited/relaunched normally. Its original broad title check passed, but both
viewed screenshots still show the splash image. This is not interaction success.
The external observer now excludes AXWindow titles, and the journey requires
navigation into actual page content instead of matching the application name.
Run 39806436244 (8345518), receipt
95146c39860da3c05b4c1b45b46f3f219ce651ef5b478321b5f217253d52b616,
verifies 20 files. Its screenshot shows the real home page and welcome dialog;
the first navigation click was blocked by that undismissed introduction. The
updated journey dismisses the genuine notice through its observed AX rectangle.
Run 39807696309 (cd92177), receipt
5f0738fc732da527f55cbbd11dda12458851c3386f55d894c0a37375b4133993,
verifies 19 files. The notice closes and the full home page renders. The bounded
AX tree reaches its contents at depth 17 without truncation. Navigation had not
occurred: React combines the card title/subtitle into one accessible label,
so the previous exact title-only click did not match. The journey now uses the
observed label and requires a history-specific empty state, not the home's
different recent-results message.

The next acquired original-binary candidate is Things iPhone 3.17.1/build
31701001, bundle ID com.culturedcode.ThingsiPhone. Its community-decrypted IPA
is 52,104,885 bytes, SHA-256
**7e3d47be5a6e5bcc9a3f4972e452fd17c953e4cecaa11c2e9256212a46a203d8**,
from [the archived package](https://archive.org/download/thingsi-phone-3.17.1-le-ti/ThingsiPhone_3.17.1_LeTi.ipa).
The [community metadata cache](https://github.com/ipaarchive/ipaarchive.github.io/blob/main/data/url_cache/279.json.gz)
records SHA-1 5904cdb2ea7a4cca92e93fa138827d1f66eb677f, which matches the acquired
bytes. The actual main executable is thin ARM64/subtype 0, cryptid 0, minimum
iOS 12.1; its SHA-256 is
**cdfe781bd499abdae3a79d30cf634c70a70d3866b3505002185c19c57ec078d4**.
Those metadata checks are input inspection results on Windows. Linux execution
now displays the original database-upgrade UI but has not reached Inbox; see
EXPERIMENT.md for the exact run/receipt. The package is declared in apps/things/app.json and
its required offline task journey is unverified; initial UI targets must be
confirmed from the actual app. The decrypted distribution is not verified against a vendor-signed
original. The package remains outside Git and is not uploaded as evidence.

| App | Stack / actual behavior | Required end-to-end result |
| --- | --- | --- |
| Things iPhone (unverified) | Pinned community-decrypted 3.17.1 IPA and its bundled frameworks | Create/edit a task, normal close/reopen with saved text, complete it and verify Logbook after another relaunch |
| Calculator (Linux journey passed) | Apple's original binary/resources, resolved by bundle ID from the pinned firmware | Real 2 + 3 = 5; normal close/relaunch, clear then 7 + 8 = 15; no replacement app code |
| Console | C / real dyld, libSystem, malloc, pthread, stdio | Interactive increment, genuine return 0, independent relaunch |
| Foundation | Objective-C / Foundation collections, JSON, Unicode, exception unwind, timer/run loop | Computed result after real callbacks, console input and return 0 |
| Process | C / 16 KiB private/shared mmap, fork, Mach task identity/ports, wait | Parent retains private 11 and sees shared 42 from actual child; each task owns its audit PID and receive port; child exits 37, parent reaps/unmaps and exits normally; a separate command exports/maps named memory, verifies alias/fork writes after port release and normal exit |
| Counter | Traditional Objective-C / UIKit app delegate, label, system button | Real pointer -> Count 1, normal close, fresh Count 0 |
| Notes | Objective-C / text input, Foundation document URLs, atomic UTF-8 file write | Typed note saved, visible text survives close/relaunch |
| Workers | Objective-C / UIKit plus pthread, mmap and dispatch | Separate thread, memory and asynchronous queue journeys, results visible |
| AnimationProbe | Source-owned UIKit / timed UIView completion and explicit CAAnimation delegates | Two short independent click/repeat/close/relaunch journeys; result only from genuine finished callbacks after elapsed animation time |
| LayoutProbe | Source-owned UIKit app / external input requests public deferred layout | Only real layoutSubviews updates bounds/color/text after each click; normal close/relaunch and repeat; no forced layout in the harness |
| SchedulingProbe | Source-owned UIKit app / public libdispatch APIs | Independent blocking handoff, 64-item backlog and timer-to-socket-read journeys; each repeats twice, closes normally and repeats in a new process |
| Canvas | Objective-C / UIView drawRect, CoreGraphics colors and geometry | Real red pixels, click -> real green pixels, normal close |
| Network | Objective-C / NSURLSession, HTTP, JSON, main queue | Two genuine HTTP requests recorded across normal close/new-process relaunch, each with visible answer 42 |
| Web | Objective-C / WKWebView, WebKit helper/rendering, JavaScript, DOM | Actual DOM click changes WebCount 0 -> 1 in composed pixels |
| SwiftCards | Swift 5 language / UIKit, generics, closures, Objective-C action dispatch | Real Deal click computes 6; close/relaunch resets state |
| Share | Objective-C / UIActivityViewController, NSItemProvider, NSExtensionContext | App requests bundled iOS sharing extension; payload round trip, independent PID, retirement/restart |

Console programs are Darwin bottom-layer apps, not normal home-screen iOS apps.
Calculator now has independent iPhone 8 portrait (375x667 points at 2x) and
landscape (667x375 points at 2x) flows. The runner resizes its real nested
compositor to the corresponding pixel dimensions before app launch; each flow
must observe the expected UIKit viewport. Other journeys restore their normal
desktop geometry. Both phone profiles passed on Linux in run 39686613898. An Apple-host
reference must also satisfy its actual viewport step; merely recording a
requested profile does not certify that macOS applied it.
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

### Focused probe apps

Source-owned probes supplement original-binary acceptance; they cannot establish
that Calculator or Things works. Give each compatibility aspect its own
`apps/probe-<aspect>` app.json, source and journeys.json, discovered by the
existing app collection. Keep independent failure modes in short journeys and
split unrelated aspects into another app rather than growing one workbench.
Use only public iOS APIs to produce real effects, external input to start them,
and the existing read-only observer plus screenshots to verify visible results.
Logs localize actual submission, execution and delivery; they never replace
effects. No private runtime calls, mocked services or special production paths
for probe app identities. Build, selection, provenance and diagnostics use the
same entry and pipeline as every other app.

SchedulingProbe isolates pending-work progress from Things' database machinery.
Blocking runs two default-QoS semaphore waiters whose releases require another
global-queue task, followed by main-queue delivery. Backlog checks all 64 distinct
background-to-main deliveries, with bounded CPU work between submissions and
completion. Timer IO checks three actual ordered bytes produced by a dispatch
timer and received through a socket-read source. Event coalescing is allowed;
no exact timer latency or worker count is required. Every journey repeats twice
in each of two process generations, requiring changed visible results, normal
exit and fresh Ready state. Bounded waits report failure rather than inventing
missing callbacks. Passing these flows does not certify all workqueue semantics
or prove the cause of an original-app stall.

Workers owns three short journeys, not one combined pass. Its queue journey
requires two successive background-to-main deliveries (Queue 3 then Queue 6)
and normal exit in each of two fresh process generations; neither execution can
be retried or satisfied by the prior label. Each other app owns a
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

Process also offers an independent named-memory command journey: the real console
App exports a Mach memory entry, maps its alias, reads the nonzero initial data,
writes in both directions, drops the port, then forks a child that modifies the
shared alias. Parent readback, real child status, unmapping and normal interactive
exit are required. This strengthens coverage of actual backing/lifetime across
fork; it does not certify transferring the port to an unrelated task. Web remains
the original independent-helper/rendering consumer and its failure stays red.
