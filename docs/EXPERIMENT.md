# Experiment contract

## Current handoff

Question: can actual XNU task, Mach IPC and VM code run behind a Linux ARM64 host
backend with 16 KiB guest pages? The next acceptance is a linked, initialized
kernel backend servicing a guest through real XNU code. Object compilation is
only the first gate; the existing syscall shim does not meet that acceptance.

[Run 39265164164](https://github.com/taowen/arlinux-xnu/actions/runs/39265164164)
at source `c91d515` (XNU fork `7ff463f06f32d144c6ccd62c0bbd5bd4a7b37dd6`)
compiled the selected kernel graph and passed the existing early diagnostics.
**The real VM executable failed linkage with 176 distinct unresolved imports.**
Neither original `thread_bootstrap()` nor `vm_mem_bootstrap()` executed. No real
XNU allocation, task, guest exception, Mach/BSD call or iOS app is established.
The log is authoritative; symbol counts and compiled-unit counts are not
acceptance milestones.

The next gate is original VM initialization followed by `kalloc_data` allocation,
zero/read/write verification and `kfree_data`, inside the real kernel entry.
It must then advance to a genuine guest task and ARM64 Mach-O output/allocation/
exit through XNU. A passing raw primitive or host syscall shim cannot substitute.
Do not expand standalone diagnostic samples while this path remains blocked.

The VM entry now contains only required initialization: a reserved kernel stack,
original bootstrap thread adoption, host memory/time/Device Tree setup, original
startup hooks, `timer_call_init`, `processor_bootstrap`, then `vm_mem_bootstrap`.
The ARM64 context backend saves/restores the real XNU kernel context and uses
Linux TLS instead of EL1 current-thread registers. Its context-switch/continuation
path is compiled but has not executed. The bootstrap stack is allocated before
TUNABLES using the same stack-page boot argument and geometry as `stack_init`.

The existing early diagnostic entry remains separate. It exercises host mappings,
copy/ticket fault recovery, codecs, time and entropy; these results only help
localize failures in those primitives. The VM path does not run these samples.
The early executable's LOCKS phase does not prove core lock hooks executed.
Linux owns native mappings, protection, TLB maintenance, clocks and entropy.
Hardware privilege may be skipped or delegated where appropriate; EL1 emulation
is not required. Guest memory, task and syscall semantics must still be real.

Current backend limitations that affect the real path:

- Kernel pmap mappings use a 256 MiB shared memfd and reverse alias chains.
  User pmaps, ledger accounting and address-space activation are unimplemented.
  Reference/dirty history is conservative, without cleared-bit fault tracking.
- The ELF image, native brk, bootstrap stack, physical aperture and kernel heap
  occupy separate arenas inside the positive kernel address window. Native
  mapping failures retain overlapping `/proc/self/maps` entries.
- ELF startup records retain all original hooks through ZALLOC for VM startup.
  A separate full-link job retains every service registration from the same
  kernel objects. Stage isolation is not proof that a stage runs.
- Original compressor/pager, swap backend and WKdm16k/LZ4 code compile. Raw codec
  diagnostics do not demonstrate a working compressed pager or swap I/O.
- Scoped synchronous SIGSEGV/SIGBUS recovery serves ticket/copy primitives.
  General kernel recovery, task isolation and guest Mach exceptions are pending.
- Linux TLS uses `TPIDR_EL0`; ARM64 context switching also preserves EL0 FPCR.
  Guest TSD activation, hardware breakpoints and timer delivery are pending.
- Native/kernel ABI collision checks reject same-named imports. BSD `fork` and
  `setrlimit` are namespaced so native Linux calls cannot bind kernel ABIs.
- Keep DEVELOPMENT with MACH_ASSERT: removing DEVELOPMENT alone broke upstream
  assertion bookkeeping (run 39257044605); it was reverted, not papered over.

Remaining link dependencies include pmap lifecycle, thread machine interfaces,
tracing, process teardown and file-backed memory. Resolve real providers or
implement the required Linux backend semantics, rather than generating success
stubs to satisfy symbols. The build script is the authoritative source graph.

X300 was observed on 2026-10-19 as ARM64 Android 17/API 37 with a 4 KiB Linux host
page size. App-domain execution, ptrace and fixed-address mapping remain
unverified; ADB shell observations are not Android app acceptance.

Kernel mode has six jobs: Linux generates configuration/MIG interfaces; macOS
Apple Clang cross-compiles Linux ELF; Linux reports link dependencies, executes
early diagnostics, attempts real VM startup and checks full service linkage.
Apple Clang is currently required for XNU allocator type-signature builtins.
Compiler/Xcode versions are retained; this is not a Linux-only compiler chain.

## Evidence boundaries

| Experiment | Last established result | Does not establish |
| --- | --- | --- |
| `host-substrate` | [ARM64 ptrace/register and 16 KiB mapping probe](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU VM or Android sandbox operation |
| `macho-userland` | [Freestanding ARM64 Mach-O output/error/exit via host shim](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU execution, dyld or frameworks |
| `xnu-baseline` | [ios-sweet bare-metal kernel built on macOS](https://github.com/taowen/arlinux-xnu/actions/runs/39213188337) | Userspace kernel boot or iOS apps |
| `xnu-kernel` | [Selected real kernel graph compiled](https://github.com/taowen/arlinux-xnu/actions/runs/39265164164) | Executable kernel or guest execution |
| `xnu-link-inputs` | Same run: relocatable merge passes | Executable linkage or kernel boot |
| `xnu-early-startup` | Same run: existing isolated diagnostics pass | VM initialization, tasks or guest execution |
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
