# Experiment contract

## Current handoff

Question: can actual XNU task, Mach IPC and VM code run behind a Linux ARM64 host
backend with 16 KiB guest pages? The next acceptance is a linked, initialized
kernel backend servicing a guest through real XNU code. Object compilation is
only the first gate; the existing syscall shim does not meet that acceptance.

[Run 39228876244](https://github.com/taowen/arlinux-xnu/actions/runs/39228876244)
compiled 99 kernel/host/MIG units with the minimal `USERMODE` configuration.
Relocatable linkage passes. Eight independent Linux process starts executed the
host pmap's shared-alias, page-copy, zero, protection and removal operations,
then the original `kernel_startup_bootstrap()` dispatcher and startup-debug
tunable, and exited normally. All five downloaded jobs' files matched receipts.

The host uses Linux TLS for XNU's current-thread access, native/alternative
bootstrap stacks, and a 256 MiB memfd for physical backing. Guest pages are
16 KiB on this runner's 4 KiB host. The ELF image and XNU virtual addresses are
inside a positive 128 GiB kernel window, preserving VM packed-pointer reach.
Zone page encoding uses a tagged offset for this layout. Native brk, the physical
view and kernel heap occupy separate arenas; earlier single-run success hid an
ASLR-dependent collision. Every repeated start retains its actual native brk.
Collision failures now retain overlapping `/proc/self/maps` entries as well.

The merged disassembly has no `TPIDR_EL1` or DAIF register accesses. Privileged
hardware operations may be skipped or delegated to Linux when execution does
not need their native mechanism; full EL1 emulation is not required.

**VM, tasks, Mach/BSD dispatch and guest execution are not running.** The early
executable links the dispatcher, host context, host pmap and entry, not the core
VM allocator. Its startup table contains only the debug tunable; advancing to
`STARTUP_SUB_LOCKS` does not establish that XNU lock hooks ran. User-process pmaps,
reverse mappings and pmap accounting are not connected yet. The VM executable
still fails linkage (468 distinct undefined references), before its original
`thread_bootstrap()` or `vm_mem_bootstrap()` can execute. A smaller missing-symbol
count is not a runtime acceptance gate.

Next: connect remaining original providers (including task references and the
C++ priority queue), then the machine/thread, BSD, time/entropy and remaining
host pmap dependencies, and execute the original VM bootstrap. Selected objects
already include scheduler/waitq/turnstile, SMR, task policy and VM interface
wrappers; their full runtime use remains unverified.

Kernel mode has five jobs: Linux generates configuration/MIG interfaces; macOS
Apple Clang cross-compiles Linux ELF; Linux separately reports link dependencies,
executes early startup, and attempts VM startup. Apple Clang is currently required
for XNU allocator type-signature/checking builtins. Compiler/Xcode versions are
retained in artifacts; this is not yet a Linux-only compiler toolchain.

## Evidence boundaries

| Experiment | Last established result | Does not establish |
| --- | --- | --- |
| `host-substrate` | [ARM64 ptrace/register and 16 KiB mapping probe](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU VM or Android sandbox operation |
| `macho-userland` | [Freestanding ARM64 Mach-O output/error/exit via host shim](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU execution, dyld or frameworks |
| `xnu-baseline` | [ios-sweet bare-metal kernel built on macOS](https://github.com/taowen/arlinux-xnu/actions/runs/39213188337) | Userspace kernel boot or iOS apps |
| `xnu-kernel` | [99 kernel/host/MIG units compiled](https://github.com/taowen/arlinux-xnu/actions/runs/39228876244) | Complete kernel or guest execution |
| `xnu-link-inputs` | Same run: relocatable merge passes, host TLS replaces privileged current-thread access | Executable linkage or kernel boot |
| `xnu-early-startup` | Same run: eight process starts; host pmap operations plus original early dispatcher | Lock hooks, VM/tasks initialization or guest execution |
| `xnu-vm-startup` | Same run: executable link fails; log and link map retained | VM bootstrap or guest execution |

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
