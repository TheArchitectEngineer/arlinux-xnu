# Userspace XNU execution design

This is an implementation contract, not working iOS compatibility. No genuine
XNU guest process has run. ARM64, 16 KiB guest pages and one virtual CPU remain
fixed. All compilation and execution use `build.sh` on GitHub runners.

## Selected execution boundary

Use a pinned Unicorn 2.1.4-based ARM64 engine for guest user instructions. The
[taowen fork](https://github.com/taowen/unicorn) adds read-only pending exception
syndrome/address registers, queried inside the interrupt hook. Exact fork/base
sources are recorded in `sources.lock.json`; no build-time patch pile is used.
This deliberately prioritizes controllable CPU state over native performance.
Native ptrace execution is no longer the initial implementation target.

ARM64 UML's existing execution backend is a reference, not a drop-in XNU port.
Its Linux guest TLS uses writable TPIDR_EL0. Apple's libsyscall directly reads
TPIDRRO_EL0, which cannot be installed with Linux's ordinary NT_ARM_TLS regset.
Intercepting SVC alone cannot intercept these legal register reads. Unicorn
provides both registers without rewriting framework instructions.

Primary implementation references:

- [UML TLS](https://github.com/zalexdev/linux-um-arm64/blob/32e5e10b81d8882bdaf77b71e4106a941840c750/arch/arm64/um/shared/sysdep/stub.h).
- [Apple TSD](https://github.com/apple-oss-distributions/xnu/blob/1b191cb58250d0705d8a51287127505aa4bc0789/libsyscall/os/tsd.h).
- [Unicorn register implementation](https://github.com/unicorn-engine/unicorn/blob/8028ec436f2d9376525352dd38ed9ed6b9f6be10/qemu/target/arm/unicorn_aarch64.c).

The native engine adapter is `src/xnu-host/native_guest.c`; its libc-independent
ABI is `include/host/guest.h`. It transfers integer, FP/SIMD, PSTATE, FPCR/FPSR
and both user TLS registers. Each run has a finite instruction budget. Synchronous
exceptions and invalid/protected memory accesses stop execution and return an
event plus CPU state. Callbacks never enter XNU, schedule threads or implement
Darwin calls. An unresolved memory fault is never reported as resolved.

## Ownership and required integration

| Owner | Responsibilities |
| --- | --- |
| Linux host | Native kernel execution, files, shared physical backing, events |
| Real XNU | Tasks/threads, scheduler, VM objects/maps/faults, Mach IPC, BSD/VFS/exec/exit |
| Instruction engine per user pmap | Isolated guest VA translations and user CPU execution |
| Guest | ARM64 Mach-O instructions, later real dyld and iOS frameworks |

XNU pmap ownership now uses a real kalloc_type object, original rwlock and
atomic reference count, a paired ledger reference per pmap reference and one
native engine. Final release requires an empty engine and releases resources
only after successful engine close. This lifecycle has not executed. Fixed
16 KiB geometry is provided using pinned ARM constants; it supplies VM geometry,
not hardware TTE/ASID operations. Flags for unsupported modes are rejected.

Machine pmap switching selects a borrowed per-map engine. PMAP_SWITCH_USER also
updates the current thread map that original vm_map_switch/copyio use. Initial
user return may select lazily; final release clears a borrowed selection before
freeing it. This activation path remains unexecuted.

The native engine accepts
only aligned frames in the initialized managed physical aperture and records
each engine/VA/frame/protection association. Frame references are established
before insertion and dropped only after completed unmapping. Physical reclaim
checks include these aliases; physical protection/disconnect visit every engine.
Reduction intersects existing rights. Destroying an engine with mappings is
rejected. A failed native mutation poisons the engine and retains uncertain
references: stop rather than recycling potentially mapped RAM. No subsequent
operation may resume a poisoned engine.

Engine mappings never MAP_FIXED guest addresses over the native process.
Code translations are flushed before resume because XNU can modify shared
physical bytes through another alias. Ref/mod tracking relies on native accesses
to the protected physical aperture; this needs real fault/TCG validation.

User insertion, same-frame permission/wiring updates, frame replacement, range
removal/protection and physical disconnect now update both engine aliases and
real XNU task ledgers. Resident, internal/external, alternate, footprint and wired
terms follow the pinned ARM provider. A ledger-less submap carries no task ledger
balance. NOENTER prepares XNU and native mapping metadata without mapping page
zero; NOWAIT respects XNU allocator nonblocking rules. Native allocation failure
is retryable only before uncertain mutation. Replacement reserves capacity before
removing the old mapping. Native mutation failures stop kernel execution.

Ledger changes happen on the XNU stack, never in engine callbacks. Native physical
protection/disconnect completes before corresponding XNU mapping records/ledgers
are updated, all with preemption disabled. Ordinary protection reductions cannot
upgrade permissions; explicit immediate write restores RW and ref/mod tracking.
Compressed/reusable mappings and unsupported mapping/cache options are rejected
until their accounting exists; they must not silently use resident accounting.
These operations are new and have not run through real XNU. User-copy, VM/COW
fault dispatch, nested/shared-cache/commpage maps and runnable threads remain
unfinished. The objective still includes those paths and a genuine app journey.

The machine return path now transfers the real thread's integer/FP/SIMD state,
Darwin TPIDRRO/TSD and CPU-ID TPIDR_EL0 to its pmap engine. It preserves original
x18 policy and enters original SLEH for BSD/Mach SVC dispatch and VM faults.
A noreturn assembly entry resets the finished kernel call stack before the next
user execution; blocking/continuations remain in the original scheduler. TSD
setters store real PCB state that is installed on each engine resume, without
touching host Linux TLS. None of this path has executed through XNU yet.

Engine exception indices are not ESR. The fork exposes pending syndrome/address
from the actual virtual CPU before exception_index is cleared. Internal/yield/
semihost events remain unsupported. Flat mapping failures explicitly model
translation/permission faults of a 16 KiB leaf; unaligned entry PC is an ARM PC
alignment event. The transport does not decode arbitrary instruction byte
patterns or treat stale ESR_EL1/FAR_EL1 banks as current exceptions.

Bounded user slices poll pending clock events against the actual stopped user
saved state, while kernel safe points retain diagnostic kernel frames. Original
FIQ/timer queues and user AST handling own scheduling; user return never uses the
kernel-only urgent-AST shortcut. PFZ deferral follows CONFIG_ARM_PFZ, as in original
locore. Timer/AST/state restoration remains runtime-unverified. Direct counter/
commpage timebase, hardware debug, all uncommon exception cases and full kernel
startup still require work; this is not working app compatibility.

USERMODE copyio now retains original copyio.c bounds/tag/zone/alignment checks
and kernel-to-kernel handling, then copies isolated user memory through real
pmap/VM state. A resident mapping must permit the actual read/write. Otherwise
original vm_fault with THREAD_UNINT resolves permissions, pager faults and COW.
The current thread retains its map as in original kernel copyio abort handling.
After fault return, re-read the current map and pmap; never copy through a stale
page/protection result. Physical access stays non-preemptible on this one CPU.
Blocking faults in interrupt/preemption-disabled contexts are rejected as kernel
errors. No engine memory-read/write API bypasses VM protection.

Byte/string copies progress by page; strings stop at the first NUL and preserve
EFAULT/ENAMETOOLONG length rules. Aligned atomic copies perform one 32/64-bit
physical load/store. Wait-if-equals reads the real value; equal may finish as a
spurious WFE hint, not as ulock completion. Original ulock scheduling still must
be connected and validated. Page boundaries service cooperative IRQ safe points
without unmasking a caller's masked interrupts. These paths are unexecuted.
Architectural copy-fault/recover metadata still needs integration;
this C transport does not synthesize a complete ARM exception frame.

Linux syscall buffers and engine/libc calls must still use native storage: Linux
user-copy cannot fault in a protected kernel buffer through XNU's SIGSEGV handler.

The existing timerfd/eventfd transport provides cooperative kernel safe-point
interrupt delivery, not asynchronous native C preemption. Its original XNU
FIQ/timer/idle paths remain unexecuted. Guest direct counter reads and commpage
must agree with the advertised XNU timebase; selecting an engine does not itself
establish that contract. Engine operations switch to a guarded 1 MiB native CPU stack allocated before
XNU startup, avoiding libc/TCG execution on XNU kernel thread stacks. Nested
entry is rejected; no engine callback may schedule XNU. Stack switching and
the actual guest path still require runtime validation.

Default host entry now performs the original ARM pre-VM timer/processor setup,
then calls original kernel_bootstrap exactly once. It no longer exits after a
kernel allocation sample. Both boot/link stages extract every declared kernel archive member, then
retain compiled startup registrations including phases after ZALLOC; omitted callbacks must never be
replaced by merely advancing startup_phase. This full bootstrap attempt still
requires missing machine/service inputs and real runtime validation.

## First execution journey

Boot the real kernel, mount a file-backed image through real XNU VFS/filesystem
services, and use original BSD exec/Mach-O loading. Start a supervisor and child;
the child writes a console FD, allocates/touches/frees memory, then exits with a
known status observed through the supervisor's real wait. Keep PID 1 alive.
There is no host Mach-O loader or replacement Darwin syscall dispatcher here.

Only that complete journey advances guest execution. Engine compilation, isolated
instruction samples, allocator probes and shim demos do not. Afterwards connect
dyld/shared cache, matching libpthread, Mach/XPC services, display/input and an app.
The [ios-sweet source](https://github.com/dipa-studio/ios-sweet/tree/fdad0de8dc0e75c5ea84762adc03f7232dc91f19)
is the service/reference baseline: it runs XNU on hardware and supplies neither
this Linux backend nor proof that our service graph works. Permissive security
policies are acceptable; false memory, TLS, IPC and lifecycle success is not.

Current status and exact run evidence belong in [EXPERIMENT.md](EXPERIMENT.md).
