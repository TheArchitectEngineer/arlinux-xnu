# Current evidence and next boundary

## Scope

Run 39784768561 (51643be), receipt
5c26eff84b5e2292b5fc434953430266db3c2f2e577879948b52c373d1c97d7c,
verifies 91 files. All later snapshots contain 137 actual layers with no
truncation: no task-row layer subtree is visible in the list; several controls
are transparent or offscreen. This does not prove the compositor lost populated
rows. Important correction: earlier successful MSAnimationManager block and
component didFinishUpdate observations are synchronous update execution, not
proof of timed CAAnimation delegate/UIView completion delivery. Add a narrow
probe-animation app with two independent journeys for those actual APIs. Only
real callbacks after an elapsed animation interval update the result; no timer
or harness callback supplies completion. Repeat, close/relaunch and repeat on
Linux and the same-input Apple reference before changing animation semantics.
These are complementary probes, not original Things acceptance.

Layout control run 39783907861 (467b30b) passes the same deferred-layout journey
on Linux and the Apple-host reference. Linux receipt
1f3027a3c240092ec70e7b1dbf5b053266dd5390d629b976a03cfc42c52476f2 verifies
29 files; Apple receipt 7c92ad4ba8560f11bdc093d8a4f2176f91a1776b1d68ce615a2998b435a86bcf
verifies 22. Actual external clicks log pending=1 followed by genuine layout
passes, changing the panel from 335 to 255 points on Linux and back, with visible
red/green pixels and new-process repetition. Final Linux 009-observe.png and
Apple 007-observe.png visibly show inset/green. Desktop sizes differ (Linux375,
Apple1025); this is behavior comparison, not pixel equality or Android acceptance.
The first run 39783522942 failed app linking because CoreGraphics was omitted;
no app ran there. The manifest dependency is corrected in 467b30b. The attempted
cancellation found that failed run already completed.

This rules out a generic deferred-UIView-layout failure for this probe, not every
Things layout path. Existing observations traverse UIViews; they do not establish
whether layer-only list content exists but fails composition. Next inspect a
bounded original CALayer tree (256 nodes/depth24) alongside the existing four
geometry samples, including delegate/content classes, masks, opacity, transforms
and actual bounds. No pixels or content payloads are read by this observer and
no layers are changed. Do not treat a data-binding cause as established merely
because UIView text inspection is empty. Things remains failed.

Run 39782339027 (40675be), receipt
ec30625d5e8e2fa8adae2999851c4894677ce5edf83997ba9026eb66c2b1daf5,
verifies 91 files. The controller's listAdapter/listView storage is nonzero in
samples 10/50/125, while navigationBar/headerDelegate remain zero. The original
THTaskListView's layoutSubviewsBlock storage is also nonzero. This does not prove
that adapter contents are valid or that a particular optional field is required.
Existing source-owned screens mostly assign child frames directly and do not
exercise deferred UIView layout. Add one small probe-layout journey: real external
click -> public setNeedsLayout -> only genuine layoutSubviews changes panel bounds,
color and result label; repeat, close/relaunch and repeat. Never call layoutIfNeeded
or a fixture action from the harness. Use the same input on the optional Apple-host
reference to assess this narrow platform question. This probe supplements Things;
its complete task/persistence journey remains red regardless of probe outcome.

Run 39780682935 (cc33e5c), receipt
95e879b2feb5f1276fd1e54ad2ee72bc65a747319d736a7a1bcd0ec43f0973e1,
verifies all 93 files. The actual component update reaches MSAnimationManager,
its supplied block at original 0x1000ae1e0 executes, animateUpdateWithContext:
enters/returns, and didFinishUpdate enters/returns. LLDB detaches successfully;
four mismatching nested return invocations are explicitly retained before the
matching observed return. No missing block/completion is proven. The same blank
journey remains failed. Stop repeating this passed callback chain.
Next inspect bounded storage presence of app-owned view fields in the existing
geometry samples, with no debugger. Runtime ivar offsets/instance sizes bound
the reads; at most sixteen fields and sixteen bytes per field are checked only
for all-zero storage, without values, pointer dereferences or app getters. Swift
fields with empty type encodings are never treated as Objective-C objects. This
checks whether the original list controller's adapter/navigation fields remain
unset after the successful parent update; it does not interpret arbitrary Swift
values or establish a platform cause.

Run 39779602603 (75a9acd), receipt
ffa820f2b3a5586033e8a23aa4fcf9bbeed56796c48f9587f3a13dd15cadbc73,
verifies 93 files. Original setDisplayScale: receives actual d0=2. After it,
canUpdate returns x0=1 and update enters/returns. No unhit points remain. Scale
is not the current blocker; the earlier before-setter false was initialization.
Static original update submits its own block through UIKitAdditions' UIView
MSAnimations category to QuartzAdditions MSAnimationManager.animate:. Next arm
that actual manager and supplied block only when the original component update
is entered, alongside animateUpdateWithContext:/didFinishUpdate entry/return.
This follows a real callback chain instead of assuming a missing animation tick.
No display scale or app state is overridden.

