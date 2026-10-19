# Experiment contract

## Current handoff

The objective is real XNU execution of ARM64 Mach-O, followed by dyld, genuine
iOS frameworks and an app. [Execution design](ARCHITECTURE.md) now defines the
host/kernel/guest ownership and unresolved ABI constraints. It is a proposed
contract, not a working implementation. Continue from those constraints instead
of using unresolved-symbol counts to choose the next subsystem.

The latest observed real-kernel attempt is
[39284001374](https://github.com/taowen/arlinux-xnu/actions/runs/39284001374),
root `bc2d15ff623cc7bf86e233705de90c474a4ab9f1`, fork `644e9ef`.
Actual compiler job `118165016525` compiled original shared-region, shared-cache
and dyld pagers plus guarded FD lifecycle. Compiler receipt SHA-256:
`152c13956810b2266d1c9a3b206d9219d8d3024449e41af634a1f57498e2e323`.

Actual VM job `118165562230` passed the native/kernel ABI collision audit,
but executable linkage still failed on machine/pmap, BSD shutdown and further
services. Its failure receipt SHA-256:
`a5645fa33ae8dc076d8d716945027b7995621820f106d3a3eaaa8e627195d264`.
No VM initialization, real allocation, task or guest ran. Actual logs are retained
under `build/runs/39284001374`; Actions retains receipts and output hashes.

Source review of the original VM fault retry exposed a host pmap semantic error:
`PMAP_OPTIONS_NOENTER` passes physical page zero to request mapping-capacity
expansion only. The host previously ignored the option and attempted a real
mapping, returning failure. The repaired kernel-only path validates the address
and acknowledges the capacity already reserved at host memory initialization,
without touching mappings, permissions, wiring or page attributes. User pmaps
still return KERN_NOT_SUPPORTED. Original reference: `arm/pmap/pmap.c` returns
before PTE installation, and `vm/vm_fault.c` re-drives the fault afterwards.
This repair requires runner verification through the real VM journey; no isolated
mapping probe is added, and no guest execution claim follows from compilation.
Inspect existing experiment
[39284374578](https://github.com/taowen/arlinux-xnu/actions/runs/39284374578),
source `72f6733`, before another dispatch. Reuse this run handle through delayed
metadata/log observations; never resubmit identical source because of a timeout.

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
