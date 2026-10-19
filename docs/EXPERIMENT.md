# Experiment contract

## Current handoff

The execution contract is now pinned ARM64 instruction emulation with real XNU
services; native ptrace is no longer the initial target. See
[ARCHITECTURE.md](ARCHITECTURE.md). No genuine XNU guest process has run.

The native adapter transfers integer/FP/SIMD/PSTATE/TLS state, maps XNU-owned
16 KiB frame backing and reports exception/memory/slice stops. It implements no
Darwin syscall or Mach-O loader. Engine operations now switch to a guarded native
CPU stack allocated before XNU startup, avoiding libc/TCG use of small XNU kernel
thread stacks. That stack integration compiled in the latest run but has not executed. The new return path now restores virtual TPIDRRO from the PCB, but has not run.

Latest completed run:
[39303186380](https://github.com/taowen/arlinux-xnu/actions/runs/39303186380),
root `766b14c8a4ab8d007cf43c5ef48c1805ed403506`, XNU `355e0c6e9767a88e58749e2e1f6817d316a4f4b7`,
Unicorn `8028ec436f2d9376525352dd38ed9ed6b9f6be10`.

- Compiler job `118233738297` passed; receipt SHA-256
  `e5c65c875c53f79fd8292e26d0448eb892a15c823cd77493f3706a1aea0c56cf`.
- VM job `118234406591` built the native adapter and ARM64 engine and passed
  the native/kernel ABI audit. Executable linkage still failed on genuine kernel
  machine/pmap/service imports. Receipt SHA-256
  `5ad9c8b3c398e9ae6df81dd44123294becab8382a341a204fba9ad2c806ee532`.
- Built `libunicorn.so.2` SHA-256
  `f1175a539cbc884127a03e554c224e155f9746a1f0e698a42b2b108d66cb70d3`.
  This proves neither engine execution nor XNU bootstrap.

Logs are in `build/runs/39303186380`. The prior engine run `39295473899`
exposed a kernel/native ABI hazard: XNU diagnostic `log(int, format, ...)`
encountered the engine's transitive libm `log(double)`. Kernel external linkage
is now `xnu_kernel_log`, and the misleading libm DSO error no longer masks the
actual missing imports. Do not fix such errors by blindly linking libc libraries.

Compiled, not runtime-validated: engine aliases now retain managed frames;
physical reclaim checks, protection and disconnect include those aliases. Only
known managed backing is accepted. Native mutation failures retain uncertain
references and poison the engine; destroy requires all mappings removed. This
is not user pmap creation, task accounting or a running process. Do not add an
isolated engine mapping probe as acceptance; exercise the real XNU VM journey.

User pmap ownership and geometry compiled in the latest run but did not execute.
Creation/reference/final release use real XNU typed allocation, rwlock, atomic
refs, ledger refs and a per-pmap engine. Unsupported creation modes are rejected;
final release cannot discard live mappings. No hardware page table ops exist.
User queries read engine metadata. No XNU pmap ownership/query path has run.

Compiled, not runtime-validated: resident insertion/replacement/removal/protection and
wiring now update real task ledger terms (phys_mem, internal/external, alternate,
footprint, wired). Physical protect/disconnect synchronizes XNU mapping records
and ledger debit after native completion, with preemption still disabled.
NOENTER reserves XNU/native metadata without mapping page zero; NOWAIT uses
nonblocking XNU allocation. Only allocation failure before uncertain native
mutation is retryable; later failures stop rather than release possibly mapped
frames. Same-frame reentry retains existing accounting classification.

Reusable/compressed mappings and unsupported cache/mapping options remain
explicitly unsupported. They must not be silently billed as ordinary resident
pages. Native reverse aliases and XNU records/ledger terms need the real VM
journey to validate consistency. No isolated mapping sample/test is added.

Compiled, not runtime-validated: the fork routes validated USERMODE byte/string/atomic
copies to a kernel VM transport. A permitted resident mapping is accessed with
preemption disabled; otherwise original vm_fault resolves the page/protection/
COW and the mapping is revalidated before physical access. Public validation,
kernel-to-kernel handling and non-USERMODE behavior remain original. Strings
stop at NUL; atomic transfers use aligned single loads/stores. Wait-if-equals
allows a spurious WFE-hint return, not a fake ulock completion. Native engine
memory APIs never substitute for XNU permission checks.

New, not yet built: original machine-return symbols now reset the consumed
kernel stack and enter a real-thread CPU loop. It restores integer/FP/TLS state,
handles original user ASTs, runs a bounded pmap engine slice and passes synchronous
events to original sleh_synchronous. Clocks poll with actual user saved state;
kernel safe points retain kernel diagnostic frames. PCB TSD setters install
virtual TPIDRRO on next resume. No native Linux TLS register is overwritten.

The pinned Unicorn fork adds read-only pending syndrome/address queries. The
native hook captures real virtual CPU metadata for synchronous exceptions;
internal events are unsupported. Flat mapping failures model leaf permission/
translation errors. No arbitrary byte-pattern decoder or stale ESR/FAR bank is
used. This fork extension and CPU return/IRQ/AST path have not executed.

Next inspect this actual build, then follow the original kernel bootstrap/task/
thread/service ordering toward the supervisor/child journey. The current VM
entry still ends after its intermediate allocator attempt; kernel task startup,
root filesystem/console/exec, nested/commpage and missing machine/service inputs
remain. Copy-fault recovery metadata, debug and timebase behavior also need work.
Do not substitute a CPU sample or a syscall shim for the real process journey.

The first acceptance is supervisor plus child through original BSD exec/VFS:
console output, XNU allocation/touch/free, normal child exit and real parent wait.
Kernel kalloc alone is intermediate. Later gates remain dyld, genuine iOS
frameworks/display/input and an actual app journey. No Apple userland assets or
working root filesystem have been supplied or tested here. Keep the objective
intact and do not replace it with engine samples or successful placeholders.

## Evidence boundaries

| Experiment | Last established result | Does not establish |
| --- | --- | --- |
| `host-substrate` | [ARM64 ptrace/register and 16 KiB mapping probe](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU VM or Android sandbox operation |
| `macho-userland` | [Freestanding ARM64 Mach-O output/error/exit via host shim](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU execution, dyld or frameworks |
| `xnu-baseline` | [ios-sweet bare-metal kernel built on macOS](https://github.com/taowen/arlinux-xnu/actions/runs/39213188337) | Userspace kernel boot or iOS apps |
| `xnu-kernel` | [Selected real kernel graph compiled](https://github.com/taowen/arlinux-xnu/actions/runs/39303186380) | Executable kernel or guest execution |
| `xnu-link-inputs` | Run 39303186380: relocatable merge passes | Executable linkage or kernel boot |
| `xnu-early-startup` | [Latest link fails on lock startup imports](https://github.com/taowen/arlinux-xnu/actions/runs/39270528453); prior raw diagnostics passed | VM initialization, tasks or guest execution |
| `xnu-vm-startup` | [Executable link fails; native engine compiled](https://github.com/taowen/arlinux-xnu/actions/runs/39303186380) | VM bootstrap or guest execution |

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
