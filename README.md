# ARLinux XNU experiment

Research toward running XNU as a userspace kernel on Linux/Android.
**No iOS app runs through XNU here yet.** The current Mach-O demo uses a small
Darwin syscall shim; actual XNU kernel integration is still blocked at compilation.

Scope: ARM64, 16 KiB guest pages, one virtual CPU, non-arm64e binaries and a
software framebuffer. No 32-bit apps or Metal support. Proprietary Apple
frameworks and firmware are not included.

## Run an experiment

The sole workstation entry is `./build.sh`, requiring Bash, Python 3 and an
authenticated GitHub CLI. All compilation and execution use GitHub-hosted runners.

```bash
./build.sh build --mode host       # Linux host probe + freestanding Mach-O
./build.sh build --mode kernel     # actual XNU compilation; currently failing
./build.sh build --mode baseline   # ios-sweet bare-metal build
./build.sh status --run-id RUN_ID
./build.sh download --run-id RUN_ID
```

[Experiment contract and current blocker](docs/EXPERIMENT.md) explains what each
result proves, how to resume work and how evidence is retained. Dependencies are
pinned in [sources.lock.json](sources.lock.json); run logs and artifacts live in
[GitHub Actions](https://github.com/taowen/arlinux-xnu/actions/workflows/experiment.yml).

## Acknowledgements

* [zalexdev/linux-um-arm64](https://github.com/zalexdev/linux-um-arm64): ARM64
  User-Mode Linux, a reference for syscall interception, host memory and Android
  integration. Its Linux kernel port is not an XNU backend.
* [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet): XNU and iOS
  userland bring-up on Redmi Note 10 Pro. Our bare-metal baseline uses its pinned
  patches and upstream build recipe; credit belongs to its authors/contributors.

These projects make the research worth attempting, but cannot simply be combined.
Older ARM64 apps with simple UI are a long-term hypothesis, not demonstrated
compatibility. Preserve upstream attribution and licenses when reusing code.
