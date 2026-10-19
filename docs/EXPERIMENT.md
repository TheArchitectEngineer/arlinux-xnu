# Experiment contract

## Current handoff

The objective is real XNU execution of ARM64 Mach-O, followed by dyld, genuine
iOS frameworks and an app. [Execution design](ARCHITECTURE.md) now defines the
host/kernel/guest ownership and unresolved ABI constraints. It is a proposed
contract, not a working implementation. Continue from those constraints instead
of using unresolved-symbol counts to choose the next subsystem.

The latest observed attempt is
[39292159184](https://github.com/taowen/arlinux-xnu/actions/runs/39292159184),
root `52f265cdd405122f1017034e98860f22cafa245b`, fork `abe6e09`.
Actual compiler job `118195918974` passed original SLEH and kernel timer-event
integration. Compiler receipt SHA-256:
`3a5e5b2e5b517266f56ea153f2b20a0ec9132e6f1adcdb19b019ee94870700f9`.

Actual VM job `118196473118` compiled native timerfd operations and passed the
native/kernel ABI collision audit, but executable linkage failed on remaining
machine/pmap and service imports. Failure receipt SHA-256:
`5240048e75cf632bf4a63c60358522167fa6bdf6db81d1aadaea1acbf9651dad`.
No new VM executable, initialization, real allocation, task or guest ran.
Actual logs are retained under `build/runs/39292159184`; Actions retains complete
receipts/output hashes. The run is terminal; do not redispatch identical source.

A real monotonic timerfd now supplies decrementer programming, remaining-time
and pending-event queries. Kernel interrupt-enable boundaries consume an expiry,
install a diagnostic software kernel-boundary frame, switch to the virtual CPU's
exception stack and call original `sleh_fiq`. Original SLEH/rtclock/timer queues
own callback dispatch; preemption is pinned until CPU interrupt state is restored
and the IRQ stack is free. Urgent AST handling then uses the thread kernel stack.
Both native and kernel code compiled; no timer delivery has run.

The next source fixes CPU initialization required by this path: after original
timer_call_init/processor_bootstrap and before VM bootstrap, initialize the real
per-CPU timer queue, EndOfAllTime deadlines, RTClockData link and actual running
CPU state, following original ARM cpu_init/cpu_data_init. A due event before
this initialization asserts instead of dispatching an uninitialized queue.
Runner verification is pending.

Delivery is cooperative at kernel safe points, not arbitrary native instruction
preemption. The software frame is diagnostic, not a guest architectural return
state. Guest execution and idle waiting still need event/interrupt integration;
arming a timerfd alone does not wake a blocked XNU thread. All new paths require
the real VM/guest journey. No isolated timer probe has been added; preserve the
full execution objective. See ARCHITECTURE.md for the explicit boundary.

`pmap_sync_page_attributes_phys` now has a native implementation distinct from
instruction-cache synchronization: ARM64 DC CIVAC writes back and invalidates
data cache lines across the physical view and every registered alias, with DSB
barriers. Line size comes from Linux's user-visible CTR_EL0; unsupported geometry
fails rather than skipping maintenance. Temporary writable translations permit
data-cache maintenance, and tracked protections are restored without changing
XNU ref/mod or wiring metadata. Code synchronization retains the compiler
runtime's CVAU/IVAU sequence and its executable-alias selection. Both sides
compiled, but no runtime cache journey has run; no device/DMA operation is proved.
Reference: the pinned UML tree's
[Linux cache-maintenance trap handler](https://github.com/zalexdev/linux-um-arm64/blob/32e5e10b81d8882bdaf77b71e4106a941840c750/arch/arm64/kernel/traps.c)
supports DC CIVAC with fault propagation.

Physical-page interlocks use original XNU spinlocks per managed frame and a
separate backup lock for foreign/fictitious identifiers, matching ARM selection.
A LOCKS callback initializes the array after lock-group/attribute startup.
Native mapping/tracking/cache mutations disable original XNU preemption through
the complete operation on this one-CPU backend. These compiled in the above run,
but lock initialization and VM fault interlocks have not run. No standalone
lock probe or successful lock placeholder is introduced.

Protection semantics remain compiled but unverified in a VM. Ordinary writable
requests preserve mappings for later VM fault/COW handling; explicit immediate
write requests update shared reference/modified tracking. Thread-state and ulock
providers also have not executed; the real libpthread kernel provider is still
not registered. Internal legacy register conversion is not 32-bit app support.

Dispatch now compares the selected local ref with its GitHub commit before
submitting. Both real entry paths were observed: pushed `f314bf1` dispatched,
and unpushed `20c23ff` was refused with no run created. Earlier accidental
old-source run `39287502627` was confirmed cancelled. This fixes dispatch source
selection, not guest execution. Cache synchronization and fail-stop native
mapping errors remain compiled but unverified at runtime. No device/DMA journey has been implemented.

The NOENTER correction from `72f6733` is included: original VM fault capacity
requests no longer attempt to map physical page zero. This has compiled, but
no original VM fault/retry path has executed. The user pmap backend still rejects
user mappings, and its creation/activation, actual guest execution/return and
timer interrupt delivery remain core implementation work. Existing compiled
BSD/VM services cannot make a guest run until those boundaries are connected.

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
