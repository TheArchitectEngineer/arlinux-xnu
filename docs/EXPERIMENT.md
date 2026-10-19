# Experiment contract

## Current handoff

Question: can actual XNU task, Mach IPC and VM code run behind a Linux ARM64 host
backend with 16 KiB guest pages? The next acceptance is a linked, initialized
kernel backend servicing a guest through real XNU code. Object compilation is
only the first gate; the existing syscall shim does not meet that acceptance.

[Run 39218516888](https://github.com/taowen/arlinux-xnu/actions/runs/39218516888)
compiled all eight selected real XNU units (task/thread, exception, Mach IPC and
VM) into AArch64 ELF and combined them on Linux using a relocatable link.
Downloaded artifacts matched their receipt hashes. The result is **not an
executable**: its link report contains 800 unresolved symbols. Many are other
XNU components not yet in the build, rather than host services to reimplement.

Next: use `unresolved-symbols.txt` and the real source call graph to bring in the
required XNU initialization/allocator/IPC/VM dependencies, then implement the
Linux memory, current-thread, exception and scheduling backends. Privileged hardware operations may be skipped or delegated to Linux when the
program can run without their native mechanism. Implement the behavior needed
for execution rather than reproducing all EL1 hardware semantics. Check the
observed program behavior before expanding an emulation; successful calls alone
do not prove app compatibility. Acceptance remains an initialized backend
servicing the guest through real XNU, not a successful relocatable link.

The kernel mode has three explicit stages: Linux generates upstream config and
MIG interfaces; macOS Apple Clang cross-compiles Linux ELF; Linux combines the
objects and reports unresolved symbols. Apple Clang is currently required for
XNU's allocator type-signature/checking builtins. Stock Linux Clang 18 compiled
`ipc_space.c` but failed on these builtins in other units. They were not replaced
with dummy values. The compiler/Xcode versions are retained in artifacts; this
is a hosted cross-build, not yet a Linux-only compiler toolchain.

## Evidence boundaries

| Experiment | Last established result | Does not establish |
| --- | --- | --- |
| `host-substrate` | [ARM64 ptrace/register and 16 KiB mapping probe](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU VM or Android sandbox operation |
| `macho-userland` | [Freestanding ARM64 Mach-O output/error/exit via host shim](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU execution, dyld or frameworks |
| `xnu-baseline` | [ios-sweet bare-metal kernel built on macOS](https://github.com/taowen/arlinux-xnu/actions/runs/39213188337) | Userspace kernel boot or iOS apps |
| `xnu-kernel` | [Eight actual XNU units compiled as Linux AArch64 ELF](https://github.com/taowen/arlinux-xnu/actions/runs/39218516888) | Complete kernel, initialization or guest execution |
| `xnu-link-inputs` | Same run: relocatable merge; 800 unresolved symbols retained | Executable linkage, boot or a functioning host backend |

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