Run 39778756148 (a798ab3), receipt
3693717c61a753198c9789f7e001169c2790f82ad4c4bf73021fcf0d57ad14d3,
verifies 93 files. Initial canUpdate actually returns x0=0, followed later by a
real setDisplayScale: call/return. All public per-view trait displayScale values
are 2 in all four samples. Initial zero before a setter may be normal setup; no
platform scale repair is justified yet. The original setter compares d0 with its
stored scale, writes a changed value and sets needsUpdate. Next read its actual
d0 argument and arm canUpdate/update only after that setter is really entered.
This distinguishes a zero supplied input from lost subsequent update delivery.
Argument observations read at most eight actual ARM64 x0..x7/d0..d7 registers;
no arguments or results are changed.

Run 39772829516 (ec5f967), receipt
c4538ced8dcdefc378ea6ae8144839fae0c52002c7f2c5e8d6f8cd511cde51d3, verifies
93 files. All three component-update points are armed but unhit in the finite
20-second window; the app later remains blank. This is not proof of an animation
completion failure. Static original canUpdate calls displayScale, compares it
with zero and returns that comparison. Next observe this actual boolean return
and setDisplayScale: entry/return, and read per-view public trait displayScale in
the existing geometry samples. This checks the component's explicit precondition
before attributing failure to its update/animation implementation. The debugger
now records raw x0 on observed returns, without changing or interpreting it.

Next targeted original control flow: the pinned executable's
THTaskListSceneLayoutComponent.update constructs a real block and submits it to
an animate: API; that block calls animateUpdateWithContext:. didFinishUpdate
records actual stack/state snapshots. These are the app's explicit component
update cycle, distinct from UIView layoutSubviews. Observe entry/return of these
three methods with existing post-load LLDB; no new platform behavior or app
callbacks are supplied. The static metadata resolves text offsets 0xae184,
0xae220 and 0xb2424; only original method names are configured. Whether this
update cycle actually blocks list population remains unproven.

Latest read-only geometry evidence: run 39771576263, source
a4d181a4c63c352e33448ee308d185f6ee0b7735, receipt
4d6caaebb4d19775cbe9117ce840996ec0879f73bbd7489a277584267fd7b4fe;
all 91 files verify. Samples 0, 10, 50 and 125 contain no visible view layer with
needsLayout or needsDisplay set, including the eventual original task-list view.
Do not add forced layout/display calls on the assumption of an unserviced flag.
Read-only inspection of the retained fresh database (including its real WAL)
finds 33 TMTask rows, 2 TMMetaItem rows and 1 TMSettings row; Meta databaseVersion
is 21 and default-tag/orphan-heading setup is recorded. These are original
first-launch records, not externally created test tasks or persistence acceptance.
The original app creates content but does not present a populated list. Next
trace original data-to-list selection/binding after initialization, rather than
repeating scene activation, renderer or database-empty hypotheses. Unsupported
EVFILT_VNODE registrations also exist, mostly with generic preferences/resource
masks; their frequency alone does not tie them to this missing list effect.
No compatibility behavior was changed by these diagnostic runs.

Latest list diagnostic: run 39770870267 (1d8c6ee), receipt
c5ed4b0e334d4255f7131e2deb6d894b5461cd0b913f708834d0a299a901b841,
verifies all 93 files. Original main is reached and both named list breakpoints
are armed; neither layoutSubviews nor setLayoutSubviewsBlock: hits in the
20-second window, with successful detach. The app later remains on its blank
list. Unhit points in a finite debugger window do not prove missing layout.
The production software frame pump currently calls renderInContext: without
an explicit layout pass. Next collect real layer needsLayout/needsDisplay flags
in the existing four geometry samples, without startup debugger overhead, before
changing the frame pump. Remove the answered/ambiguous startup probes from the
manifest; the reusable bounded block observer remains available when needed.

Latest original callback evidence: run 39769922814, source 7cdec16036e94c68c2345ba8a5a6121429d128df,
receipt 9200bb36d726f0dfb226b7964333edb319220f3a5a761d6a813df8af2cc590c9;
all 93 files verify. Connect, foreground and active delegate methods each hit
and return on the real main thread; no unhit points or return mismatches remain,
and LLDB detaches successfully. This crosses the main-copy diagnostic boundary
but leaves the same Inbox failure. The real app receives its activation events;
do not add synthetic lifecycle callbacks. Next observe the original task-list
view's layout block registration, its supplied invoke function and genuine layout
entry/return. The existing collector reads only the original Blocks ABI invoke
pointer, validates readable block/executable code and arms an observation; it
never invokes the block or changes layout. These app-specific sites remain only
in the opt-in manifest, not production compatibility logic.

Latest diagnostic run: 39769180827, source c0e9674623c0a6fd9a0afbce33e67a3b9c005e75,
receipt ae273c48fd749dfd492834d0a70431e4ec87b02d90781222ae48cf12b3633159;
all 91 files verify. The actual original app launches, remains active and still
fails Inbox. The typed CADisplayLink factory observer is installed (no ABI
unavailable marker), but samples 0, 50, 100 and 250 all record created=0/live=0.
This supplies no evidence for a registered-but-not-firing display link; it does
not cover alternative private constructors. Do not implement synthetic refresh
ticks based on the blank screen. Next observe original scene-delegate connect/foreground/active entry and return
with the existing startup debugger after code copying. The source-owned metadata
resolver now accepts original Objective-C instance-method names; the Things
manifest names those three methods with observeReturn, without numeric sites.
No file uprobes are placed on the main image. Foreground-active/key-window metadata and
actual database/extension writes remain established; tasks remain unverified.

