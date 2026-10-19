# Experiment contract

## Current handoff

Question: can actual XNU task, Mach IPC and VM code run behind a Linux ARM64 host
backend with 16 KiB guest pages? The next acceptance is a linked, initialized
kernel backend servicing a guest through real XNU code. Object compilation is
only the first gate; the existing syscall shim does not meet that acceptance.

[Run 39220160469](https://github.com/taowen/arlinux-xnu/actions/runs/39220160469)
compiled 43 real XNU units plus the host entry into AArch64 ELF. The Linux host
also linked and executed the upstream `kernel_startup_bootstrap()` dispatcher,
read `startup_debug=1` through its real tunable callback, and exited normally.
Downloaded artifact hashes matched all four jobs' receipts.

This executable currently links only `startup.o`, the host entry and Linux
platform services. Its initialization table contains the startup debug tunable;
although the dispatcher advances to `STARTUP_SUB_LOCKS`, no XNU lock-init hooks
are linked yet. **VM, tasks, Mach/BSD dispatch and guest execution are not running.**
The separate relocatable merge of all selected objects has 1,067 unresolved
symbols, including other XNU units and required host backends.

Next: provide the current-thread and basic host memory backends, connect actual
VM/allocator initialization and grow the executable toward the Mach-O journey.
The expanded disassembly contains 30 `TPIDR_EL1` reads; these must use host thread
state or emulation before their paths execute. Privileged hardware operations
may be skipped or delegated to Linux when the program can run without their
native mechanism. Implement behavior needed for execution rather than all EL1
hardware semantics; use observed failures to decide what is needed next.

Kernel mode has four jobs: Linux generates upstream configuration/MIG interfaces;
macOS Apple Clang cross-compiles Linux ELF; Linux reports link dependencies and
executes early startup in separate jobs. Apple Clang is currently required for
XNU's allocator type-signature/checking builtins. Compiler/Xcode versions are
retained in artifacts; this is not yet a Linux-only compiler toolchain.

## Evidence boundaries

| Experiment | Last established result | Does not establish |
| --- | --- | --- |
| `host-substrate` | [ARM64 ptrace/register and 16 KiB mapping probe](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU VM or Android sandbox operation |
| `macho-userland` | [Freestanding ARM64 Mach-O output/error/exit via host shim](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU execution, dyld or frameworks |
| `xnu-baseline` | [ios-sweet bare-metal kernel built on macOS](https://github.com/taowen/arlinux-xnu/actions/runs/39213188337) | Userspace kernel boot or iOS apps |
| `xnu-kernel` | [43 XNU units and host entry compiled](https://github.com/taowen/arlinux-xnu/actions/runs/39220160469) | Complete kernel or guest execution |
| `xnu-link-inputs` | Same run: relocatable merge and disassembly; 1,067 unresolved symbols | Executable linkage or kernel boot |
| `xnu-early-startup` | Same run: upstream dispatcher and its startup-debug tunable executed on Linux | Lock hooks, VM/tasks initialization or guest execution |

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
4. Download that run's artifacts. Compare the expected observation with actual
   logs, exit/result and binary hashes. Preserve failures; do not relax checks
   to make an unfinished stage green.
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
