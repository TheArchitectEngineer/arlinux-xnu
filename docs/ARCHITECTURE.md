# Darwin/iOS compatibility architecture

The target is a real iOS application journey on ARLinux, not faithful execution
of an unmodified kernel. ARM64, 16 KiB guest pages, one advertised CPU and
non-arm64e code define the initial scope. Compilation and execution occur only
on GitHub runners through the root build.sh entry.

## Native execution first

macho-native loads a bounded static Mach-O image. Its executable instruction
sections are adapted in private memory: Darwin TPIDRRO_EL0 reads use Linux's
writable TPIDR_EL0; original Darwin TPIDR_EL0 CPU metadata reads return CPU zero.
The file is unchanged. This currently covers our relocation-free console code,
not arbitrary instruction/data mixtures, dylibs, shared caches, JIT or arm64e.
A general rewriting policy must address those before claiming that scope.

Darwin platform set/get-TSD calls maintain actual guest TLS. Borrowing tawcroot's
approach, the SIGSYS runtime never calls host libc after guest TLS takes over.
Raw Linux syscalls have one exact allowed instruction site; other syscalls trap.
Linux signal return is allowed separately. This is an ABI transport, not a
security sandbox. Unknown Darwin calls return ENOSYS; unsupported executable,
file or fixed mapping modes return errors rather than fake support.

The Linux kernel supplies real anonymous 16 KiB-aligned mappings, protection,
read/write, fork/COW, wait and process exit. The kernel validates user buffers.
The runtime converts Linux error results to Darwin's carry/positive-errno ABI.
No XNU kernel is linked into this native path. The historical ptrace hello shim
remains an explicitly separate host diagnostic, never app acceptance.

Primary sources: [tawcroot runtime](https://github.com/taowen/tawc/blob/bee5ae2d216f6283a8a6e6b60f18e2a7b6d8451d/notes/tawcroot/seccomp-filter.md),
[Apple TSD](https://github.com/apple-oss-distributions/xnu/blob/1b191cb58250d0705d8a51287127505aa4bc0789/libsyscall/os/tsd.h)
and [platform calls](https://github.com/apple-oss-distributions/xnu/blob/1b191cb58250d0705d8a51287127505aa4bc0789/osfmk/arm64/machine_routines.c).

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