The first same-source run 39768611350 did not launch the original app: all IPA
routes exhausted their shared deadline. Receipt
bf212284e8ed0a8197a3dc9bf4cfd9c205a822ed4f1eed99b9911ddf4822b7f0 verifies
17 files. This was input unavailability, not a runtime or display-link result.
The retry above acquired the same pinned IPA in 2.055 seconds. Its initial report
API error was recovered by reporting that exact completed run, not redispatching.

Latest run 39768008670 (a405db3) verifies receipt
be350856e5b921acce9f42ac582abdbfd3a2a4e8ad97fe01a7dac5adbdfef251 and 91
files. Without main-image uprobes, Things remains alive but Inbox times out.
The public UIWindowScene has activationState=0 (foreground active), the real
THWindowSceneDelegate, application role and the observed window association.
The window is key, UIApplication active and protected data available. These
states do not prove every app delegate callback, but provide no support for an
inactive-scene fix. Next observe actual CADisplayLink creation/scheduling and
public timestamp progression using typed forwarding wrappers in the existing
opt-in observer. Never call a display-link target or inject missing ticks.
The original binary contains display-link callback references; that alone does
not establish causation. Full task and regression acceptance remains incomplete.

Diagnostic run 39767427316 (3596271), receipt
ef57637d8bb73ebcf9ff2d37dc4aee380d954cf696b73ccaba3f19bb1f95eceb, verifies
88 files but cannot diagnose the blank-screen cause: SIGTRAP is at the newly
probed original main-executable callback 0x10052c9f4. macho-image.h maps the
source file then copies its text to anonymous memory; this copies the uprobe
trap outside its kernel-owned mapping. Remove the main-image probes and reject
MH_EXECUTE in the collector before attaching any probe. No application runtime
repair is justified by this diagnostic interference. Next rerun only the same
case with the public connected-scene/key-window metadata, without code probes.

Latest diagnostic run: 39754726627, source fd207841a45208eea2bc7424db6e333c9592413a,
receipt ca1df07cd074946388b6e84aa841396e1591b0cca2804f00aaeaaf6d16af2861;
all 96 files verify. At samples 10, 50 and 125, original geometry remains stable:
375x667 window/root, no transform or mask, THTaskListView exists but contains only
its background; plus button is at y=667. The original scene controller replaced
the upgrade controller; no active presentation transition is reported. The prior
database worker/Swift work/block probes all return. These exclude neither absent
app initialization nor an unimplemented display-driven callback. No blank-screen
cause is proven. Next use public connected-scene activation metadata and replace
the now-answered database probes with three original THWindowSceneDelegate
callbacks (connect, foreground, active). Sites come from Objective-C method
metadata in the pinned executable, never production offsets. Observe callbacks;
do not invoke them. Task creation/edit/completion/relaunch remains unverified.

Ordinary run 39753836535 (4a3cc09), receipt
6bd92e358a3b3f906a1ed78990e07e6a398bd591c85bcc8da6b2e166e18d3f1e, verifies
91 files. Today Extension Data.json has 112 real bytes; the image/export aborts
are crossed. Main.sqlite is 196608 bytes with WAL, not task-persistence proof.

Current baseline: run 39739768119, source aeef517fe6f0c4f3c025c2558574f4a27ac6625b,
receipt dc8e20c3446e944bf1f7d56798291ee129287063f118413d5982851305df4682;
all 91 retained files verified. Deferred QoS-5 requests are actually admitted:
REQUEST_DEFERRED -> PENDING_STARTED -> original block/Swift closure/database
worker entry and return -> main-queue callback. The original task database now
exists (167936 bytes). This crosses the lost workqueue-request boundary but
still does not pass Inbox or the task/persistence journey.

The next original fault is libdispatch event_loop_poke at 0x18db51d28 after
EVFILT_WORKLOOP ADD|ENABLE / THREAD_REQUEST|UPDATE_QOS|IGNORE_ESTALE returns
Linux -EACCES. In the actual runtime implementation this comes from setpriority
on the existing workloop worker. Darwin QoS lowering had been mapped to Linux
nice lowering, which an unprivileged worker cannot subsequently reverse.
The candidate removes host renice from thread creation, guest QoS updates and
workloop updates together. It preserves validated guest priorities and QoS-first
pending admission, while Linux retains its inherited scheduling policy. This
is not full XNU priority inheritance or performance equivalence.

Run 39741993419 (3e39ff2) did not execute an app: firmware curl had reached
only about 20 percent after 523 seconds of the preparation step, and the run was
explicitly cancelled. Its curl survived Python termination and mutated the
download log during receipt/upload; local evidence verification correctly
rejected that artifact. Do not use it as app or QoS repair evidence. Download now
has a 240-second outer deadline and cancellation kills/reaps curl before log
publication; preparation has a six-minute step limit. No firmware mirror or
different binary is substituted.

