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

## Latest complete collection before Things: failed, 17/19

[Run 39687118653](https://github.com/taowen/arlinux-xnu/actions/runs/39687118653),
source **d87ec6731af2fcc83e112625070329ae6b0a0d94**, attempt 1, passes 17/19
journeys, including both original Calculator phone cases. Receipt
**9903e0e46e913b55d905c8c0b8ccccf757aa3b4f4943e8097ba1a706a9286e37**
and all 362 files were verified. Feedback was 360 s; journeys 225.7 s including
34.3 s diagnosis. Canvas, Counter, Notes, Swift, Network, Workers, Console,
Foundation, Process and all three cross-app scenarios pass.

- Canvas's source-owned button now stays within the corrected viewport; its
  unchanged external-input red-to-green journey passes.
- Web times out at first DOM/pixels. Earlier runs reached DOM but failed external
  input to WebCount 1; startup is not deterministic.
- Share still cannot show EchoExtension. Real extension discovery/launch and
  payload/lifetime acceptance remain incomplete.

Things adds a twentieth case after this run. No current full 20-case result
has passed; selected original-app runs do not certify the collection.

## Next original app

Things iPhone 3.17.1/build 31701001 executes its original binary/frameworks in
[run 39687337351](https://github.com/taowen/arlinux-xnu/actions/runs/39687337351),
source **9247ddb92ac2d366d0cd9adc3d51bb4919b36e48**, attempt 1. Its first
viewport step fails, blocking task actions. Receipt
**4ab49a25d1e1dc98b8ab4c172a806f5804086a6d7cbac3bbd7915eb01c7da3a5**
and all 83 files were verified. Original PID/start ticks: 5862/37632.

The retained fault PC 0x3025ebf30 is ThingsCommon VM address 0x19ff30, a
`brk #1`. Original code calls NSFileManager's
`containerURLForSecurityApplicationGroupIdentifier:`; the saved x24 is its nil
return, and the branch force-unwraps that missing URL. Actual signature metadata
declares application groups. Unsupported csops queries are a dependency clue,
not proof that supplying metadata alone will implement shared storage.

The loader now retains bounded, original XML/DER entitlement blobs, and csops
exposes those bytes with XNU's short-buffer protocol and PID/audit-incarnation
checks. It asserts no Apple signature validation or platform trust. This change
requires an actual runner journey; group storage remains a separate Linux-backed
boundary, not an app-specific fabricated URL. Exact input metadata/distribution
limits are in APPS.md. Create/edit/complete/reopen remains unverified, as does
the Apple-host IPA path. Source-owned fixtures cannot substitute for this goal.
