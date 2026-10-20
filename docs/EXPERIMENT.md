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
[39367892856](https://github.com/taowen/arlinux-xnu/actions/runs/39367892856),
source `8d14f628b00a55381b500860a50d882995f3fd46`, dyld job `118449298555`.
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

**ARHello still fails**, ordinary status 133 at 0x1809bbf6c. Failure-only LLDB
records NSInternalInconsistencyException:
`backboardd isn't running -- or we couldn't talk to it -- result:10000003`.
Bootstrap lookup requests com.apple.backboard.display.services with port 0;
the old display registry is intentionally unavailable to Darling names. The
previous task-voucher error is gone after preserving the strict empty bank
voucher policy; it creates no port and does not emulate extra accounting keys.

The app also registers EVFILT_MACHPORT (-8) with flags 0x385 and fflags
0x0400080e. The native kqueue explicitly rejects this until actual Darling
kqchannels are connected. LLDB captures a real generated mach_msg_overwrite
RPC waiting with send size 40, receive size 256, options 3. An old local queue
snapshot cannot describe these server queues and is now suppressed.

The Wayland host child configures a real 1024x640@60Hz surface and submits a
blank wl_shm buffer. This is **not application output**. The earlier local IPC
baseline [39364012679](https://github.com/taowen/arlinux-xnu/actions/runs/39364012679)
establishes BackBoard/QuartzCore display-query wire formats, but rejects an
actual 1260-byte XPC COPY_SEND/MAKE_SEND_ONCE request. Reuse its display protocol
knowledge, not its incomplete port/right model. The local backend remains an
explicit comparison, not a fallback inside the Darling namespace.

Next action: connect the existing native bootstrap/display services to real
Darling receive/send rights and actual reply delivery, then its Mach-port
kqchannels and required S2C mappings. Keep native TLS and direct Wayland.
Verify actual send-once transfer, peer replies, ownership and wakeups in the
same app. Do not equate a queue enqueue or server build with app compatibility.

## Exact evidence and inputs

Latest source/run above: server job `118448809620`, native job `118449110599`.

| Artifact | SHA-256 |
| --- | --- |
| Native ELF | `0dd4b16fa22142e096f864bf2eaa8abb1f559fdf1d55a05de989e64527d87103` |
| Darling server ELF | `0dbea5d0d891a4ed0a6aef23cfcae4e5f3a20b52e4b850e7d23c1b431d56e195` |
| Failed app evidence receipt | `8ce356d60f7dea5c9621570d779baa28dd7b6a0b895f3b768fe6ce484bc0711f` |
| Native receipt | `f5fd2d1916ef55c0497886498192b09f9d5b31ddbf134dee337ea7b37c0fc789` |
| Server receipt | `1b4508ea06ebcec8d60851b097b3af573e192db0a0a393908182ab69458f9550` |

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
| Darling/native dyld | [Two real libSystem console journeys](https://github.com/taowen/arlinux-xnu/actions/runs/39367892856) | UIKit GUI, general IPC/S2C/OOL, Android or all 16 KiB VM behavior |
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
