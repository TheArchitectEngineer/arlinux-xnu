# ARLinux XNU experiment

An experimental Darwin/iOS compatibility environment for ARLinux on
Linux/Android. **No iOS app runs here yet.** XNU, dyld and iOS userland may be
adapted or replaced with Linux-backed services where application semantics are
preserved. Running an unchanged kernel is not a requirement.

The current native ARM64 path adapts static Mach-O TLS reads and intercepts
Darwin calls inside the process without depending on host libc TLS. Its first
journey is an interactive console program with memory/COW, per-process TLS,
input, normal exit and restart. [Runner verification passed](https://github.com/taowen/arlinux-xnu/actions/runs/39320163457). It is not dyld,
a framework-based app or a working Android integration.

A [full-system XNU reference](https://github.com/taowen/arlinux-xnu/actions/runs/39311945725)
provides a behavior comparison. Compilation is not application compatibility.

Scope: ARM64, 16 KiB guest pages, one virtual CPU, non-arm64e binaries and a
software framebuffer. No 32-bit apps or Metal support. Proprietary Apple
frameworks and firmware are not included.

## Run an experiment

The sole workstation entry is `./build.sh`, requiring Bash, Python 3 and an
authenticated GitHub CLI. All compilation and execution use GitHub-hosted runners.

```bash
./build.sh build --mode host       # explicit historical host/shim diagnostics
./build.sh build                   # default: native Darwin console journey
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

## Acknowledgements

* [zalexdev/linux-um-arm64](https://github.com/zalexdev/linux-um-arm64): ARM64
  User-Mode Linux, a reference for syscall interception, host memory and Android
  integration. Its Linux kernel port is not an XNU backend.
* [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet): XNU and iOS
  userland bring-up on Redmi Note 10 Pro. Our bare-metal baseline uses its pinned
  patches and upstream build recipe; credit belongs to its authors/contributors.

* [wmww/tawc](https://github.com/wmww/tawc): the tawcroot in-process syscall
  interception and host-libc-independent runtime model informed the native path.
* [Apple Open Source](https://github.com/apple-oss-distributions): XNU and the
  matching libdispatch kernel firehose implementation; licenses remain upstream.

Preserved forks: [taowen/linux-um-arm64](https://github.com/taowen/linux-um-arm64)
and [taowen/ios-sweet](https://github.com/taowen/ios-sweet). Fixed source commits
and upstream repositories are recorded in `sources.lock.json`.

These projects make the research worth attempting, but cannot simply be combined.
Older ARM64 apps with simple UI are a long-term hypothesis, not demonstrated
compatibility. Preserve upstream attribution and licenses when reusing code.
