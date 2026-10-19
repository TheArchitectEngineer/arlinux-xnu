# Experiment contract

## Current handoff

The objective is real XNU execution of ARM64 Mach-O, followed by dyld, genuine
iOS frameworks and an app. [Execution design](ARCHITECTURE.md) now defines the
host/kernel/guest ownership and unresolved ABI constraints. It is a proposed
contract, not a working implementation. Continue from those constraints instead
of using unresolved-symbol counts to choose the next subsystem.

The latest observed real-kernel attempt is
[39282406671](https://github.com/taowen/arlinux-xnu/actions/runs/39282406671),
root `91ed81d42a512c17c9209b0fd8011c74aaa31daa`, fork `574ac76`.
Actual compiler job `118159350657` compiled the original BSD process/FD/vnode
limits and the real static ELF early trace buffer. Compiler receipt SHA-256:
`0ff362e7722eaabcad277aea6e9b66c29351832375e09484a6f5d732e638e148`.

Actual VM job `118160103988` passed the native/kernel ABI collision audit,
but executable linkage still failed on machine/pmap, BSD time and further
services. Its failure receipt SHA-256:
`6027fff3fa4f7beb76ee581a530d46e275899c68aa25c57a35e6fd9e21539eed`.
No VM initialization, real allocation, task or guest ran. Actual logs are retained
under `build/runs/39282406671`; Actions retains receipts and output hashes.

The next source adds original `bsd/kern/kern_time.c`: the actual kevent/pselect
path lacks its `timespec_is_valid` provider, and BSD time/interval calls belong
to the same original implementation. Fork `27e48f543d6d420f7e2d50ae2909daabc13ad126`
also protects the three kperf reset calls with the existing KPERF configuration.
With KPERF disabled, user configuration returns ENOTSUP and internal kernel
configuration asserts that KPERF was not requested; no pretend sampling/reset
provider is added. Original kdebug reset/ownership behavior remains intact.
These changes require runner verification and do not establish timekeeping,
timer interrupt delivery or a working VM.

GitHub run/job metadata has lagged behind directly available job logs. Inspect
existing job IDs and available logs before concluding that compilation never ran.
A queue, observation error, cancellation or missing log is never a passing result;
none justifies resubmitting identical source. Earlier integration failures and
repairs remain in Git/Actions rather than growing this handoff into a run diary.

The source now includes permission-based reference/dirty tracking and original
tracing/SFI providers. These have not been exercised by real XNU VM operations.
Enabling virtual interrupts now follows original ARM urgent-AST handling before
unmasking; local AST notification validates the one-CPU identity and idle queries
read the actual processor state. These paths have not run in a real VM journey.
Core gaps include user pmap lifecycle/activation, genuine guest execution and
return, user-copy access, machine events, BSD/VFS setup and service dependencies.
Do not add isolated samples or success stubs to hide these gaps.

Architecture review found a prerequisite absent from the original plan: Apple's
TSD accessor directly reads `TPIDRRO_EL0`, while Linux/UML TLS controls
`TPIDR_EL0`. Merely saving `thread->machine.cthread_self` cannot activate Darwin
TLS. USERMODE now rejects that installation until an actual guest instruction/TLS
adaptation exists. The non-USERMODE hardware path remains original. This is an
explicit unsupported operation, not a successful framework port.

Execution-engine comparison now records a second candidate: Unicorn 2.1.4
exposes TPIDRRO state and shared host-memory mappings. This is source/API evidence
only; no engine, framework or guest journey has been integrated. Execution
preference is being clarified; neither candidate is established as working.

Next: resolve the execution/TLS contract, then implement the host helper,
real XNU task/map association and dispatch/return paths together. Use ios-sweet's
working service graph as the functional reference for kernel startup and later
iOS services. Preserve exact source pins and compile on GitHub runners through
`build.sh`. Existing early diagnostics, the host syscall shim and the bare-metal
baseline are historical observations; none advances the guest execution gate.

The first end-to-end acceptance is the genuine BSD/Mach guest journey specified
in the design: supervisor plus child, console output, XNU allocation/touch/free,
child exit and a real parent wait status. Kernel `kalloc_data` inside original VM
initialization remains an intermediate observation, not the objective's end.

## Evidence boundaries

| Experiment | Last established result | Does not establish |
| --- | --- | --- |
| `host-substrate` | [ARM64 ptrace/register and 16 KiB mapping probe](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU VM or Android sandbox operation |
| `macho-userland` | [Freestanding ARM64 Mach-O output/error/exit via host shim](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU execution, dyld or frameworks |
| `xnu-baseline` | [ios-sweet bare-metal kernel built on macOS](https://github.com/taowen/arlinux-xnu/actions/runs/39213188337) | Userspace kernel boot or iOS apps |
| `xnu-kernel` | [Selected real kernel graph compiled](https://github.com/taowen/arlinux-xnu/actions/runs/39270528453) | Executable kernel or guest execution |
| `xnu-link-inputs` | Same run: relocatable merge passes | Executable linkage or kernel boot |
| `xnu-early-startup` | [Latest link fails on lock startup imports](https://github.com/taowen/arlinux-xnu/actions/runs/39270528453); prior raw diagnostics passed | VM initialization, tasks or guest execution |
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
