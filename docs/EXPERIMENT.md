# Experiment contract

## Current handoff

Question: can actual XNU task, Mach IPC and VM code run behind a Linux ARM64 host
backend with 16 KiB guest pages? The next acceptance is a linked, initialized
kernel backend servicing a guest through real XNU code. Object compilation is
only the first gate; the existing syscall shim does not meet that acceptance.

[Run 39256103670](https://github.com/taowen/arlinux-xnu/actions/runs/39256103670)
at source `b6974c4` (XNU fork `0554506`) compiled 161 kernel/host/MIG units with the minimal `USERMODE` configuration.
Relocatable linkage passes. Eight independent Linux process starts executed the
host pmap's shared-alias, page-copy, zero, protection and removal operations,
including physical-page reverse mappings, alias replacement and disconnect.
Contiguous physical blocks were mapped through the pmap API; reclaim checks
observed live aliases before removal and no aliases after removal/disconnect.
Resident queries counted both virtual aliases, then one and zero as they were
disconnected/removed, while the backing bytes remained accessible physically.
These are guest mapped bytes, not Linux RSS; compressed mappings are unsupported.
The page-fill primitive wrote and checked every 32-bit word of a 16 KiB frame,
while a separate aliased frame retained its contents.
Physical-aperture writes/zeroing changed both read-only aliases' visible bytes,
while ordinary writes still generated SIGSEGV. The RO-zone copy/zero/atomic
providers retain original zone-membership validation and atomic operations,
but have not run on real zone allocations. PPL hardware lockdown is not provided.
Child processes actually received SIGSEGV on writes to read-only aliases and
reads after disconnect/removal; the parent retained the physical backing bytes.
Each start continued through the original `kernel_startup_bootstrap()` dispatcher and startup-debug
tunable, and exited normally. Each start also executed the original ARM rtclock
conversion functions with a Linux nanosecond timebase and a real 1 ms absolute
sleep, plus early/random-buffer/kmem-uniform calls backed by Linux `getrandom`.
No random seed or output bytes are logged. All six jobs recorded receipts and
uploaded their artifacts; Actions logs expose receipt and executable hashes.
This run was inspected through runner logs without downloading binary archives.
The original boolean generator is compiled but has not run.
The early executable SHA-256 is
`6424d5067a84c6a7254a9d92c87451f084665c273e5e6aafc8f2c461289fc7e3`.

ARM ticket reservation now performs real LDAXR/STXR operations with an explicit
ELF recovery range. A Linux [signal handler](https://man7.org/linux/man-pages/man2/sigaction.2.html)
redirects only synchronous faults in that range through the saved
[signal context](https://man7.org/linux/man-pages/man2/sigreturn.2.html).
Every start verified five recoveries on the alternative stack: four SIGSEGV
(read-only and unmapped addresses, repeated) and one real SIGBUS (empty-file
mapping). Valid tickets advance/wrap; invalid words and misaligned writable
words are rejected unchanged. Alignment is checked explicitly because the host
did not fault on the initial unaligned load. A diagnostic child faults outside
the recovery range and must die from SIGSEGV; its intentional `unhandled pc=`
line appears between the parent diagnostic markers in passing logs.
General kernel recovery ranges and guest Mach exceptions remain unimplemented.

Fault recovery also covers an explicit ELF copy-in/out instruction range.
The original `arm64/copyio.c` validation wrappers are compiled, retaining map
bounds, overflow/size limits, alignment, kernel-buffer and zone checks. Hardware
PAN writes are skipped for USERMODE; Linux owns page permissions. Leaf byte,
string and aligned 32/64-bit scalar primitives return XNU errno values through
scoped SIGSEGV/SIGBUS recovery. Every early start verifies real copies, string
termination/truncation, misalignment rejection, read-only and unmapped faults,
a string crossing into an unmapped page, and an empty-file SIGBUS. Each has six
recoveries on the alternative stack (five SIGSEGV, one SIGBUS), then succeeds
on a subsequent copy. Diagnostics preserve the owned pmap page's contents.
Only raw primitives execute here: no fake user task/map is installed. Public
copyio validation, task address isolation and the wait-if-equal path remain
unverified until genuine user maps and scheduling are connected. Copy imports,
including `verify_write`, no longer fail the full VM link.

The host uses Linux TLS for XNU's current-thread access, native/alternative
bootstrap stacks, and a 256 MiB memfd for physical backing. The exported
CPU-number/data accessors return the actual single virtual
CPU record; every early start checks its identity against current-thread state.
CPU type/subtype/thread type come from that record (ARM64/ALL/NONE); count,
boot ID and maximum ID describe one virtual CPU, not Linux's host topology.
Every start verifies these accessors, the early topology query after tunables,
and a write/read/restore through the actual per-CPU pending-AST storage.
This does not execute AST dispatch.
The machine-info singleton reports the same one physical/logical virtual CPU.
After Linux successfully creates physical backing, its memory-size fields and
boot-memory query report that allocation's actual 256 MiB capacity. All eight
starts verify the values. Kernel version fields await normal kernel version
initialization; reporting capacity does not initialize XNU's VM allocator.
That record now lives in the original per-CPU section. The ELF linker pads it
to 16 KiB; all eight starts used XNU's original traversal macro and observed one
master CPU, with no secondary replicas. Current-thread setup installs the master
per-CPU offset in the thread's machine state. Actual scheduling is not running.
The linker script also binds/preserves compiler-generated fixed/variable kalloc
views. Those boundary imports no longer fail the VM link, but the early binary
has no core allocator views; their ingestion by real kalloc is still unverified.
Kernel image/text bounds now come from actual ELF linker symbols. Every start
checks a real function's location and slide/unslide round trip; this fixed ELF
has zero slide and an empty built-in-kext interval. The original kalloc parser
uses coalesced ELF views for USERMODE, preserving view parsing/zone assignment
without walking a nonexistent Mach-O BootKC. That path compiles but has not run.
Compact zone queues use the actual ELF image base for positive queue indices,
covering linked writable sections regardless of orphan-section placement.
The original zone allocator has not executed this encoding yet.
Guest pages are 16 KiB on this runner's 4 KiB host. The ELF image and XNU virtual addresses are
inside a positive 128 GiB kernel window, preserving VM packed-pointer reach.
Zone page encoding uses a tagged offset for this layout. Native brk, the physical
view and kernel heap occupy separate arenas; earlier single-run success hid an
ASLR-dependent collision. Every repeated start retains its actual native brk.
Collision failures now retain overlapping `/proc/self/maps` entries as well.

The merged disassembly's system-register reads/writes now only use host TLS
(`TPIDR_EL0`). The original ARM64 PCB/status code keeps guest thread state while
skipping privileged debug-register writes, unused CurrentEL reads and immediate
guest TSD activation in the host. Hardware breakpoints and guest TSD activation
on entry are not implemented. Privileged hardware operations may be skipped or
delegated to Linux when execution does not need their native mechanism; full
EL1 emulation is not required.

**VM, tasks, Mach/BSD dispatch and guest execution are not running.** The early
executable links the dispatcher, host context/pmap/time/entropy/fault recovery, original rtclock and
required atomics, not the core VM allocator. Its startup table contains only
the debug tunable; advancing to
`STARTUP_SUB_LOCKS` does not establish that XNU lock hooks ran. User-process pmaps
and pmap ledger/wiring accounting are not connected yet. Reference/dirty attributes record
conservative mapping history, not precise accesses or cleared-bit tracking.
The staged VM executable still fails linkage (164 distinct undefined references), before its original
`thread_bootstrap()` or `vm_mem_bootstrap()` can execute. A smaller missing-symbol
count is not a runtime acceptance gate.

ELF startup records are now separated by original XNU phase. VM startup retains
every hook through ZALLOC, the last phase executed by `vm_mem_bootstrap`; a
separate full-link job retains all phases from exactly the same compiled objects
and still fails with 272 missing imports. Boot variables use ordinary individual
ELF data sections, preventing the initial thread from retaining unrelated IPC
callbacks just because Mach-O grouped them in the same boot-data section.
The staged link no longer roots the processor-control MIG server table. This
is dependency isolation, not successful VM execution. Remaining failures include
context switching, pmap lifecycle, compressor and file-backed-memory services.
The VM entry now requires real `kalloc_data` zero/write/read/free after original
VM bootstrap before reporting allocation success; that check has not executed.

The link-map audit found native Linux `fork` and `setrlimit` resolving to
same-named BSD kernel implementations. Kernel compilation now namespaces those
two syscall symbols; Linux probes retain libc calls. Both link stages inspect
actual native imports against kernel definitions and reject ABI collisions,
excluding the deliberately shared `xnu_host_` bridge. The latest run reports no
collisions, and neither native call extracts a kernel archive member anymore.
Separately, retained Mach IPC startup registration references the full MIG server
table, including processor control. Link-time dependencies from this table do
not prove those services are needed before VM bootstrap; do not implement CPU
hotplug or power management merely to satisfy that table.
The original kernel-stack accessor now builds independently of DTrace, matching
its unconditional declaration and the stack walker in task reference accounting.
That missing import is resolved; the stack walker has not executed yet.

Next: connect remaining machine/thread, kernel data layout, host pmap and BSD service dependencies,
then execute the original VM bootstrap. Ticket reservation's recovery is now
connected, while general kernel recovery and guest exception delivery are not.
The selected graph contains real IPC, task/thread, VM/zone allocator, libkern
and required BSD providers; the build script is the authoritative source list.
ARM dispatch selects original dualq with traditional scheduling/priority support.
BSD syscall prototypes use upstream unifdef/makesyscalls; public and kernel MIG
interfaces follow their distinct upstream rules. Optional hooks follow the
selected configuration, including networking and IOKit boundaries.

X300 observation (2026-10-19): Vivo X300/V2509A, ARM64, Android 17/API 37,
Linux 6.12.58 with 4 KiB host pages (`getconf PAGE_SIZE=4096`, corroborated by
procfs KernelPageSize/MMUPageSize). It is a candidate for Android 4 KiB-host /
16 KiB-guest experiments. App-domain ptrace, execution/loading and fixed-address
mapping have not been verified; the ADB shell observation does not prove them.

Kernel mode has six jobs: Linux generates configuration/MIG interfaces; macOS
Apple Clang cross-compiles Linux ELF; Linux separately reports link dependencies,
executes early startup, attempts staged VM startup and checks full service linkage.
Apple Clang is currently required
for XNU allocator type-signature/checking builtins. Compiler/Xcode versions are
retained in artifacts; this is not yet a Linux-only compiler toolchain.

## Evidence boundaries

| Experiment | Last established result | Does not establish |
| --- | --- | --- |
| `host-substrate` | [ARM64 ptrace/register and 16 KiB mapping probe](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU VM or Android sandbox operation |
| `macho-userland` | [Freestanding ARM64 Mach-O output/error/exit via host shim](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU execution, dyld or frameworks |
| `xnu-baseline` | [ios-sweet bare-metal kernel built on macOS](https://github.com/taowen/arlinux-xnu/actions/runs/39213188337) | Userspace kernel boot or iOS apps |
| `xnu-kernel` | [158 kernel/host/MIG units compiled](https://github.com/taowen/arlinux-xnu/actions/runs/39244611652) | Complete kernel or guest execution |
| `xnu-link-inputs` | Same run: relocatable merge passes, host TLS replaces privileged current-thread access | Executable linkage or kernel boot |
| `xnu-early-startup` | Same run: eight starts; real ticket atomics + scoped SIGSEGV/SIGBUS recovery, fatal unmatched child, prior memory/time/entropy/per-CPU/image/dispatcher checks | General recovery/guest exceptions, real RO-zone allocation/update, lock hooks, VM/tasks initialization or guest execution |
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
