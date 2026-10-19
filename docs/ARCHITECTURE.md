# Userspace XNU execution design

This is an implementation contract, not working iOS compatibility. No genuine
XNU guest process has run. ARM64, 16 KiB guest pages and one virtual CPU remain
fixed. All compilation and execution use `build.sh` on GitHub runners.

## Selected execution boundary

Use Unicorn 2.1.4's ARM64 instruction engine for guest user instructions. The
exact unmodified source is a submodule and is recorded in `sources.lock.json`.
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
to the protected physical aperture; this needs real fault/TCG validation. Task
ledger accounting, wiring, replacement and COW remain
unfinished. Native aliases and XNU pmap ownership remain runtime-unverified; they do not
yet connect user insertion or a runnable thread. User physical/resident queries
now read their engine metadata. Empty-range removal verifies that no mappings
exist; nonempty removal is rejected until ledger debit is connected.

The machine return path must transfer the actual thread saved state, run its
engine, and translate the reported event into original ARM exception handling:
Mach/BSD SVC dispatch or VM fault resolution in that thread's kernel context.
Do not confuse Unicorn exception numbers with ARM ESR values. Preserve the
correct faulting/resume PC, error convention, carry flags and return state.
Thread blocking/continuations stay in the original scheduler. A time slice must
service pending kernel clock events before the next scheduled user execution.

Kernel copyin/out must resolve the actual user map and its protections/COW.
An engine memory-read/write API must not bypass XNU's copy validation. Guest
kernel shared aliases and native Linux syscall buffers need explicit handling;
Linux cannot fault in a protected buffer through the XNU SIGSEGV handler.

The existing timerfd/eventfd transport provides cooperative kernel safe-point
interrupt delivery, not asynchronous native C preemption. Its original XNU
FIQ/timer/idle paths remain unexecuted. Guest direct counter reads and commpage
must agree with the advertised XNU timebase; selecting an engine does not itself
establish that contract. Engine operations switch to a guarded 1 MiB native CPU stack allocated before
XNU startup, avoiding libc/TCG execution on XNU kernel thread stacks. Nested
entry is rejected; no engine callback may schedule XNU. Stack switching and
the actual guest path still require runtime validation.

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
