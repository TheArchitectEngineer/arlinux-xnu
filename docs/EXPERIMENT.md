# Current evidence and next boundary

## Scope

Run original ARM64 iOS binaries using selected Apple frameworks and Linux-backed
platform services. Compilation/execution run only on hosted runners via build.sh.
The collection currently has 12 source-owned apps, original Calculator and Things, 20 app
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
ftruncate. Static inspection of the pinned ThingsCommon binary shows update
queuing closures through FoundationAdditions' runInNextUpdatePass. The next
targeted diagnostic observes that exported submission, its actual return and
the supplied callback's execution with the existing one-shot collector. The
original caller stack must identify whether this is the database component;
the first submission by a different component cannot establish its progress.

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