Ordinary run 39743269876 (7fd49d5) verifies receipt
9c1b711f3465628bdffdc2c6328d4360297832f5e12d8439929626762b9b7fdd and all 89
files. The previous libdispatch workloop error is crossed, but the original main
thread now terminates with NSInternalInconsistencyException: INVocabulary requires
com.apple.developer.siri. The original stderr retains the throw stack. Inbox and
all task effects remain failed. Inspect original entitlements and the reusable
SecTask/csops boundary before any new Things diagnostic run. Static inspection
of the hash-matched original Things executable confirms its XML declares
com.apple.developer.siri=true; the same failed runtime records DER op 16 returning
-ENOTSUP. Host-side bounded XML-to-DER serialization preserves the unchanged
embedded dictionary, existing DER and csops size negotiation.

Run 39747191785 (c4a8d11) verifies receipt
d03e825a07851c05fc49cfc7a5e17cecfcffd053896ed934efe2edd46b170120 and all 89
files. The original Siri entitlement exception is crossed. Things now throws
NSInternalInconsistencyException: input data length 0 does not match 100x100
image dimensions, in its original QuartzAdditions image path. Immediately before
that, the original ImageIO PNG decoder's __ImageIO_Malloc requests mmap length
0xc000, protection 3, flags 0x41002 and allocation metadata 0x46000002. The runtime
rejects this with EINVAL: anonymous private allocation with VM_FLAGS_PURGABLE
and image-memory alias 70 was excluded from its tag-only mask. XNU kern_mman.c
explicitly accepts that allocation flag. Candidate aligns mmap with the existing
Mach VM allocator: private purgeable memory initially has actual nonvolatile,
zero-filled backing; volatile/discard control remains unsupported. Next run only
the ordinary Things journey, checking actual image creation and task effects.
Neither allocation success nor crossing this exception alone passes the journey.

Run 39748396327 (1ec04e2) verifies receipt
83f1b6b0722175baf18a20bdf3c529ba682659e0a7a82b39acd68170edd7192f and all 87
files. ImageIO's purgeable anonymous allocations now return real memory, the
100x100 exception disappears, and the process continues rendering. It then traps
in original Swift TodayExtensionExportComponent.swift:270: try! encounters Cocoa
512 / POSIX 78 while saving Today Extension Data.json in the declared group
container. The same thread records syscall 216 (open_dprotected_np) returning
ENOSYS immediately before the error. Candidate maps ordinary plaintext opens
through existing container-confined guest_openat, preserves real flags/mode/I/O
errors, validates creation classes and rejects raw encryption flags. No Apple
keybag policy is claimed. Next run only the original Things journey; no task
creation, edit, completion or persistence effect has yet passed.

User-requested resource work: cache only the two pinned OS volume DMGs on hosted
Actions, verifying locked byte sizes/SHA-256 before use and keeping app data
fresh. No full IPSW, boot/recovery payload, app state or test result is cached.
Cold run 39744136865 (313c63b) passes both original Calculator journeys; receipt
4a514214d4773752cf1e0f4c989133fecc30fb845526b46600574be944b2d4b6 and 53 files
verify. Portrait final 012-observe.png shows 15 after relaunch. Firmware download
16.101 s, preparation 42 s, cache save 29 s; dispatch-to-completion 174 s.
The Actions input cache is 5,651,344,265 compressed bytes.

Warm run 39744680047 on the same source passes workers--queue and scheduling
blocking/backlog, including repeated effects and new-process execution. Receipt
57abbb79a79b1586f2b864700f33944a7737608afb7800a952196bbb61fd946d and 70 files
verify. It records zero Apple download bytes, pinned-volume-cache validation
4.216 s and no repeat cache save. Restore took 57 s, toolchain 42 s, userland
preparation 23 s; overall 190 s with 34 s of journeys. Fresh mounts and app state
are retained. These are different journey selections: they do not prove an
end-to-end speedup. Warm restore + preparation (80 s) exceeds cold preparation
(42 s) plus the prior 16 s download, which had overlapped apt. This cache avoids
the observed pathological CDN delay; it is not yet a general resource reduction.
Do not add another large cache or repeat these passing runs just to seek a better
timing sample. Next runtime work is the original Things blank-screen boundary.
Android remains unverified. Earlier trace/admission history stays in Git/Actions.

Run original ARM64 iOS binaries using selected Apple frameworks and Linux-backed
platform services. Compilation/execution run only on hosted runners via build.sh.
The collection currently has 14 source-owned apps, original Calculator and Things, 23 app
journeys and three cross-app scenarios. Android execution is unverified.

The fixed input is iPhone10,1 (iPhone 8/A11), iOS 16.7.16 / 20H392,
6,035,280,776 bytes, firmware SHA-256
**601cb23e740dacf8b61271fe02ffd5b3c146b518abb5a6e6b286e5f0475fe43b**.
Apple binaries/resources remain runner-local; retained evidence contains metadata,
logs and screenshots. Actual input declarations and hashes are in sources.lock.json
and each app.json. Historical experiments remain in Git and Actions.

## Original Calculator: phone-sized Linux journeys pass

