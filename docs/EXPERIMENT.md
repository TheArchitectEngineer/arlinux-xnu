# Current evidence and next boundary

## Scope

Run original ARM64 iOS binaries using selected Apple frameworks and Linux-backed
platform services. Compilation/execution run only on hosted runners via build.sh.
The collection currently has 11 source-owned apps, original Calculator and Things, 17 app
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

## Latest complete collection: failed, 17/20

[Run 39694034223](https://github.com/taowen/arlinux-xnu/actions/runs/39694034223),
source **ce07f6dbc482955e0da13d3764e3fb2de453cf84**, attempt 1, passes 17/20
journeys after the shared-storage, vectored-write, statfs, guarded-I/O and record
lock changes. Both original Calculator phone cases, Notes persistence and all
previously passing fixture/session journeys remain passing. Receipt
**05dcfb438b7e498fa8aefb2685e13789b27552384ac1c059873857dc187a48ca**
and all 379 files were verified. Feedback is 490 s; journeys 341.5 s including
41.1 s diagnosis. Android remains unverified.

- Things actually runs, displays its upgrade UI, and fails Inbox after 120 s.
- Web reaches its original DOM/first pixels, but external input still fails
  WebCount 1. Earlier runs intermittently missed first paint; this does not prove
  that the new filesystem code repaired Web startup.
- Share still cannot show EchoExtension; payload/helper-lifetime acceptance fails.

No complete 20-case run has passed. Later changes are read-only diagnostics and
restore Things' ordinary 30-second first-Inbox wait after repeated 120-second
runs retained the same modal. Inbox/pixels and all task/relaunch steps remain
required. New observation success is never app success.

## Things: first UI works; task database initialization remains unexplained

The latest selected run is
[39694944246](https://github.com/taowen/arlinux-xnu/actions/runs/39694944246),
source **fd62d33aada116678a8bb98057e1c52614d9e20b**, PID/start 6014/91271.
It executes unchanged Things iPhone 3.17.1/build 31701001 and bundled frameworks,
passes the actual 375x667-point viewport and displays **Upgrading Database**,
but fails Inbox after 30.11 s; all 22 later task steps are blocked. Receipt
**9524db79443aee2a6bd37276038df5043c7f393b9dd22ac465669b3f434f61fd**
and all 84 files were verified. The actual UIKit sample reports active state,
protectedDataAvailable=true, and no ongoing presentation/dismissal or transition
coordinator on the root/child controllers. Those observations do not prove the
cause of initialization's failure or describe every earlier startup instant.

The saved main thread composes through CoreGraphics/vImage; live guest workers
are in event waits. The existing LLDB collector now retains bounded actual
workloop ownership and original IPA image metadata. Some workloop IDs have no
readable label and the list is truncated; no dispatch-deadlock verdict follows.
Static inspection locates the original THCDatabaseComponent.startUpgrade export,
NSOperationQueue imports and Swift task creation/switch imports. The next task is
to trace its actual startup/event/completion path, rather than fixing incidental
SDK messages as if they explained the persistent upgrade screen.

### Proven reusable boundary changes

The former ThingsCommon trap force-unwrapped a nil group-container URL. Original
XML membership now registers real shared Linux writable roots; Foundation returns
their actual URLs. No Things identifier is hardcoded. Linux writev preserves
previously lost framework diagnostics. Named statfs64 uses real mount metadata;
guarded file calls use Darling's real-I/O, unenforced-guard policy. access(W_OK)
checks actual writable-root permissions. POSIX/OFD lock records translate to real
Linux fcntl; the full run records successful OFD GETLK and SETLK operations.

The original AppCenter SQLite SDK now creates a real 20,480-byte Logs.sqlite and
its previous open-failure message disappears. This is the SDK database, not
Things' task database; the main UI does not advance. File-mapping failures retain
a separate eight-request argument/caller budget instead of being hidden by
successful startup mappings. Their frequency is not a root-cause verdict.

The identical IPA is obtained from declared official Archive.org storage mirrors
and verified against its pinned size/SHA-256 before extraction. The actual URL is
recorded. A 185-second total download budget replaces independent long retries
that previously delayed a full run by roughly 548 s. Timing can still vary with
the network; no binary is substituted or uploaded as evidence.

XML-to-DER entitlement conversion remains explicitly unsupported; SecTask still
reports it. Group preferences/keychain, DER-only membership and last-member
removal are incomplete. Exact input URL/version/hash and distribution limits
are in APPS.md and apps/things/app.json. Creating/editing/completing tasks and
saved data across normal close/reopen are unverified. No fixture substitutes
for these original-app requirements.
