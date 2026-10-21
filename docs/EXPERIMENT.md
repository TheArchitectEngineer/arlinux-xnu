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

[Run 39690482740](https://github.com/taowen/arlinux-xnu/actions/runs/39690482740),
source **463dc608a75f8e61c42bfe4cbe28394577ced15d**, attempt 1, passes 17/20
journeys, including both original Calculator phone cases and Notes persistence
after the shared-storage changes. Receipt
**18a904b983a0b9bc651890d09e97a6b84c44aba28d255ede84c172fa99217ee5**
and all 373 files were verified. Journeys take 225.4 s including 33.7 s diagnosis.

- Web still times out at first DOM/pixels; earlier runs also failed external input.
- Share cannot show EchoExtension; payload/helper-lifetime acceptance is incomplete.
- Things input fails: archive.org port 443 times out through all retries. No
  Things process runs in this full collection; this is not its 120-second trial.

Dispatch-to-completion is 917 s. Original-input preparation accounts for roughly
548 s beyond actual journeys: curl gave each retry an independent long connect
wait. Downloads now have one 185-second outer budget, 60 seconds per route, and
explicit same-object mirrors obtained from Archive.org's item metadata. The
actual download URL is retained; the pinned bytes/SHA-256 still gate extraction.
This network failure is distinct from the original app's actual stall below.
No complete 20-case run has passed. The later writev/statfs changes need fresh
full regression. Android remains unverified.

## Things: original first UI, database initialization still incomplete

Things iPhone 3.17.1/build 31701001 and its bundled frameworks execute unchanged.
The former ThingsCommon trap at VM 0x19ff30 force-unwrapped a nil Foundation
group-container URL. The loader now parses original application-group membership
with libplist; a shared Linux directory becomes a declared writable root, and
the filesystem adapter returns its real URL. No Things identifier is hardcoded.

[Run 39689862812](https://github.com/taowen/arlinux-xnu/actions/runs/39689862812),
source **b444f95edf253fb3fbe6dfb9ded0de4a68c21ea6**, PID/start 5849/28795,
passes the real 375x667-point viewport and composes the original **Upgrading
Database** modal. Receipt
**c7a56cc0b4ed319872c38b1fc9adba499018c34c78dccd5217a0cd854c24c366**
and all 84 files were verified. It does not reach Inbox in 30 seconds.

[Run 39691084207](https://github.com/taowen/arlinux-xnu/actions/runs/39691084207),
source **74d2cf85728a951d627b22099451fd357abefada**, PID/start 5989/42062,
still fails Inbox after 120.051 seconds; all 22 later task steps are blocked.
Receipt **84b728e01f3700b1f1342bb7c784cc8d2af4a82549cb7a01973a8a82a20f51e8**
and all 84 files were verified. The screenshot remains the upgrade modal.
The saved main thread is in CoreGraphics/vImage composition and the other live
guest workers are in event waits; this does not establish a dispatch deadlock.

Real Linux writev now preserves previously lost CoreFoundation diagnostics.
The original stderr reports AppCenter SQLite open result 10 (I/O error) and
unsupported entitlement loading. The saved original SQLite call chain requests
unimplemented statfs64 (345). The current adapter now resolves this named path
through a real descriptor and reuses the existing fstatfs64 mount/space packing;
no successful database result is fabricated. Verification remains pending.
Repeated later mmap errors exhausted the ordinary startup trace budget, so the
existing mapper now retains eight actual failed mappings' arguments, paths and
caller frames separately. Frequency alone does not identify the cause.

The signed IPA has XML but no DER entitlement blob. Native iOS SecTask requests
DER without XML fallback; XML-to-DER conversion remains unsupported, explicitly
reported rather than silently claiming no grants. Group preferences/keychain,
DER-only group membership and last-member uninstall cleanup are also incomplete.
Exact package URL/version/hash and distribution limits remain in APPS.md and
apps/things/app.json. Create/edit/complete and data survival across normal
close/reopen are unverified. No fixture can substitute for those requirements.
