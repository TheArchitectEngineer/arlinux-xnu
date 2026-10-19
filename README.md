# ARLinux XNU experiment

Research toward a userspace XNU backend on Linux/Android. **Not a working iOS emulator.**

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

Two independent experiments run in one dispatched workflow:

* `xnu-baseline`: reproduce the pinned ios-sweet ARM64 bare-metal XNU kernel using its build recipe. A successful Mach-O kernel build is **not** a userspace XNU port or iOS app acceptance.
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
