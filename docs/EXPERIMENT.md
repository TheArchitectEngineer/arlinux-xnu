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

## Latest complete collection: failed, 15/18

[Run 39685455873](https://github.com/taowen/arlinux-xnu/actions/runs/39685455873),
source **4ca70fd2e5bd5f3edc3d56d360d436371919d6b0**, attempt 1, passes 15/18
journeys, before adding the second phone case. Receipt
**bb2893724e1bd769e2ab76590c55e423a7d8eb4eb08b5fd520266023ec067688**
and all 308 files were verified. Feedback was 344 s; journeys 203.4 s including
42.5 s diagnosis. Counter, Notes, Swift, Network, Workers, Console, Foundation,
Process and all three cross-app scenarios pass.

- Canvas fails to click Paint: its fixed y=280 button extends past the corrected
  300-point desktop viewport. Its source-owned fixture now places that button
  within the actual bounds; the same red-to-green pointer journey must rerun.
- Web reaches DOM but external input still fails to produce WebCount 1. An earlier
  complete run also timed out before first paint; startup is not deterministic.
- Share still cannot show EchoExtension. Real extension discovery/launch and
  payload/lifetime acceptance remain incomplete.

Do not report the older 16/18 baseline as the current complete result. The new
phone cases and Canvas layout fix require a fresh full collection run.

## Next original app

Things iPhone 3.17.1/build 31701001 has been acquired and inspected, not executed.
Its exact package URL, hashes, architecture, encryption metadata and distribution
limits are recorded in APPS.md. Its pinned IPA is now declared in the same
runner input/launch path; actual create/edit/complete/reopen storage remains
unverified. Input preparation preserves the package contents, validates archive
safety and version/hash/ISA/encryption, and uses the shared bundle path. The
Apple-host IPA path is explicitly unvalidated; no reference success is inferred.
No source-owned fixture can substitute for this remaining goal requirement.
