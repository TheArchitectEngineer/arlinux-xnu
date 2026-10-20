# Experiment contract

## Current handoff

Target: a modifiable Darwin/iOS compatibility environment on Linux/Android,
native ARM64, 16 KiB guest pages, one advertised/scheduled CPU, non-arm64e.
Real display, input, normal exit and relaunch have not passed on Linux.
Acceptance includes two concurrently interactive applications in one shared
session, exit/relaunch without disrupting the other, and app-requested helper
or XPC-service launch, communication and cleanup. Repeat through ARLinux;
none of that complete multi-process/Android journey has passed.

Current app: **ARHello 1.0** (`io.arlinux.ARHello`), source in guests/ARHello,
using the real pinned UIKit/frameworks. Latest executed source:
`b1df2cdb5a39e2627f05dc2caae23f614a50b56d`,
[run 39403237349](https://github.com/taowen/arlinux-xnu/actions/runs/39403237349).
Use the sole build.sh entry, `build --mode app --ipc darling`.

Two genuine dyld/libSystem console journeys still pass: malloc, pthread join
(value 37/shared memory 42), real input, normal return and independent relaunch.
The independent session owns bootstrap, process-info and FrontBoard listeners;
Darling/XNU owns actual cross-process Mach rights and messages. Kernel audit
identities and Linux pidfds bind and retire connections. Submitted task rights
are capabilities, not independently verified task-to-PID mappings. No PID 1/root
identity is fabricated. System-app facilities and app-requested helper spawning
remain incomplete.

Launch events and responses reach real UIKit. Workspace message 5 reaches the
actual create-scene handler. Initial scene changes now decode correctly:
BSSettings.objects contains arrays [uint64 key, value, bool format]; class key 1
with true format uses CoreFoundation XPC conversion. LLDB observes both genuine
class-name strings being written by BSSettings _setObject:forSetting:, then both
scene settings diffs being applied. The prior `settings != nil` assertion is
passed in this traced attempt.

Current traced failure: NSInvalidArgumentException,
`*** -[NSCountedSet addObject:]: attempt to insert nil`.
The real caller obtains displayIdentity and immediately adds it to the set.
The payload currently supplies only initial settings classes, without display
configuration. Hypothesis: the scene needs a valid FBSDisplayConfiguration and
identity, consistent with the Wayland geometry. Decode their actual codecs and
scene-setting keys before changing the payload. Ordinary app exits 133 at
0x1808c4878 (libdispatch); this is a separate execution from LLDB's exception.
Neither attempt reaches ARHELLO_READY. No UIKit frame or interactive GUI passes.

Display codec evidence: [run 39404428141](https://github.com/taowen/arlinux-xnu/actions/runs/39404428141),
source `c2bd4dbbd673a9fad641bc72b6df67413b285d23`, confirms that scene
displayIdentity forwards to displayConfiguration.identity. Actual XPC codecs
expose identity/currentMode, pixelSize/bounds and other display fields. It still
throws the nil insertion exception; these are protocol observations, not a fix.
LLDB log SHA-256: `dcfdc6c10dd52b3f8fd7a175620addcf2dcbc5b84a0f9b5f4459a22713a14d6a` (receipt verified).

[Run 39405060101](https://github.com/taowen/arlinux-xnu/actions/runs/39405060101),
source `237e4e3cf6d78c709f7cd43168e42d8ce4fd8fe3`, captures both scene
setting blocks. Actual object dispatch subtracts 2 from the setting key; its
six signed offsets at 0x183841334 start with -0x304, reaching 0x183841030:
class validation followed by setDisplayConfiguration:. Thus object setting 2
is display configuration; no firmware-specific runtime address is introduced.
LLDB log SHA-256: `f8fff0cf693c33b163e9b4f6a4827b637f7f75460919397e7cd6f156791ebd24` (receipt verified).
It still fails on nil display identity; no GUI acceptance passes.

Exact pending run: [39405811356](https://github.com/taowen/arlinux-xnu/actions/runs/39405811356),
source `6155178ecca033e3f2c45904a896d44b94467bdf`, publishes actual Wayland
geometry/refresh/backend PID to the independent session using native Mach IPC.
The service uses kernel audit sender PID/incarnation, validates backend
parentage and retains both pidfds; either process exit retires the record.
Verify SESSION_DISPLAY_REGISTERED/RETIRED against each actual app/backend and
check that the session remains alive. The data is not yet encoded into initial
scene settings; the pending run is expected to retain the nil identity failure.
It also includes the corrected read-only switch table page resolver.
Continue this exact run; observation timeouts do not justify restarting it.
Next implement setting 2's actual FBSDisplayConfiguration/identity/mode codecs
using these live dimensions, then verify genuine UIKit scene progression.
Do not bypass nil assertions, invoke the app delegate directly or substitute a
host-drawn app.

Header-only MACH_NOTIFY_SEND_ONCE cancellation is handled only on outstanding
reply ports with the exact kernel notification and zero audit identity; it is
not a successful scene response. Client retirement is observed. Complete crash
isolation, actual helper launches, normal GUI exit/relaunch, two simultaneously
interactive apps and the ARLinux/Android journey remain acceptance requirements.

Useful research: [ios-rendering-docs](https://github.com/EthanArbuckle/ios-rendering-docs)
explains the scene/settings/context/render-server boundaries. Generated
[FBSSceneSettings](https://github.com/nst/iOS-Runtime-Headers/blob/master/PrivateFrameworks/FrontBoardServices.framework/FBSSceneSettings.h),
[FBSDisplayConfiguration](https://github.com/nst/iOS-Runtime-Headers/blob/master/PrivateFrameworks/FrontBoardServices.framework/FBSDisplayConfiguration.h)
and [FBSDisplayIdentity](https://github.com/nst/iOS-Runtime-Headers/blob/master/PrivateFrameworks/FrontBoardServices.framework/FBSDisplayIdentity.h)
headers identify interfaces, not the wire format or a ready implementation.

## Exact evidence and inputs

Latest completed app job: `118563466255`. Downloaded app/peer/LLDB/session textual
logs match the receipt SHA-256 entries. App evidence receipt SHA-256:
`23c8a6d157bf095b1526b6257d8f749c8a1ab4e29b3668c11afbf7a34cd5a9fb`.

| Artifact | SHA-256 |
| --- | --- |
| app.runtime.log | `1302dad9d05c41d8003a3b0456f80628fdac0c7c776967c5a993fe4b5c29665d` |
| lldb.log | `064f80055c63292bbd5c7976112adaa2a42017212f42c697746e85d537abefc1` |
| peer.runtime.log | `0baba9e7d46e819a64eb1124b76c95cc33a839a4c792a256ae84f4e7d1f8d95c` |
| session.log | `3a9348b54cd39b7adaf983b36afb026e9ecd802e325fe020fa93af1565c931c5` |
| session.stdout | `692f04815af26a5e7380ac1672d0ab44f6076eebbcee44cc480b538798034736` |

Firmware identity/URL/IPSW SHA-256 live in sources.lock.json: iPhone7,2 /
iOS 12.5.7 / 16H81. Apple executables, firmware, cache and cores remain
transient on runners, never workstation artifacts. ARHello Mach-O SHA-256:
`4b346aa7dd998425990cb9ef282ddb7e5252b4d9d069b12c369d32d407f29b77`.
Dependency commits and other input/output hashes belong to the exact run's
receipts. Keep trial history in Actions/Git rather than a growing diary.

## Evidence boundaries

| Experiment | Established at the linked source/run | Does not establish |
| --- | --- | --- |
| Darling/native dyld | [Two real libSystem console journeys](https://github.com/taowen/arlinux-xnu/actions/runs/39370264712) | UIKit GUI, general IPC/S2C/OOL, Android or all 16 KiB VM behavior |
| Local native runtime | [VM/COW/TLS/input/normal exit/restart](https://github.com/taowen/arlinux-xnu/actions/runs/39320163457) | The same lifecycle on the Darling backend or Android |
| PlayCover macOS reference | [Identical iOS input, real HID click, Count: 1 screenshot, normal exit and relaunch](https://github.com/taowen/arlinux-xnu/actions/runs/39338697656) | Linux/Android compatibility; macOS supplies its own runtime |
| Full-system XNU reference | [Exec, VM/COW, child exit and parent wait](https://github.com/taowen/arlinux-xnu/actions/runs/39311945725) | Userspace XNU, dyld, iOS frameworks or app compatibility |

These observations belong to their exact source revisions. No Android
app-sandbox journey or third-party iOS app has been validated. A green probe
never advances a later acceptance gate. Architecture/ABI reasoning belongs in
[ARCHITECTURE.md](ARCHITECTURE.md); diagnostic evidence selection belongs in
[DIAGNOSTICS.md](DIAGNOSTICS.md), not a duplicated run diary here.

## Working loop

1. Read this handoff and the exact failing run. State one hypothesis and the
   observable result that would confirm or reject it.
2. Commit source, dependency pins and workflow changes together; dispatch only
   relevant work through build.sh. Compilation/execution remain runner-only.
3. Track exact run ID and attempt. A local observation timeout is not grounds
   to restart a job; inspect its authoritative status first.
4. Use `build.sh report --run-id RUN_ID` for status, exceptions, original fault,
   receipts and executable hashes, with raw logs for causal details. LLDB status
   0 describes evidence collection, never an app pass. Keep ordinary and traced
   attempts distinct. Preserve failures and do not relax checks to turn green.
5. Replace this current handoff when the blocker changes; retain trial history
   in Actions/Git. Re-run affected upstream/downstream stages when interfaces
   change. Source pins do not freeze hosted images/packages or guarantee
   bit-for-bit binary reproducibility; retain actual environment versions.

## Evidence lifecycle

Each executed job records source/run/attempt/job identity, source lock, host
versions, outcome before upload and retained-file SHA-256 in receipt.json.
Actions conclusion remains authoritative for later upload/cancellation failures;
missing evidence never passes a gate. Download only needed text/screenshots or
open-source artifacts, never transient Apple executables/cores.

Artifacts expire after 30 days. Before relying on a milestone long-term,
preserve its exact artifact ZIPs, receipts and workflow logs in a versioned
external archive or immutable release. Local downloads are not a shared durable
archive. Until preservation exists, links may lose binaries; never silently
substitute a new build for expired evidence.
