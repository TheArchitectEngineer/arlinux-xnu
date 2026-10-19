# Experiment contract

## Current handoff

The native-XNU/Unicorn-user hybrid is an **unvalidated architecture candidate**,
not an established implementation route. Further machine-layer expansion is
held behind an execution reference: run the original kernel and a real process
journey with the CPU/MMU/exception environment intact. This does not replace the
userspace-XNU goal, and a reference pass cannot satisfy that goal.

Latest completed usermode attempt:
[39309052509](https://github.com/taowen/arlinux-xnu/actions/runs/39309052509),
root `67949d497238323a7111d7a6ce8640a6e5ecf120`,
XNU `ac6d2d422d4232a01e8e01c564b63a50d0772870`,
Unicorn `2bca534e6168c9096e326c1868279a5baed4f9b3`.

- Kernel compiler job `118253977259` succeeded. Receipt SHA-256:
  `3a9323beab1ddf0fbe87e1096bc06390e88a36548eee694f01c6ff3293da2a47`.
- Full service job `118254540494` failed executable linkage, including original
  kernel_bootstrap dependencies `bsd_scale_setup` and `mem_size`. Receipt SHA-256:
  `2055dae91a1f350b356ed34fd412430eb93194ccb357cd534e5b236f4ad28230`.
- Native engine SHA-256:
  `329f3fb3b771c3cb3777549b064e62e590e86d028a58af804f4f8e49c291a135`.
  Compilation and the ABI audit passed; neither establishes execution.

Text logs: `build/runs/39309052509`. No executable linked, no real VM bootstrap
completed, no XNU user process ran. Prior code for user pmap ownership/ledgers,
copyio, machine return, virtual TLS, exception transport and timers remains
runtime-unvalidated. A local memory-size globals edit is also unvalidated; it
must not be presented as fixing kernel boot.

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
explicit modes. The real VM and full-service links remain required attempts;
optional diagnostics do not turn failed kernel execution into a passing result.
