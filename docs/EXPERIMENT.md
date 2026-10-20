# Experiment contract

## Current handoff

The target is a modifiable Darwin/iOS compatibility environment on
Linux/Android: native ARM64, 16 KiB guest pages, one advertised/scheduled CPU
and non-arm64e code. Original XNU boot is a behavioral reference. Acceptance
still requires an actual iOS app's display, input, normal exit and relaunch,
followed by its actual ARLinux integration journey. None of those GUI/Android
requirements has passed on Linux.

Current application: **ARHello 1.0** (`io.arlinux.ARHello`), a programmatic
UIKit window, label and increment button. Source/TBD declarations are under
guests/ARHello; real framework implementations come from the pinned firmware.

Current integration attempt:
[39372733667](https://github.com/taowen/arlinux-xnu/actions/runs/39372733667),
source `3598d76f2462619abe5f032289a1498cb7f6d916`, dyld job `118464449133`.
Use `build.sh build --mode app --ipc darling` through the sole root entry.
The taowen/darling parent gitlink pins our community server fork and upstream
MIG/header dependencies; sources.lock.json records the community base.

The RPC service runs separately with `--rpc-only`: no second userland,
launchd or mount setup. Its ARM64 XNU configuration is guest shift 14, with
one CPU; this runner's Linux pages are 4096 bytes. Backend memory hooks still
count host pages. The native generated RPC client uses raw Linux syscalls,
actual per-thread checkin/checkout and authorization to the exact server PID.

**Two genuine dyld/libSystem console journeys pass through Darling**, with
malloc, a real pthread create/join (value 37, shared memory 42), input counter 1,
normal return and independent relaunch. Self ports and Mach port/message traps
use Darling's real namespace. Current-task VM, clocks and other native BSD
services remain Linux-backed. General S2C memory/OOL behavior is not validated.

The same app now obtains real bootstrap/display send rights and the native
service thread handles actual Mach requests through a receive-right port set.
BackBoard screen info succeeds (1024x640). QuartzCore parses the OOL display
list and requests display properties (40232), using a real 1024x640@60Hz mode.
Synchronous S2C anonymous mapping callbacks allocate actual 16 KiB-aligned low
addresses (observed 0x301754000 and 0x302990000 for 4096-byte host requests).
The server copies the real OOL bytes into those mappings. File/fixed/executable
S2C mappings and asynchronous signal-driven callbacks remain incomplete.

Mach-port kqchannels now attach and deliver actual data. One observed receive
has status 0, 112 bytes at 0x302a1bf90 and 0x3910 bytes remaining in the caller's
stack data area. The upstream filter had first panicked on missing locked /
suppressed knote scan state (run 39369011861); a real cooperative processing
mutex and scoped scan state fix that assertion without removing it. Channel
read flags/residual extensions preserve caller-owned kevent data storage.

The missing-note workloop ADD|DELETE soft delete now follows XNU's actual
kevent_register semantics. Run 39371022211 passes the former libdispatch BRK
and registers ARHello.gsEvents. No dummy note is created and a plain missing
DELETE still fails. Current runs preserve that progress.

The next entitlement failure exposed unimplemented csops. The loader now
records actual signature metadata; unsigned ARHello returns unsigned status
and EINVAL for an absent entitlement blob, following XNU/Security semantics.
The server's real Linux credentials, PID and start-tick incarnation populate
its task audit token. Embedded unvalidated signatures remain unsupported.

**ARHello still fails**, ordinary status 133 at 0x1809bbf6c. Failure-only LLDB
now reaches a different throwing thread: BackBoardServices
`____getHIDEventSystemClient_block_invoke`, caller 0x18382b22c, constructs a
three-entry NSDictionary with a nil first value. UIKit's main thread is waiting
in `_classicMode`; the earlier entitlement throwing stack is absent from this
traced attempt. Identical exception text alone must not be treated as the same
cause. Actual caller disassembly is in the log; identify the nil dictionary
value and its producer before assuming a missing HID device or service.

The Wayland host child still supplies a real 1024x640@60Hz surface with a blank
wl_shm buffer: **no application frame or input journey is validated**. The
local IPC baseline remains an explicit comparison, never a fallback in the
Darling namespace. Its display protocol knowledge is shared by both producers.
The former 1260-byte XPC COPY_SEND/MAKE_SEND_ONCE request still needs an actual
peer/reply observation on this backend; enqueue or right acceptance alone is
not a preferences service response.

Next action: trace the actual BackBoard HID dictionary inputs and producer,
then continue this same app toward rendering/input/normal exit/relaunch and ARLinux
integration. Keep native TLS, real port ownership, mapped OOL storage and
direct Wayland. Extend S2C signals/FD mappings only from actual required flows.
## Exact evidence and inputs

Latest source/run above: server job `118463999136`, native job `118464300129`.

| Artifact | SHA-256 |
| --- | --- |
| Native ELF | `f0761d28054863d9bc0e68c7ad8ff1715273b1dcc6d54053b862b20cf2adeb7f` |
| Darling server ELF | `bd246b6bd09ea0867c6573c7a1dddf1cba5e3049538f0a3d506756bdb8beef0e` |
| Failed app evidence receipt | `d2a2ecf1095e98e5396f1900410a2fe0524b21d8e2a238bc89bf9cab34d07e85` |
| Native receipt | `54ef218f662d74c4eb5dc1b22973d5199370006341e965acb846d9f66fa97bfa` |
| Server receipt | `f46a1d64ca03fa5cea72969ed3684c4b1b8ec64d6e7ed0bd8e05c26fa60a724d` |

Firmware identity/URL/IPSW SHA-256 live in sources.lock.json. Selected firmware
is iPhone7,2 / iOS 12.5.7 / 16H81. Actual input/cache metadata and runtime hashes
are retained in each run's artifacts and receipts. Apple executables, firmware,
cache and cores remain transient on runners, never workstation artifacts.

| Input | SHA-256 |
| --- | --- |
| ARHello 1.0 original Mach-O | `4b346aa7dd998425990cb9ef282ddb7e5252b4d9d069b12c369d32d407f29b77` |
| Dynamic console Mach-O | `2b66ddec09f799b483a2e97b86b859ed5cc1e0474a9c6afb197dde17cf3327ca` |
| dyld 650.3.4 | `5c5563ec91c78cfe060cb500e3ca8d764350572f124f98144bac175aa25655e9` |
| ARM64 cache (1276837888 bytes) | `a60a0653eb730f73b5d1de61a97e86e5a241f7a4ff00c83659f9eefb965cac6f` |

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
