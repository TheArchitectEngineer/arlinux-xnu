# ARLinux XNU experiment

Research toward a userspace XNU backend on Linux/Android. **Not a working iOS emulator.**

Initial scope: ARM64, 16 KiB guest pages, one virtual CPU, non-arm64e binaries,
software framebuffer. No 32-bit guest, Apple GPU driver, Metal translation or IPA support.
Apple proprietary firmware/frameworks are not included or downloaded by this project.

The sole workstation entry is `./build.ps1`; Linux/macOS compilation runs only on GitHub-hosted runners.

```powershell
./build.ps1 build
./build.ps1 status
./build.ps1 download
```

Two independent experiments run in one dispatched workflow:

* `xnu-baseline`: reproduce the pinned ios-sweet ARM64 bare-metal XNU kernel using its build recipe. A successful Mach-O kernel build is **not** a userspace XNU port or iOS app acceptance.
* `host-substrate`: compile and run a Linux ARM64 diagnostic probe. It verifies ptrace interception of a real ARM64 `svc #0x80` instruction, Darwin-style x16 dispatch, suppression of the Linux syscall, return register/flags, and 16 KiB logical-page shared mapping/protection on the host. This is a host mechanism probe, **not XNU code**, a Mach-O loader, or a compatibility claim.

The Linux probe executes on a standard ARM64 runner. Android app-UID/SELinux behavior remains unverified.
Guest 16 KiB mappings are composed from host pages when the host page size divides 16 KiB;
this initial scope rejects larger host pages. XNU VM/COW and Mach memory-object semantics are not implemented.

Source pins are in `sources.lock.json`. `ios-sweet` supplies the baseline XNU port;
`linux-um-arm64` is an architectural reference, not linked into XNU.
Outputs retain runner environment, source pins, SHA-256, executable descriptions and logs.
See Actions for the actual outcome; compilation alone does not establish app compatibility.
