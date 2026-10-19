# ARLinux XNU experiment

Research toward running XNU as a userspace kernel on Linux/Android.
**No iOS app runs through XNU here yet.** The current Mach-O demo uses a small
Darwin syscall shim. Historical host diagnostics executed limited XNU startup
and shared mappings. The real VM executable still fails linkage; VM
initialization, task creation and guest execution through XNU are unverified.
The native-XNU/emulated-user CPU boundary is an unvalidated candidate. A
full-system reference now attempts the same real process journey before further
userspace machine-layer work; it does not establish userspace XNU compatibility.

Scope: ARM64, 16 KiB guest pages, one virtual CPU, non-arm64e binaries and a
software framebuffer. No 32-bit apps or Metal support. Proprietary Apple
frameworks and firmware are not included.

## Run an experiment

The sole workstation entry is `./build.sh`, requiring Bash, Python 3 and an
authenticated GitHub CLI. All compilation and execution use GitHub-hosted runners.

```bash
./build.sh build --mode host       # explicit historical host/shim diagnostics
./build.sh build                   # default: original XNU kernel bootstrap attempt
./build.sh build --mode baseline   # build and boot ios-sweet full-system reference
./build.sh status --run-id RUN_ID
./build.sh download --run-id RUN_ID
```

[Execution design and unresolved ABI constraints](docs/ARCHITECTURE.md) defines
the host/kernel/guest boundary.

[Experiment contract and current blocker](docs/EXPERIMENT.md) explains what each
result proves, how to resume work and how evidence is retained. Dependencies are
pinned in [sources.lock.json](sources.lock.json); run logs and artifacts live in
[GitHub Actions](https://github.com/taowen/arlinux-xnu/actions/workflows/experiment.yml).

Standalone early diagnostics require `./build.sh build --diagnostics`; their
results do not establish guest execution.

## Acknowledgements

* [zalexdev/linux-um-arm64](https://github.com/zalexdev/linux-um-arm64): ARM64
  User-Mode Linux, a reference for syscall interception, host memory and Android
  integration. Its Linux kernel port is not an XNU backend.
* [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet): XNU and iOS
  userland bring-up on Redmi Note 10 Pro. Our bare-metal baseline uses its pinned
  patches and upstream build recipe; credit belongs to its authors/contributors.

* [Unicorn Engine](https://github.com/unicorn-engine/unicorn): ARM64 instruction
  execution. [Our fork](https://github.com/taowen/unicorn) exposes pending CPU
  exception metadata; XNU retains VM and Mach/BSD ownership.

Preserved forks: [taowen/linux-um-arm64](https://github.com/taowen/linux-um-arm64)
and [taowen/ios-sweet](https://github.com/taowen/ios-sweet). Fixed source commits
and upstream repositories are recorded in `sources.lock.json`.

These projects make the research worth attempting, but cannot simply be combined.
Older ARM64 apps with simple UI are a long-term hypothesis, not demonstrated
compatibility. Preserve upstream attribution and licenses when reusing code.