[Run 39686613898](https://github.com/taowen/arlinux-xnu/actions/runs/39686613898),
source **d98e9025d00416a8199dcbb1711ac8abf283f0dc**, attempt 1, passes **2/2**
independent original-app journeys:

| Journey | Observed display | Actual PID/start ticks |
| --- | --- | --- |
| Portrait | UIKit 375x667 points, compositor 750x1334 pixels | 5836/11810 -> 5906/12252 |
| Landscape | UIKit 667x375 points, compositor 1334x750 pixels | 5988/12739 -> 6064/13206 |

Each flow observes the actual viewport, uses external pointer input for
**2 + 3 = 5**, closes with status 0, starts a new process, clears, computes
**7 + 8 = 15** and closes with status 0 again. Composed screenshots show the
portrait/basic and landscape/scientific layouts with separated keys. The
`011-observe.png` in each case retains the result 15. The surrounding black
area is the larger Xvfb desktop, not an enlarged logical phone screen.

Original input: private/var/staged_system_apps/Calculator.app, version 1.0.0,
build 1.0, thin ARM64/subtype 0, cryptid 0; executable SHA-256
**82dc9e13e2f215f1128c13521b9b5110418c566742a25e5a1f9500c5dbc74120**.
The executable and app resources are unchanged. Execution receipt
**60242dcd8e14144e688fb70a624d650b6350da592b6908a2dc1f5ec76558dcc4**
and all 56 files were verified against the producing job. Feedback was 126 s;
journeys 18.8 s, no failure diagnosis. This is a partial Linux result, not the
whole collection or exact rendering equivalence to a physical iPhone.

## Display boundary and Apple-host comparison

The earlier geometry run 39625694451 exposed a 1024x600-point scene on a
512x300-point UIScreen at scale 2. Scene construction confused buffer pixels
with UIKit points. The runtime now divides scene/input geometry by the real
screen scale and applies that scale during software composition. The harness
also maps observed points to actual desktop pixels. No Calculator view or
private app action is patched.

[Run 39685654769](https://github.com/taowen/arlinux-xnu/actions/runs/39685654769),
source **5b1d297de2174ac37fbe5c352f2ea93d3c323fba**, attempt 1, passes the
calculation/normal-close/relaunch flow on both Linux and native macOS using the
same original input. Only the reference copy undergoes pinned PlayCover
conversion/signing. Linux receipt
**019814b23da02356f7622c0bcbbc497d5e981f93d5fcd3622b49528e3b3bca46**
(35 files) and Apple receipt
**9dfb22559920359a44f8f4eb84006c7b27202ba6db9624d0a97b9f37ef45e62a**
(27 files) were verified. Its window geometries differ; it is behavioral
comparison, not a pixel-equivalence baseline. The new phone viewport gate has
not passed on macOS. An arbitrary desktop-shaped iPhone app cannot establish
that a display difference is a rendering bug.

## Latest complete collection: failed, 19/23

[Run 39709336319](https://github.com/taowen/arlinux-xnu/actions/runs/39709336319),
source **5ef9ba3c17ca07d968d8fef8e10585417d22d370**, attempt 1, passes 19/23
journeys. All 17 previously passing journeys remain passing, including both
original Calculator phone cases, Notes persistence, Network and the session
scenarios. Two new scheduling probe journeys pass; its timer/I/O journey fails.
Things, Web input and Share remain failed. This is not a full-suite pass.
Receipt **81f850f83ccfb300db90a7a99ce8b81510fc07fcbebd07a40876488ec6d91102**
and all 435 files verify. Dispatch-to-completion was 429 s; measured journeys
282.2 s including 41.6 s diagnosis. Android remains unverified.

## Focused scheduling probe: real failures, separate from original acceptance

The source-owned SchedulingProbe has three independent public-iOS-API journeys.
Each requires external pointer input, real result pixels, two successive runs,
normal close and the same operations in a fresh process. It uses the existing
build, selection, observer, screenshots and failure collector. See APPS.md for
the extension convention; no second probe framework or builder exists.

In the full run, blocking handoff and the 64-item backlog pass in 9.255 s and
8.835 s respectively. Timer IO fails in process 6792/startTicks 23378. The same
failure is isolated in [39702531395](https://github.com/taowen/arlinux-xnu/actions/runs/39702531395),
process 6174/startTicks 22878: the actual dispatch timer fires three times and
writes three bytes, but the read handler never runs. After five seconds the app
shows Timer IO: FAILED 1. Original kevent registration for descriptor 22 returns
ENOTSUP: filter -1 (READ), flags 0x385 (ADD, ENABLE, DISPATCH2, VANISHED), fflags 1
(NOTE_LOWAT). These arguments are rejected by the production adapter's explicit
subscription gates, before readiness can be delivered. This is an observed
compatibility gap, not proof of a thread-pool starvation root cause.

The first probe baseline [39702058928](https://github.com/taowen/arlinux-xnu/actions/runs/39702058928)
failed earlier at socketpair ENOSYS. Local socket pairs now create actual Linux
stream/datagram/seqpacket descriptors, preserve real errors and close both on
failed guest copyout. The stream path is exercised by the timer's real writes;
datagram/seqpacket behavior has not been independently accepted. Error evidence
now also retains kevent data/udata to identify the rejected threshold and
subscription. Read-source lifecycle and threshold support remain to be fixed;
the probe is deliberately still red.

The isolated parameter run [39703128074](https://github.com/taowen/arlinux-xnu/actions/runs/39703128074),
source f21bed8f652a7270aad5d8cdb69ff9fa1f872181, confirms that the READ subscription
requests NOTE_LOWAT with data=1. It fails identically in process 6111/startTicks
41468. Receipt **86c47b5456a3463c79584eb52b9421e267aea76ae377917f012399f8af14d28a**
and all 25 files verify. The producer records three real timer writes and no
read-handler delivery. This run changes diagnostics only, not subscription policy.

## Things: original database-upgrade UI, no task acceptance

The full run's Things process 7189/startTicks 38332 displays Upgrading Database
and fails Inbox after 30.085 s; all 22 later task steps are blocked. The original
IPA/version/hash remain fixed in APPS.md and apps/things/app.json. No original
task creation, editing, completion or persistence has passed.

Targeted [39699108324](https://github.com/taowen/arlinux-xnu/actions/runs/39699108324),
source 572734a512f71eeee0973cf5f118cc50974ed5a4, observed original main,
THCDatabaseComponent.startUpgrade and its supplied completion, followed by the
actual Swift fatal error at DatabaseUpgradeAppComponent.swift:137. The saved
Foundation caller is NSFileManager._URLForReplacingItemAtURL:error:, requesting
NSItemReplacementDirectory. Its NSFileCoordinator returned Cocoa error 512
without invoking the accessor. Foundation's filecoordinationd-crashed message
is a reported service failure; no such daemon process was observed launching.
Receipt **35d22eade8569ae16ea026f4ca51a19f64e3b3071fa7bf44d1da5b661a2d6eb0**
and all 85 files verify. The trace alters timing and cannot certify normal launch.

The shared replacement-directory adapter uses a real unique mkdtemp on the
target filesystem and preserves failures, forwarding other directory requests
to Foundation. Ordinary Things runs still stop at the upgrade UI. The opt-in
trace follows the original upgrade request, the next database-component update
and its actual same-thread return; it never invokes an application action.

Diagnostic [39708697684](https://github.com/taowen/arlinux-xnu/actions/runs/39708697684),
source 10c93e52dba8d716f2ae4cbf09c1d3776280dc97, observed that update and return,
actual replacement-directory creation, then an uncaught original SQLite error
4618 on a background worker. The original shared-memory-file initialization
called ftruncate(fd, 3), syscall 201, and received ENOSYS. SQLite identifies 4618
as [SQLITE_IOERR_SHMOPEN](https://www.sqlite.org/rescode.html#ioerr_shmopen).
This is an original-app storage failure, separate from the probe READ rejection.
The run's receipt **d8bfb66c1fdc8e59c21588b5c734f0db8f83ef4cb17249674157595326b0aef3**
and the four relevant stderr/runtime/startup-trace/symbol files individually
verify. Its whole archive does NOT verify: Actions omitted hidden temporary
database files that the receipt recorded. Result uploads now include hidden
files on both platforms; the incomplete archive is not accepted as a full receipt.

The runtime now forwards ARM64 ftruncate's descriptor and 64-bit length to the
real Linux syscall, preserving errors. Diagnostic [39709263488](https://github.com/taowen/arlinux-xnu/actions/runs/39709263488),
source 5ef9ba3c17ca07d968d8fef8e10585417d22d370, verifies receipt
**3bca860640e46f229005af93830fa729f87b5d5fc6d19819c95b6139d33b4e90** and 86 files.
The upgrade request/update/return again execute, but no database creation or
ftruncate occurs in this run; Inbox still fails. It does not validate the new
ftruncate path or prove that the ordinary stall shares the SQLite failure.
The later ordinary full run also creates no task database and does not exercise
ftruncate. The pinned ThingsCommon update first calls FoundationAdditions'
consumeTriggers(with:); its supplied closures decide whether later work is
queued. The initial reading of those stub calls as unconditional
runInNextUpdatePass submissions was incorrect. The lazy-bind table maps pointer
0x531f80 to consumeTriggers(with:), and 0x531fc8 to runInNextUpdatePass in this
fixed binary. These are static evidence addresses, never runtime patch offsets.
The candidate consumeTriggers trace was superseded before execution when the
next diagnostic exposed the concrete vendor-identifier fault described below.

Focused [39710756996](https://github.com/taowen/arlinux-xnu/actions/runs/39710756996),
source 1630a3731e85aff9fd09e703de9d45058768549d, PID 5854/startTicks 18860,
still fails Inbox. Receipt
**fc6640325c47d9c92da21d16a7d63e645c35cfdc8d8521b0f91ec29cc9b977c1**
and all 86 files verify. The exported runInNextUpdatePass entry is unhit in the
20-second trace; the expected callback submission has not been established.
The recorded update return matches thread/address but did not record its entry
stack pointer. The collector now requires that pointer as well to disambiguate
nested invocations before another return observation is used to exclude a stall.
This diagnostic correction has not yet been runner-verified. The focused run
took 211 seconds, versus 429 for the full collection; its 64.3-second journey
includes 34.1 seconds of diagnosis, and all 170 Linux compile calls hit ccache.
It does not validate ftruncate, original task behavior or Android execution.

Diagnostic [39711398117](https://github.com/taowen/arlinux-xnu/actions/runs/39711398117),
source 68de123256efcb8395484adb72c7dbff2bd04440, PID 5852/startTicks 55981,
does reach the actual SQLite shared-memory initialization: syscall 201 returns
0 after opening main.sqlite-shm. The original upgrade completion and a later
queued callback execute. Return observations match thread, LR and entry SP,
with zero invocation mismatches. Receipt
**ddd324795ec55b27bf4a2ce601d3b5c87d85387df7e35b0938caa88e15bfff7a**
and all 89 files verify. The final shared container holds a 167,936-byte
Things Database.thingsdatabase/main.sqlite with original Meta, TMTask, TMArea,
TMTag and other actual tables; its retained DB/WAL/SHM hashes still match the
receipt. This is timing-sensitive traced initialization and on-disk storage,
not ordinary launch acceptance or task persistence across app relaunch. The run
took 160 s.

The next real fault is the original main-thread Swift BRK at 0x1001c9afc.
Its branch at 0x1001c80d4 follows a nil UIDevice.currentDevice.identifierForVendor;
the optional UUID is then force-unwrapped. The pinned binary's selector refs
and UIDevice class binding identify that call. A Linux-backed vendor-identity
adapter now stores a random UUID under the explicitly supplied shared storage
root, isolated by the non-App-Store bundle-prefix vendor. Creation/readback use
real file locking, entropy and persistence, preserving failures. No fixed UUID
or app-specific return value is supplied. App Store vendor metadata and automatic
reset after the last vendor app is uninstalled remain unsupported; clearing the
shared development store resets it. This change still needs original-app runner
verification. The obsolete upgrade breakpoint declarations have been removed;
the next run uses ordinary execution of the same task journey.

Ordinary [39712359756](https://github.com/taowen/arlinux-xnu/actions/runs/39712359756),
source 6b3307c4121e23a07017412e7e0790e573daaaa5, PID 5999/startTicks 133032,
compiles both native and guest adapters but still stalls before Inbox. No vendor
getter or replacement-directory creation is observed, so the new identifier
behavior remains unverified. Receipt
**cef93d8c76404eb737e36b57cee6a172267eb7f3d9d2f3aee13538c2c677d045**
and all 84 files verify. Feedback was 193 s, journey 46.2 s. The earlier nil-ID
fault is a concrete separate gap; this run does not establish that fixing it
changes the ordinary stall. The next bounded workqueue observation records real
SHOULD_NARROW priority, counted workers and decision, without changing scheduling.
Its invocation counter is included in the existing process collector so an
unused syscall path can be ruled out before attempting a scheduling repair.

The same source c55f64044e7e9adf92ca967ee65759d9d69f8e79 was then measured in
ordinary [39713029819](https://github.com/taowen/arlinux-xnu/actions/runs/39713029819)
(PID 5744/startTicks 21449) and read-only-profile, no-startup-breakpoint
[39713335953](https://github.com/taowen/arlinux-xnu/actions/runs/39713335953)
(PID 5796/startTicks 22232). Both still fail Inbox; both retain 84 verified files.
Receipts are respectively
**507150c9f9fb4539b1e057f62308120635f54fb44805819fceb5e5cb30184da6** and
**75d5a822fa08f59a248cc5cb5a004abe60572d94596bc8341f7558afeced70cc**.
The actual SHOULD_NARROW invocation count is zero: the known policy mismatch
does not explain this observed stall. No new scheduling behavior was installed.
The no-breakpoint profile alone also does not establish upgrade completion.

The latter main-thread stack is inside native_ui_capture, which overwrote about
4 MB on every Things paint. This instrumentation cost is now removed from the
guest main loop. Actual composed journey screenshots remain unchanged; failure
collection takes one bounded, completed shared source buffer through the same
LLDB attachment. This reduces repeated diagnostic I/O without weakening effect
checks.

[39714180392](https://github.com/taowen/arlinux-xnu/actions/runs/39714180392),
source 6cdc8629fe7196eb7c1e298eeae265f6a13b74c1, PID 5870/startTicks 20126,
verifies the new capture path: original source buffer sequence 495, slot 1,
750x1334 pixels, 4,002,000 bytes, captured once from the stopped process and
converted to its actual PNG. The source image was visually inspected. The
composed failure screenshot remains SHA-256
4083cd047177e692e30fa99c1bdaaab832d43c24d57c3c00e77dbdcf94fbe8a3,
matching the previous stall. Receipt
**a7f51e7621bf4783bd65120015d93d5bb5bb1ae0a9613ba7c27a1dc7236f84cb**
and all 84 files verify. The previous profile wrote 462 source frames, totalling
1,848,924,000 payload bytes; this run writes one source image after failure and
no per-frame diagnostic file. This is an I/O reduction, not a measured reduction
in runner minutes: feedback was 179 s versus 165 s, and the original app still
fails Inbox after 30 s. SHOULD_NARROW remains unused. The vendor getter and task
database initialization have not been reached in this no-startup-breakpoint run.

Kernel [39716512266](https://github.com/taowen/arlinux-xnu/actions/runs/39716512266),
source ae8f0817e9d8590932ba81835172af74af996491, PID 5848/startTicks 50145,
validates observation without a debugger entry stop. Its private trace records
66/66 entries, no buffer overflow, all requested registers available and clean
event removal before the failure LLDB attachment. Receipt
**0c0294b76da2d8a7b988284c4bd9ff1a7aa76d88ea67512ef959d8d8fb273028**
and all 89 files verify. Feedback was 185 s; Inbox still fails.

The original startUpgrade executes once. Its actual receiver 0x30478b9a0 enters
update during the request, calls consumeTriggers four times, returns, and updates
again about 81 ms later. There are four update calls overall, including an
earlier different receiver; thread/address alone cannot equate those instances.
All observed entries have returns. The remaining stall is after request delivery
and synchronous update, not a request that never arrived. The four supplied
predicate code pointers were recorded by the actual trigger consumer; the next
bounded observation follows their real entry/return on the same exact image.
This does not yet establish a background submission or fix scheduling.

Predicate [39717413546](https://github.com/taowen/arlinux-xnu/actions/runs/39717413546),
source 26406591167035f13ac785d2c1345d3e8a2d8c18, PID 5870/startTicks 18521,
records 70/70 entries without overflow and clean cleanup. Receipt
**c0ccc646247189f9c83080bf1b214493225e6f5a6959332ba4173807bf41a192**
and all 89 files verify. The first predicate returns false; the second executes
once and returns true within the requested receiver's consumeTriggers call.
The third/fourth predicates are not invoked on that consumed event. Inbox still
fails; feedback was 206 s. Static binding inspection identifies NSProcessInfo
beginBackgroundTask followed by the original global DispatchQueue.async call
in the accepted branch, not the initially guessed next-update-pass method.
The next focused trace replaces the unused predicates with that actual submit
stub and the block/Swift/database worker entries from its pinned disassembly.

The next scoped trace, [39723784073](https://github.com/taowen/arlinux-xnu/actions/runs/39723784073),
source 543ca643a76351fef0b800080a229ad747684f05, observes that accepted branch
calling the original `DispatchQueue.async` import stub once. Its exact copied
block, Swift closure and database worker are not entered during the observation
window; Inbox still fails. The process also made 69 actual
`__pthread_wqthread_exit` calls, while the callback traces show many kevent and
workloop deliveries. This proves ordinary pthread workqueue callbacks are
created and exit, but does not identify whether a worker was available for this
particular submitted block or whether libdispatch drained it. The next focused
trace replaces already-established startup/upgrade entries with the original
`__pthread_wqthread` entry and captures its actual flags and event count beside
the submitted block. It remains a diagnostic Linux run, not Things acceptance.

Two earlier kernel-setup attempts (39715347208 and 39716080783) produced no valid
function trace: Python append mode sought to the end of the tracefs command
file and received EINVAL before writing. The collector now issues a single
non-seeking command write, with parser errors and any unavailable fetch fields
retained explicitly. Those earlier attempts are not negative execution evidence.

## Reusable changes and outstanding boundary

XML shared-group membership resolves to actual Linux roots; vectored writes,
statfs64, writable-root access checks and POSIX/OFD record locks perform real
Linux operations. Guarded I/O uses actual descriptors without enforcing Apple's
guard policy. AppCenter creates its real 20,480-byte Logs.sqlite; that is an SDK
log database, not Things' task database. XML-to-DER conversion, group preferences
and keychain remain incomplete. Anonymous allocation publishes its actual next
placement after a fallback, instead of repeatedly hitting the fixed dyld region;
the overall Things startup failure persists.

The current workqueue narrowing code counts all live workers, including parked
event workers. XNU instead validates caller/QoS/overcommit and uses active/busy
accounting. This semantic mismatch is identified but not yet corrected; the two
passing probe flows do not certify it. The READ rejection is a real independent
gap, but neither original Things nor Web has demonstrated that same rejection.
Prioritize the original application's actual pending work and its producer /
consumer before extending a probe. A probe may encounter an earlier unrelated
missing API; fixing that does not establish progress on the original stall.
Do not accept flags while silently omitting their lifetime behavior or infer a
common cause from similar-looking stalls.

The read-only lifecycle run [39703669624](https://github.com/taowen/arlinux-xnu/actions/runs/39703669624),
source 60f1ea2361fb206a6159c5009fe5ae0cfd4fc6e3, still fails both selected journeys.
Receipt **d8b2f5159c61a1c9b2cfbf98a0c19a3842f7a8c49f782eda1694a0dff27f61f0**
and 105 files verify. Things' sampled main thread is rendering its original
upgrade UI, not visibly blocked in a read source. That single sample does not
prove rendering starvation. Web's real began event reaches UIKit with a view,
but the view is cleared during sendEvent before the ended event; DOM remains
WebCount: 0. This narrows input delivery, without establishing which recognizer
or event property causes the cancellation. The loaded UITouch has no
setGestureView: method; adding that selector is not a supported repair.
