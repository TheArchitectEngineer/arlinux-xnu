# ARLinux XNU experiment

Research toward a userspace XNU backend on Linux/Android. **Not a working iOS emulator.**

## Acknowledgements and feasibility

This experiment is inspired by two independent projects. Credit for their
existing kernel ports, engineering work and published evidence belongs to their
respective authors and contributors:

* **[zalexdev/linux-um-arm64](https://github.com/zalexdev/linux-um-arm64)**:
  the ARM64 User-Mode Linux port demonstrates running a Linux kernel as a
  userspace process, including on Android without root or KVM. Its syscall
  interception, address-space management, signal/register handling and Bionic
  integration are important references for our proposed XNU host backend.
  It is a Linux kernel port, not a userspace XNU port or an iOS compatibility layer.
* **[dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet)**:
  the XNU port to Redmi Note 10 Pro demonstrates Apple's real iOS userland and
  software-rendered interface on non-Apple hardware. Its XNU/IOKit adaptations,
  IOSurface/display/HID services and bring-up diagnostics inform this experiment.
  The bare-metal XNU baseline here is built directly from its pinned patches and
  upstream build recipe; it is not an original kernel port by this project.

Our hypothesis is that these two results make a userspace XNU experiment worth
attempting: reuse real XNU logic and eventually compatible iOS frameworks while
providing Linux/Android host backends. They cannot simply be combined: UML is
Linux-specific, and ios-sweet boots XNU on bare metal rather than inside Android.
Neither project establishes that this repository can run iOS apps.

Some older **ARM64, non-arm64e, offline apps with simple software-renderable UI**
are a plausible long-term target, not a current compatibility promise. The
16 KiB-only guest scope reduces page-size variation; it does not implement XNU
VM, Mach IPC, threads, exceptions, dyld, UIKit or an Apple GPU interface.
32-bit legacy iOS apps are outside this experiment's current scope.

The current Linux Mach-O demo uses our small Darwin syscall shim; real XNU
integration and Android app-sandbox execution remain outstanding. No third-party
iOS app has been validated. Source commits are recorded in `sources.lock.json`;
upstream licenses and attribution must be preserved for reused code. Apple
proprietary frameworks and firmware are not distributed by this repository.

Initial scope: ARM64, 16 KiB guest pages, one virtual CPU, non-arm64e binaries,
software framebuffer. No 32-bit guest, Apple GPU driver, Metal translation or IPA support.
Apple proprietary firmware/frameworks are not included or downloaded by this project.

The sole workstation entry is `./build.sh`. Use Bash, Python 3 and authenticated
GitHub CLI (`gh`). Linux/macOS compilation runs only on GitHub-hosted runners;
the local entry only dispatches jobs and retrieves their status/artifacts.

```bash
./build.sh build
./build.sh status
./build.sh download --run-id RUN_ID
```

To run only one experiment or inspect an exact run:

```bash
./build.sh build --mode host
./build.sh build --mode baseline
./build.sh status --run-id RUN_ID
```

Independent experiments run in one dispatched workflow:

* `xnu-baseline`: reproduce the pinned ios-sweet ARM64 bare-metal XNU kernel using its build recipe. A successful Mach-O kernel build is **not** a userspace XNU port or iOS app acceptance.
* `macho-userland`: build a freestanding ARM64 Mach-O executable using Linux Clang/LLD, map its real segments with 16 KiB granularity and run its ARM64 instructions directly. A small host shim handles Darwin write/getpid/exit and the carry/error ABI; no XNU kernel is linked yet. Dynamic loading, dyld, Foundation and UIKit remain unsupported.
* `host-substrate`: compile and run a Linux ARM64 diagnostic probe. It verifies ptrace interception of a real ARM64 `svc #0x80` instruction, Darwin-style x16 dispatch, suppression of a Linux close(fd) verified through its absent side effect, return register/flags, and 16 KiB logical-page shared mapping/protection on the host. This is a host mechanism probe, **not XNU code**, a Mach-O loader, or a compatibility claim.

The Linux probe executes on a standard ARM64 runner. Android app-UID/SELinux behavior remains unverified.
Guest 16 KiB mappings are composed from host pages when the host page size divides 16 KiB;
this initial scope rejects larger host pages. XNU VM/COW and Mach memory-object semantics are not implemented.

Source pins are in `sources.lock.json`. `ios-sweet` supplies the baseline XNU port;
`linux-um-arm64` is an architectural reference, not linked into XNU.
Outputs retain runner environment, source pins, SHA-256, executable descriptions and logs.
See Actions for the actual outcome; compilation alone does not establish app compatibility.

## Hosted evidence (2026-10-19)

[ARM64 host-substrate run 39212532039](https://github.com/taowen/arlinux-xnu/actions/runs/39212532039)
passed on ubuntu-24.04-arm (4 KiB host pages): 16 KiB shared mapping and write
protection, Darwin SVC 0x80 interception, Linux syscall suppression, and register
return. Source commit: `39a5c50`. A repeat with explicit carry-flag initialization
also passed in [run 39212664017](https://github.com/taowen/arlinux-xnu/actions/runs/39212664017).
Artifacts contain the actual ELF and logs.
The first attempt incorrectly expected ENOSYS after ARM64 syscall cancellation;
the corrected diagnostic verifies the observable absence of close(fd)'s side effect.

This does not validate Android app sandbox behavior, XNU VM, Mach IPC, dyld,
UIKit, or third-party iOS apps. Those remain subsequent milestones.

The full [run 39213188337](https://github.com/taowen/arlinux-xnu/actions/runs/39213188337)
passed both jobs at commit `0c2d146`, dispatched and downloaded through `build.sh`.
The baseline produced `kernel.development.sweet`, a Mach-O 64-bit ARM64 executable.
SHA-256: `7157827c22a73d122be5d48664950a3dd38ec7ac381ca04075992ac083fc20ad`.
Downloaded artifact bytes matched the runner's SHA256SUMS. The host-substrate
probe also passed in the same workflow. This establishes hosted build feasibility
and selected Linux host mechanisms, **not a userspace XNU kernel or iOS app runtime**.
The baseline is not claimed to be bit-for-bit reproducible; upstream toolchain
provisioning and kernel build metadata require further locking/normalization.

## First executable milestone

`guests/hello.S` is assembled into an actual ARM64 Mach-O executable on the
Linux ARM64 runner using LLVM 18 without an Apple SDK. `src/macho-host.c` maps
its load commands, zero-fills segment tails, applies protections, supplies a
stack, and enters the guest. The parent intercepts Darwin SVC 0x80 and services
write/getpid/exit. Guest exit currently terminates the tracee from the supervisor;
it is not XNU task teardown. Unknown syscall numbers return Darwin ENOSYS.
The fixture checks output length, EBADF with carry set, getpid and exit status.

This is a custom Darwin host shim, not execution through XNU. Kernel integration
is still outstanding. Use only the repository-owned freestanding diagnostic:
the loader is not a security sandbox or a general Mach-O/dyld implementation.
It rejects imported dylibs, undefined symbols and nonempty bind/rebase streams.
LLD emits a dyld path declaration even for this freestanding executable; the host
records that metadata and enters LC_MAIN directly. It does not implement or run dyld.
No proprietary iOS programs are included. `build --mode host` runs both Linux
experiments and leaves the unrelated bare-metal baseline disabled.

### Mach-O execution evidence (2026-10-19)

[Run 39214608040](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040)
passed at commit `082cf9c`. Both the Linux host-substrate and Mach-O userland jobs
passed. The bare-metal baseline was intentionally skipped because its inputs
were unchanged. Native ARM64 instructions from `hello.macho` produced:

```text
Hello from ARM64 Mach-O on Linux (Darwin ABI)
DARWIN_GUEST_EXIT status=0 syscalls=4 (host shim, not XNU)
```

The guest checks the write result, EBADF/carry error convention and a positive
process identity before requesting Darwin exit(0). The supervisor maps two real
Mach-O segments with a 16 KiB mapping granularity. Downloaded ELF and Mach-O bytes
matched the runner's SHA256SUMS. Artifact `linux-arm64-macho-userland` contains
both executables, the object file, load commands, environment, output and trace.
Mach-O SHA-256: `5a8e61cb7e37acfbf7776f9cbc38aa6e6ee50b71625a71cd99f4869da5fbad46`.

The next kernel milestone is implementing XNU's host memory/task/exception
backend and routing guest calls into actual XNU code. This shim is not that
backend. Dynamic loading, real system libraries/frameworks, threads, guest VM
allocation/COW and Android app-sandbox execution remain unimplemented/unverified.
