# ARLinux XNU experiment

An experimental Darwin/iOS compatibility environment for ARLinux on
Linux/Android. **No iOS app runs on Linux/Android here yet.** XNU, dyld and iOS userland may be
adapted or replaced with Linux-backed services where application semantics are
preserved. Running an unchanged kernel is not a requirement.

The native ARM64 path runs a real dyld/libSystem console through memory, TLS,
threads, input, normal exit and restart. ARHello 1.0 now enters real UIKit on
Linux but stops at missing workqueue support. The same iOS binary, converted
with PlayCover on a macOS ARM64 runner, completes display, real mouse input,
normal exit and relaunch under LLDB. Linux GUI and Android integration remain
unverified.

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
./build.sh build --mode dyld       # real dyld/libSystem console journey
./build.sh build --mode app        # ARHello 1.0 real UIKit on Linux
./build.sh build --mode playcover  # same iOS binary, PlayCover conversion on macOS
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

## IPA sources for compatibility research

Sources checked on 2026-10-20; these are candidate inputs, not ARLinux
compatibility results. Prefer older non-arm64e ARM64 versions for this experiment.

| Channel | What is available |
| --- | --- |
| [Decrypt IPA Store](https://ipastore.app/) / [decrypt.day](https://decrypt.day/home) | Search by name, App ID or App Store URL; decryption requests and game listings. ipastore.app currently redirects to decrypt.day. |
| [ARM Converter Decrypted App Store](https://armconverter.com/decryptedappstore/) | Decrypted IPA service listed by PlayCover; its web interface requires JavaScript. |
| [IPA Archive index](https://github.com/ipaarchive/ipaarchive.github.io/blob/main/data/urls.json) | Historical package links, including entries labelled decrypted; inspect each package for its version and ARM64 architecture. |

[PlayCover's download guide](https://docs.playcover.io/getting_started/download_ipa)
also lists decrypt.day, ipastore.app and ARM Converter as external IPA sources.
For an actual experiment, record the package URL, app/version, SHA-256, Mach-O
architecture and encryption state. A decrypted IPA alone does not establish
compatibility; this repository does not bundle third-party IPAs.

## Graphics compatibility references

These are candidates for future integration, not implemented Metal support in
ARLinux. Documentation reviewed on 2026-10-20; no local build or device validation
has been performed for these components.

| Project | Relevant work and limits |
| --- | --- |
| [Darling Indium / Iridium](https://github.com/darlinghq/indium) | Experimental Metal-on-Vulkan implementation and AIR-to-SPIR-V shader translation. Requires an outer ABI-compatible Metal implementation; not a drop-in Metal.framework replacement. |
| [metal2vulkan](https://github.com/steelbrain/metal2vulkan) | Alpha metallib/AIR translator and Rust Metal-style runtime on Vulkan. An Objective-C ABI bridge and display integration are still needed; its runtime does not compile MSL source. |
| [darling-lol](https://github.com/lucannez64/darling-lol) | Integration reference with Metal/Indium and other Darling compatibility changes. The author reports macOS game execution on x86-64 Linux; Android ARM64 support is not established. |

Evaluate actual game shaders and complete rendering/presentation flows before
choosing a backend. OpenGL ES games can use a separate GLES adaptation path;
ARM64 code does not imply a Metal dependency.

## Acknowledgements

* [zalexdev/linux-um-arm64](https://github.com/zalexdev/linux-um-arm64): ARM64
  User-Mode Linux, a reference for syscall interception, host memory and Android
  integration. Its Linux kernel port is not an XNU backend.
* [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet): XNU and iOS
  userland bring-up on Redmi Note 10 Pro. Our bare-metal baseline uses its pinned
  patches and upstream build recipe; credit belongs to its authors/contributors.

* [wmww/tawc](https://github.com/wmww/tawc): the tawcroot in-process syscall
  interception and host-libc-independent runtime model informed the native path.
* [PlayCover](https://github.com/PlayCover/PlayCover): a reference for running
  decrypted iOS apps on Apple Silicon Macs, application adaptation and input
  mapping. It relies on macOS's existing iOS runtime; our Linux/Android target
  must also supply the underlying Darwin and framework compatibility.
* [Apple Open Source](https://github.com/apple-oss-distributions): XNU and the
  matching libdispatch kernel firehose implementation; licenses remain upstream.

Preserved forks: [taowen/linux-um-arm64](https://github.com/taowen/linux-um-arm64)
and [taowen/ios-sweet](https://github.com/taowen/ios-sweet). Fixed source commits
and upstream repositories are recorded in `sources.lock.json`.

These projects make the research worth attempting, but cannot simply be combined.
Older ARM64 apps with simple UI are a long-term hypothesis, not demonstrated
compatibility. Preserve upstream attribution and licenses when reusing code.
