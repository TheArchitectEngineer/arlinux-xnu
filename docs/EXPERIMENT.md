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
[39378607772](https://github.com/taowen/arlinux-xnu/actions/runs/39378607772),
source `bec89382a0a0f35c21c5332a293ad61bf6af432c`, dyld job `118482715181`.
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

The BackBoard nil dictionary value was `bundleID` (run 39373331310), obtained
from `mainBundle.bundleIdentifier` (run 39376968132). CoreFoundation's actual
directory open rejected O_NONBLOCK; after translating it, fstatfs64 failed with
ENOSYS (39377567538). Real Linux statfs/mount metadata and inode64 directory
conversion now let CoreFoundation enumerate the bundle and read its Info.plist
(0x28f bytes in run 39378124844). No identifier is injected into the framework.
The nil dictionary exception is gone in the current traced attempt.

The next failure was the native adapter's fixed eight-queue limit, returning
EMFILE during FrontBoard initialization (39378124844). Queue objects now use
real mmap/epoll allocations with stable published addresses and Linux resource
errors. They remain process-lifetime objects; full retirement is incomplete.

**ARHello still fails**, ordinary status 139 at 0x180b1d564. Failure-only LLDB
independently reaches that address in libxpc `__xpc_connection_call_reply_async`:
`ldr w0, [x1, #0xa8]` faults at 0xa8 with x1 zero. Its caller stack includes
libdispatch's async Mach reply invocation. Determine where the actual second
callback argument originates and is lost; do not skip the callback or presume
the missing value is a Mach trailer context without following the real ABI.

The Wayland host child still supplies a real 1024x640@60Hz surface with a blank
wl_shm buffer: **no application frame or input journey is validated**. The
local IPC baseline remains an explicit comparison, never a fallback in the
Darling namespace. Its display protocol knowledge is shared by both producers.
The former 1260-byte XPC COPY_SEND/MAKE_SEND_ONCE request still needs an actual
peer/reply observation on this backend; enqueue or right acceptance alone is
not a preferences service response.

Next action: trace the real libdispatch/libxpc asynchronous reply arguments,
then continue this same app toward rendering/input/normal exit/relaunch and ARLinux
integration. Keep native TLS, real port ownership, mapped OOL storage and
direct Wayland. Extend S2C signals/FD mappings only from actual required flows.
## Exact evidence and inputs

Latest source/run above: server job `118482268719`, native job `118482548736`.

| Artifact | SHA-256 |
| --- | --- |
| Native ELF | `a05e4e4d9118becc3b54bfacae45fb0f9496a00af40b67e2dd5c4dedfbbc4d0a` |
| Darling server ELF | `884c7b11593580041af983e42f40fc3ab260a571d05a719edbf3804139f99855` |
| Failed app evidence receipt | `81e08d55cc206e78b633fc41de60c5b5a51e26946130af285c6649ae75b91b43` |
| Native receipt | `e302fb35adaef9460808bfef7b9779876ff774572cd656f7f5a354b51442265d` |
| Server receipt | `837e02c2bd771abd28ac8a4ca26c6cde033025df1249b3d875e8f81c8b87e1f5` |

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
