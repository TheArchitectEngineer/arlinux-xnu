# Experiment contract

## Current handoff

The native-XNU/Unicorn-user hybrid is an **unvalidated architecture candidate**,
not an established implementation route. Further machine-layer expansion is
held behind an execution reference: run the original kernel and a real process
journey with the CPU/MMU/exception environment intact. This does not replace the
userspace-XNU goal, and a reference pass cannot satisfy that goal.

Latest completed usermode attempt:
[39315284120](https://github.com/taowen/arlinux-xnu/actions/runs/39315284120),
root `51e03e8618abf692d02014f62a5a3dba6859e673`,
XNU `50de8e61eee1a1785a4340a04910a6dfaf47d8f9` (see sources.lock.json for the authoritative pin).

- Kernel compiler job `118275536017` passed. Receipt SHA-256:
  `6501460f2dcc137c1c47f4e62a1fb5d756e301cf0b7dfceb0d2ed04e8feccf9f`.
- Native/engine compilation and the ABI collision audit passed. Executable
  linkage failed; examples are machine stack-boundary queries and original
  bank/persona/entitlement dependencies. VM receipt SHA-256:
  `4e5b5f65157a65f652b8493dd02c158fd2ca37c652ae2a1a76f219a23631dc33`.
- Engine SHA-256:
  `329f3fb3b771c3cb3777549b064e62e590e86d028a58af804f4f8e49c291a135`.

Text logs: `build/runs/39315284120`. No executable linked or kernel ran.
Earlier failures `39313910768` (unconditional TCP types without INET) and
`39314591126` (missing BSD pty.h generation and metadata tier configuration)
remain preserved in Git/Actions and local text logs. The fork fixes these
configuration boundaries without enabling fictitious hardware/network support.

Compiled, not executed: real memory-size globals, original BSD sizing/tunables
and buffer VM registration, BSD-generated device counts/ioconf, original memory
and terminal devices, specfs/devfs/mockfs and buffer I/O. mockfs typed allocation
and virtual-disk pager fixes come from pinned ios-sweet patches 0021/0024.
Original km/TTY/console ring code connects to a Linux headless terminal adapter;
Linux reads/writes use native TLS buffers rather than faultable kernel buffers.
The user pmap/ledger/copyio/machine return/virtual TLS/exception/timer code also
remains runtime-unvalidated. Compilation does not establish those contracts.

New, not yet built: the kernel build now produces `guests/init.S` with the
same pinned upstream Mach-O/signature writer as the working reference. The
native host reads opaque image bytes into owned zero-padded RAM-disk backing
before entering XNU. For IOKIT=0 and secure-root policy disabled, platform BSD
initialization registers original mdev0; root discovery returns its real device
number. Original mockfs, paging and Mach-O exec must perform loading. This is
not a native Mach-O loader or a Darwin syscall dispatcher, and supplies no IOKit
registry/framework services. No device registration or mount has executed yet.

The actual startup task passes this image as a declared, SHA-256 checked input.
PID 1 must remain alive: accept its controlled timeout only after two success heartbeats following
the completed memory/COW/exit/wait journey, with no explicit guest failure. An early
zero exit or quiet boot cannot pass. This acceptance path is not yet exercised.

New, not yet tested: consume the complete original BSD C/assembly source graph
from its generated Makefile instead of handpicking providers. Source-qualified
object names prevent basename overwrites. Kernel linkage now prefixes actual
kernel-defined symbols in object copies, retaining only the explicit xnu_host_
bridge ABI; compiler receipt inputs stay unchanged. The native collision audit
still runs afterwards. This generalizes the previously observed libm/kernel log
ABI failure without C macros or a manually maintained library-function list.
These changes do not implement missing machine services or establish boot.

`build.sh build --mode baseline` now builds the pinned ios-sweet kernel plus
`guests/init.S` on macOS, then boots that exact kernel in full-system QEMU on
Linux ARM64. Upstream mockfs serves the static Mach-O RAM disk as
`/sbin/launchd`; original XNU exec starts PID 1. The program opens the real
console, mmaps/touches two 16 KiB pages, forks, modifies/frees the child's
mapping, exits 37, checks parent wait4 status and COW isolation, frees the
parent mapping and stays alive. It uses no host Darwin syscall shim.

The reference must show the completed journey in the decoded guest console;
compilation, a quiet screen or `no_panic_observed` alone is insufficient.
Capture registers, framebuffer, exceptions, exact input hashes and receipts
on failure too. No proprietary assets are needed for this first journey.

Latest reference execution:
[39311945725](https://github.com/taowen/arlinux-xnu/actions/runs/39311945725),
root `9ef397e1cd7d26c496b15cb0ada18d8badbfa6af`. Both compiler and actual boot
jobs succeeded. The original XNU loader executed PID 1; guest output shows
`child freed memory, exit 37` and `XNU JOURNEY PASS: memory COW exit wait`.
The pass branch requires parent wait4 to return the child's PID and exact normal
exit status, unchanged parent memory after child writes, and successful unmaps.
PID 1 continued printing after real select sleeps. The 180-second final console
snapshot decoded with zero unknown glyphs and no visible panic. It is a circular
snapshot, not a complete event trace or proof against every earlier transient.

- Compiler receipt SHA-256:
  `c07c44b204a34e5833d3e605a72b870b3116d648c2690c919969890fa1b6d609`.
- Executed kernel SHA-256:
  `d2e148a3982925554ee990810b7d7ec037aafcf93ac77dae7b801423f706d7b2`.
- Executed Mach-O SHA-256:
  `f884156eb2847161a41634c90c92d477f4ad2ac7983d5738ddc82b81fd380a74`.
- Boot job `118266392988`, receipt SHA-256:
  `06bbf549740ca1a0e09db1d99d1607222d970a811da7b82dd158c09b5bdca639`.

Logs are retained in Actions and locally in `build/runs/39311945725` (text only).
Runner artifacts contain the exact kernel, Mach-O, input hashes, framebuffer,
registers, exception log and receipts. Earlier assembly failure `39310743662`
and explicit cancellation `39310550551` remain preserved; neither ran a guest.

This establishes that the pinned original kernel and freestanding journey work
with the original CPU/MMU/exception environment. It does not validate any native
usermode replacement. Next use this same executable and original boot/service
ordering to assess the userspace boundary; adding missing link providers alone
cannot establish that boundary. Preserve original memory/thread/exception
semantics and require the same complete journey through the Linux-hosted XNU.

The reference is not a working usermode kernel, dyld, frameworks or an iOS app.
The complete objective remains genuine XNU process execution in Linux userspace,
then real dyld/iOS frameworks and an actual app journey on Android.

## Evidence boundaries

| Experiment | Last established result | Does not establish |
| --- | --- | --- |
| `host-substrate` | [ARM64 ptrace/register and 16 KiB mapping probe](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU VM or Android sandbox operation |
| `macho-userland` | [Freestanding ARM64 Mach-O output/error/exit via host shim](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU execution, dyld or frameworks |
| `xnu-baseline` + `xnu-reference-boot` | [Full-system XNU exec, memory/COW, child exit and parent wait](https://github.com/taowen/arlinux-xnu/actions/runs/39311945725) | Userspace XNU, dyld, iOS frameworks or apps |
| `xnu-kernel` | [Selected real kernel graph compiled](https://github.com/taowen/arlinux-xnu/actions/runs/39309052509) | Executable kernel or guest execution |
| `xnu-link-inputs` | Run 39309052509: relocatable merge passes | Executable linkage or kernel boot |
| `xnu-early-startup` | [Latest link fails on lock startup imports](https://github.com/taowen/arlinux-xnu/actions/runs/39270528453); prior raw diagnostics passed | VM initialization, tasks or guest execution |
| `xnu-vm-startup` | [Executable link fails; native engine compiled](https://github.com/taowen/arlinux-xnu/actions/runs/39309052509) | VM bootstrap or guest execution |

These are historical observations at their runs' source revisions, not promises
about current HEAD. No Android app-sandbox journey or third-party iOS app has
been validated. After kernel initialization, separate acceptance gates remain:
guest memory/tasks/exceptions/IPC, dyld and system libraries, frameworks/display/
input, then an actual app journey on Android. Record the highest gate actually
observed; a green probe never advances a later gate.

## Working loop

1. Read this handoff and the exact failing run before changing code. State one
   hypothesis and the observable result that would confirm or reject it.
2. Commit the source, dependency pins and workflow changes together. Dispatch
   only the relevant mode through `build.sh`; `both` explicitly runs all stages.
3. Track the exact run ID and attempt. Inspect its status after interruptions;
   a local wait timeout is not grounds to dispatch another build.
4. Inspect that run's Actions logs, exit/result and printed receipt/executable
   hashes. Download a specific artifact only when its contents are needed for
   diagnosis or durable preservation, not as a routine workstation build step.
   Preserve failures; never relax checks to make an unfinished stage green.
5. Replace the current handoff when the blocker changes. Update the table only
   when an acceptance gate changes. Keep trial history in Actions and Git,
   rather than appending run diaries to README or this document.

Avoid changing unrelated layers in one trial. Re-run the affected upstream and
downstream stages when interfaces change. An unchanged bare-metal baseline need
not run for every Linux host edit. Source pins fix source identity, but hosted
runner images/package repositories still change: retain environment versions,
and do not claim bit-for-bit reproducibility from pins alone.

## Evidence lifecycle

Each executed job writes `receipt.json` alongside its artifacts, including the
Git revision, run/attempt/job identity, dependency lock, host identity, execution
outcome before artifact upload and SHA-256 of retained files. The Actions job
conclusion remains authoritative for upload failures/cancellation; a receipt
cannot certify events after it was written. Checkout/runner failures can prevent
receipt creation, so missing evidence is never a passing result.

Artifacts are retained for 30 days. Download useful failures and milestone runs
through `build.sh download --run-id RUN_ID` before expiry; local downloads are
not a durable shared archive. Before relying on a milestone long-term, preserve
its exact artifact ZIPs, receipts and workflow logs in a versioned external
archive or immutable release. Until that archive exists, old Actions links may
lose their binaries. Do not silently replace expired evidence with a new build.

Git stores source, pins and this short handoff. Actions stores run history and
logs. Artifacts store bytes and provenance. These roles should stay separate.

The default `build.sh build` now selects the real kernel path. Standalone early
diagnostics require `--diagnostics`; host/shim and bare-metal runs require their
explicit modes. The VM startup task links the complete declared service graph and attempts
execution. The former full-link job duplicated the exact same link and exited;
it has been removed. Optional diagnostics do not turn failed kernel execution
into a passing result.
