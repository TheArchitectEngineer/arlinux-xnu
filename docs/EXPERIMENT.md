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
[39384447686](https://github.com/taowen/arlinux-xnu/actions/runs/39384447686),
source `df458047907779d9a14cf64ef57fb2663b5d5854`, dyld job `118501354331`.
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

The libxpc async reply crash is fixed: its disconnect branch queries the reply
port's context. The message/continuation retained its context, but the native
bridge returned KERN_NOT_SUPPORTED for guard trap 41, leaving the actual port
context zero. The bridge now forwards guard/unguard to Darling's existing RPCs.
In run 39382175891's LLDB attempt, port 0x3303 is guarded with context 0x30022c820,
strict=false, result=0; the actual libxpc callback receives that same nonzero
second argument. No context table or guard success is fabricated.

**ARHello still fails startup.** The ordinary attempt exits with status 0
before ARHELLO_READY. Its actual exit stack includes AssertionServices at
0x1836bdc24, libxpc event handling and a dispatch worker. The LLDB attempt also
exits 0 without a window-ready callback; it no longer hits the former SIGSEGV.
The startup step now explicitly rejects a zero exit before the real app ready
callback. A prior green run (39379317212) was only such an early zero exit,
not a working app.

Run 39383448706 identifies the exit's actual connection: the creation of
`com.apple.assertiond.processinfoservice` at caller 0x1836bdb00 is followed by
handler installation on connection 0x30022e4f0 at 0x1836bdb44. That same
connection receives event 0x1b102de10, then its thread exits. The real exit
caller's code references "Exiting because the workspace server has disconnected."
and calls exit(0). FrontBoard systemappservices/workspace connections receive
the same error object earlier. The bootstrap endpoint 0x303 also receives four
232-byte XPC message-ID 0x10000000 requests; our current producer returns its
36-byte MIG_BAD_ID response. This establishes a bootstrap/XPC protocol gap,
not a working assertion service or proof of its required reply schema. Next:
inspect those actual requests and reuse community XPC encoding/bootstrap
dispatch before implementing the process-info service behavior. Do not bypass
the framework's exit or invent a successful connection.

The downloaded textual lldb.log SHA-256 matches its receipt:
`fdabf8ea0fa9f6cdba09fc6156163c7e3e5508a854429d9cb4ac56269d4e084a`.

The current run links the pinned unmodified community libspc codec in the
host-TLS service thread. Actual bootstrap requests decode as subsystem 3,
routine 804 (0x324), zero handle/lookup-handle. FrontBoard requests use type 7,
flags 0 and receive encoded 64-byte XPC error dictionaries (registry result
1102); assertiond processinfoservice uses type 1, flags 8 and remains
unsupported. Darling's bootstrap_priv.h defines flag 8 as privileged-server
lookup. The app still exits before ARHELLO_READY: no process-info service or
privileged provider is established. Complex requests are rejected because this
upstream codec ignores OOL descriptors; malformed inline requests retain its
fatal behavior. No new Mach namespace or rights table is introduced.
Current lldb.log SHA-256, verified against its receipt:
`ec5fc84f14afd5d8b7da3620287c2b593fc7614b34b52be2d9cd2a73c364e282`.

The Wayland host child still supplies a real 1024x640@60Hz surface with a blank
wl_shm buffer: **no application frame or input journey is validated**. The
local IPC baseline remains an explicit comparison, never a fallback in the
Darling namespace. Its display protocol knowledge is shared by both producers.
The former 1260-byte XPC COPY_SEND/MAKE_SEND_ONCE request still needs an actual
peer/reply observation on this backend; enqueue or right acceptance alone is
not a preferences service response.

Next action: trace the actual AssertionServices exit caller and its XPC event,
then continue this same app toward rendering/input/normal exit/relaunch and ARLinux
integration. Keep native TLS, real port ownership, mapped OOL storage and
direct Wayland. Extend S2C signals/FD mappings only from actual required flows.
## Exact evidence and inputs

Latest source/run above: server job `118493546558`, native job `118494035151`.

| Artifact | SHA-256 |
| --- | --- |
| Native ELF | `41ccbd921faee7548ef6c29719618f3002e476627bf1d2c240eb26a3b39ba303` |
| Darling server ELF | `29d0a91afd3e4d05e137911016f4d25f5ae3dd8daa69ebcfb226a9e04eceafbd` |
| Failed app evidence receipt | `bf32e3e1ea5f0f4ce69833b87a6cbfbb7a2fa036da2648c7460aaa732c820d75` |
| Native receipt | `3d4b442db427af90a6d1a5621e0dffd190bdbdfb79357d002620f72ae1215eb3` |
| Server receipt | `749f40259058e0232f18e1b90f8a1de50272d7e8417ce70cc34d58f044d32786` |

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
