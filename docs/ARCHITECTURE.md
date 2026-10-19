# Darwin/iOS compatibility architecture

The target is a real iOS application journey on ARLinux, not faithful execution
of an unmodified kernel. ARM64, 16 KiB guest pages, one advertised CPU and
non-arm64e code define the initial scope. Compilation and execution occur only
on GitHub runners through the root build.sh entry.

## Native execution first

macho-native loads a bounded static Mach-O image. Its executable instruction
sections are adapted in private memory: Darwin TPIDRRO_EL0 reads use Linux's
writable TPIDR_EL0; original Darwin TPIDR_EL0 CPU metadata reads return CPU zero.
The file is unchanged. This has executed our console, the pinned Apple dyld and system-library
initializers in the pinned non-arm64e cache. It does not establish arbitrary
instruction/data mixtures, later executable mappings, JIT or arm64e.

Darwin platform set/get-TSD calls maintain actual guest TLS. Borrowing tawcroot's
approach, the SIGSYS runtime never calls host libc after guest TLS takes over.
Raw Linux syscalls have one exact allowed instruction site; other syscalls trap.
Linux signal return is allowed separately. This is an ABI transport, not a
security sandbox. Unknown Darwin calls return ENOSYS; unsupported executable,
file or fixed mapping modes return errors rather than fake support. The
read-only firmware filesystem supports the loader's actual open/stat/read
operations, with Darwin metadata layouts translated from Linux statx.

The Linux kernel supplies real anonymous 16 KiB-aligned mappings, protection,
read/write, fork/COW, wait and process exit. The kernel validates user buffers.
The runtime converts Linux error results to Darwin's carry/positive-errno ABI.
No XNU kernel is linked into this native path. The historical ptrace hello shim
remains an explicitly separate host diagnostic, never app acceptance.

Primary sources: [tawcroot runtime](https://github.com/taowen/tawc/blob/bee5ae2d216f6283a8a6e6b60f18e2a7b6d8451d/notes/tawcroot/seccomp-filter.md),
[Apple TSD](https://github.com/apple-oss-distributions/xnu/blob/1b191cb58250d0705d8a51287127505aa4bc0789/libsyscall/os/tsd.h)
and [platform calls](https://github.com/apple-oss-distributions/xnu/blob/1b191cb58250d0705d8a51287127505aa4bc0789/osfmk/arm64/machine_routines.c).

## ABI definitions and adapter policy

`src/darwin-abi.h` owns guest syscall/trap IDs, errno and MIG values, flags,
commpage offsets and wire structures. Its source references identify the Apple
interfaces; named fields replace pthread-registration byte offsets. Host errno
uses Linux headers and is explicitly translated, even where values coincide.
`src/darwin-machine.h` owns our address layout, stack limits and local port-name
allocation policy. Add ABI values to the former and adapter choices to the
latter; keep interface-specific meanings distinct when numbers happen to match.

## Guest address geometry

All guest heap/stack allocations use disjoint MAP_FIXED_NOREPLACE ranges below
the legacy 36-bit iOS commpage, with 16 KiB guest pages and requested power-of-two
alignment preserved. Linux automatic mappings can exceed the address bits packed
into real Objective-C metadata/isa fields. Such host addresses are not suitable
guest pointers even on the same ARM64 CPU. Host-only signal/runtime memory can
remain at normal Linux addresses. See Apple's [packed isa definition](https://github.com/apple-opensource/objc4/blob/master/runtime/isa.h).

## Genuine shared cache

Before guest entry, the loader maps the selected firmware's legacy ARM64 cache
into private host mappings at its unslid addresses. It decodes version-2 pointer
chains, adapts marked executable sections once per unique image, flushes code
and restores final protections. Unknown formats fail. Only then does the runtime
expose the real mapped base through shared_region_check. Apple authentication
is explicitly disabled as a loading policy; verified firmware SHA-256 and
mapping/relocation state are recorded independently. This replaces the kernel's
cache-loading responsibility rather than faking its completion.

Real libSystem/pthread/libc initializers now execute. The observed Host
priority query, Clock service lookup and semaphore creation have passed their
clients' checks. The true dynamically linked C main now executes real libSystem
write/malloc/free calls and real pthread create/join, input, exit and relaunch. An empty task Bootstrap
slot is reported as NULL, not as a working launchd/XPC service. See docs/EXPERIMENT.md for the exact source/run/hash and
remaining acceptance gaps. Cache symbolication preserves actual image and nearest-symbol
metadata from the same input without uploading Apple executable contents.

## Initial Mach services

Process-local reply rights have distinct names and Linux SOCK_SEQPACKET queues.
The observed combined send/receive exchanges enqueue a MIG response and then
receive it through the real queue. Unknown service routines get a MIG error;
unknown destination names get transport errors. Host priority ranges describe
the Darwin ABI policy, not implemented realtime scheduling. Host and Clock send
references are tracked; Clock service replies transfer a registered service
right using the normal complex descriptor format. Clock time uses Linux clocks.

The observed semaphore-create request allocates an actual Linux eventfd counter.
Signal/wait/timed-wait support is implemented, but it has not passed a complete
multithreaded user journey; contended FIFO ordering and concurrent namespace
mutation remain unverified. General descriptors, out-of-line memory, port sets,
notifications, arbitrary rights transfer and cross-process namespace semantics
are still outside the implemented transport. These limitations remain failures,
not empty successful replies.

## Preserve behavior, permit implementation changes

Reuse and adapt XNU algorithms or services where they shorten the application
path, especially Mach IPC and Darwin thread/exception semantics. Linux-backed
replacement is permitted. Memory lifetime/protection, synchronization, message
ownership, return values and error behavior must meet application requirements.
Hardware or privilege policies can be simplified when that is sufficient.
Returning success for an operation whose missing effect breaks the app is not
an implementation.

Full-system ios-sweet/XNU runs are behavioral references, not proof of ARLinux
compatibility. The active application path uses native ARM64 execution; its
build does not depend on a CPU instruction-emulation engine.

## Application-driven sequence

1. Run the native interactive Mach-O console end to end: real VM/COW and TLS,
   user input, normal exit and an independent second launch.
2. Add Mach-O process startup/dyld/system-library behavior required by a chosen
   real userland. Extend rewriting when code is mapped, not only at initial load.
3. Provide the services required by genuine iOS frameworks; connect display and
   input to ARLinux. Preserve licenses and do not distribute proprietary assets.
4. Record a named/versioned simple iOS App's launch, display, interaction, exit
   and restart on the actual ARLinux integration.

The console program is an intermediate executable, not the final iOS App.
Do not turn each missing symbol into an isolated probe or count a green build
as a completed application stage. Preserve exact source, hashes, environment,
logs and images for both successful and failed journeys.
