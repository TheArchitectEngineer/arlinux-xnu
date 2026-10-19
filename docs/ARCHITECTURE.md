# Userspace XNU execution design

This is the proposed implementation contract, not a report of working software.
The objective remains real ARM64 Mach-O execution through XNU, then dyld, genuine
iOS frameworks and an app. ARM64 / 16 KiB / one virtual CPU remain fixed.

## What the upstreams supply

- [ARM64 UML](https://github.com/zalexdev/linux-um-arm64/tree/32e5e10b81d8882bdaf77b71e4106a941840c750)
  supplies a reference host execution architecture. `arch/um/kernel/skas/mmu.c`
  creates an execution process per address space; `arch/um/os-Linux/skas/process.c`
  handles stops; `arch/um/kernel/skas/syscall.c` invokes real kernel services.
  The ARM64 series extends the existing UML architecture, not a bare-metal
  ARM64 kernel linked to libc. Its Linux syscall ABI is not Darwin's ABI.
- [ios-sweet](https://github.com/dipa-studio/ios-sweet/tree/fdad0de8dc0e75c5ea84762adc03f7232dc91f19)
  supplies a functional XNU/iOS service reference: matching libpthread provider,
  filesystem, real iOS root/shared cache, IOSurface, framebuffer and HID services.
  Its hardware shim boots XNU on the CPU; it is not a Linux host backend.
  Security policy may be permissive, while shared memory, ports and event queues
  still have observable semantics. Preserve this distinction when reusing code.

## Execution-engine decision remains open

The native ptrace design below is a candidate, not an established ABI solution.
Darwin TLS is an input to this decision, not a late kernel feature to stub.

| Candidate | What it would preserve | Cost / unresolved requirement |
| --- | --- | --- |
| Native ARM64 helper | Direct CPU execution; real XNU services | Must adapt Darwin TSD instructions and validate code coverage, clocks, register state and later executable mappings |
| ARM64 instruction engine | Original guest instruction bytes, virtual TPIDRRO state, real XNU services | CPU execution overhead; actual XNU task/map/fault/return integration and framework journey remain unimplemented |

[Unicorn 2.1.4 ARM64 registers](https://github.com/unicorn-engine/unicorn/blob/2.1.4/include/unicorn/arm64.h)
and its
[backend implementation](https://github.com/unicorn-engine/unicorn/blob/2.1.4/qemu/target/arm/unicorn_aarch64.c)
explicitly read/write `TPIDRRO_EL0`. Its `uc_mem_map_ptr` API accepts shared host
backing. This establishes available interfaces, not a running XNU/iOS backend.
An engine could own each user pmap's executable address space and report SVC,
protection faults and CPU state to the same real XNU task/thread dispatcher.
It must not supply substitute Darwin syscall handlers. Instruction emulation
alone cannot fix missing XNU bootstrap, VM, BSD/VFS or Mach services.

No instruction engine has been added as a dependency or selected as the default.
The choice trades native performance against instruction adaptation complexity;
record the user's execution preference before implementing a different backend.

## Ownership decisions

| Owner | Responsibilities |
| --- | --- |
| Native `xnu-host` process | Linux file/memory/process/event operations; one virtual CPU; hosting genuine XNU kernel threads and their stacks |
| XNU | Tasks, threads, VM maps/objects, page allocation, faults, Mach ports/messages, BSD processes/FDs/VFS and exec/exit semantics |
| Native execution helper per user pmap | A separate Linux address space executing guest instructions; controlled mapping commands, signal capture and register restoration |
| Guest | Real ARM64 Mach-O code and, later, genuine dyld/framework binaries; no host libc required inside its execution helper |

A helper starts by exec, without inheriting the host kernel's mapped memory.
Its code, communication and alternate stack must lie outside the guest VM map.
Reserve/validate address ranges before mapping; never silently overwrite a host
mapping. One helper represents an address space, not one permanent host thread
per Darwin thread. XNU chooses the runnable thread; the backend restores its
complete architectural state. FP/SIMD and guest TLS are part of that state.

## Execution and return path

1. Boot genuine kernel initialization against Linux machine services. Use the
   ios-sweet service graph as the functional reference; define the host port at
   machine/device boundaries instead of growing an arbitrary source subset.
2. Mount an image through a file-backed virtual block device and genuine XNU
   filesystem/VFS services. Use the original BSD/Mach-O exec path. Initially the
   image contains repository-owned freestanding programs; Apple files are not
   required for this acceptance. Later use an explicitly supplied matching iOS
   root and shared cache. Do not replace exec with the host shim's Mach-O loader.
3. XNU pmap operations determine helper mappings of shared physical backing.
   A guest fault is captured by the helper and handed back as an event; invoke
   real `vm_fault` in the corresponding XNU thread context. Mapping a page through
   Linux follows XNU's resolution, including protection and copy-on-write.
4. Start with ptrace interception and explicit cancellation of the host syscall;
   seccomp is a later transport optimization. Distinguish helper Linux calls
   from guest Darwin traps. Save x0-x30, SP, PC, PSTATE, FP/SIMD and TLS before
   changing cancellation registers. Darwin uses x16 and its own error convention;
   Linux's x8 syscall table must never service an intercepted Darwin request.
5. Populate the real thread's saved state, then enter the original Mach/BSD
   dispatch (`arm64/sleh.c`, `bsd/dev/arm/systemcalls.c`). Their return/continuation
   paths must reach the host execution backend with the resulting saved state.
   A blocked syscall stays in real XNU scheduling; it is not a synchronous C RPC
   that is allowed to report success before the thread actually completes.
6. The backend updates the stopped helper and resumes that thread. Real XNU
   process exit and parent wait status establish guest exit; terminating a Linux
   helper is resource cleanup, not evidence of Darwin exit semantics.

Kernel access to guest buffers is an explicit unresolved boundary. The intended
single-CPU design mirrors the active user pmap into an owned host address window,
with user pages prefaulted through real XNU before copyin/out. Activation must
replace the previous map, retain protection/COW semantics and integrate with the
original copy validation. Existing direct-copy leaves alone do not provide this.
If this cannot support the actual VM/copy paths, revise it before implementing
more dependent services; do not bypass user-map checks with ptrace writes.

## Kernel timer interrupt boundary

The first host clock transport uses one CLOCK_MONOTONIC timerfd with real
one-shot programming, remaining-time queries and event consumption. Kernel
interrupt-enable boundaries service pending events through original `sleh_fiq`,
`rtclock_intr` and timer queues. This is cooperative kernel interrupt delivery;
it does not asynchronously preempt arbitrary Linux/native C instructions.
The one-CPU implementation pins preemption while on the virtual interrupt stack,
restores CPU interrupt state, then handles urgent ASTs on the kernel thread stack.
Its software boundary frame supplies diagnostic PC/SP/frame pointers; it is not
a captured native exception or a guest register set for architectural return.

Host idle now resynchronizes original XNU deadlines and polls the timerfd plus
an eventfd wake transport. Polling leaves clock events unread for original FIQ;
return enables virtual interrupts and lets original queues/ASTs select work.
Wake hints are coalesced, including eager deferred hints. Native syscall output
objects use host TLS to avoid writing via Linux user-copy into reference-tracked
XNU kernel stacks. This idle path still needs runtime validation.

Guest execution slices/stops must still feed this event source and deliver
guest-aware interrupt state. Host I/O callbacks are not implemented by posting
a wake token. Kernel delivery, accounting, callback expiry and restoration
still require the real runtime journey. Do not substitute a timerfd probe or
Linux sleep for a blocked XNU thread's scheduler/timer wakeup.

## Native execution constraint: Darwin TLS

Apple's `libsyscall/os/tsd.h::_os_tsd_get_base` directly reads `TPIDRRO_EL0`.
XNU `arm64/pcb.c::machine_thread_set_tsd_base` normally installs that register.
Linux ARM64's `NT_ARM_TLS` and the UML backend control `TPIDR_EL0`; native 64-bit
Linux does not offer that operation as Darwin TPIDRRO installation. Saving
`cthread_self` therefore does not make pthread/errno/TSD work.

An EL0 read of this register is legal, so ordinary SIGILL/syscall interception
cannot virtualize it. Relevant primary sources are
[Apple TSD access](https://github.com/apple-oss-distributions/xnu/blob/1b191cb58250d0705d8a51287127505aa4bc0789/libsyscall/os/tsd.h)
and [Linux TLS switching](https://github.com/torvalds/linux/blob/v6.12/arch/arm64/kernel/process.c).

This is a prerequisite for framework execution. The candidate is an explicit
instruction adaptation layer for non-arm64e executable images/shared-cache code,
redirecting Darwin TSD reads to helper-owned `TPIDR_EL0`. It is not implemented or
validated. Establish instruction identity and executable coverage, preserve
original/adapted hashes and instruction-cache coherence, and cover later mappings;
blind byte-pattern replacement is insufficient. Do not claim unmodified native
iOS instruction execution. If this approach cannot preserve the real binaries'
semantics, instruction translation or a revised host assumption is required.
Until then USERMODE TSD installation returns `KERN_NOT_SUPPORTED`, not success.

Also audit direct counter reads/commpage against the advertised timebase, reserved
register conventions, FP/SIMD, atomics and address bounds. A Linux nanosecond
kernel clock alone does not prove that Apple's userspace fast paths agree.

## Acceptance and implementation order

First establish the execution/TLS/address-space contract above. Then connect
kernel bootstrap, genuine task/process setup and the host execution helper as
one path. The first real guest journey uses a supervisor plus a child program:
open/write a console FD through BSD, allocate/touch/free memory through XNU,
exit with a known status, and observe that status through the supervisor's real
BSD wait. Keep PID 1 alive; its exit is not an ordinary application exit.

Only this journey advances the execution gate. Subsequent gates add dyld and
shared-cache loading, matching pthread, Mach/XPC services, IOSurface/display/input
and an app. Permissive security/hardware stand-ins are allowed where semantics
permit; successful calls with incorrect memory, TLS, IPC or lifecycle results
are not a substitute. Keep current compilation, raw diagnostics and shim results
as historical evidence. All compilation/execution continues through `build.sh`
on GitHub runners, with exact revisions, hashes and failure logs.
