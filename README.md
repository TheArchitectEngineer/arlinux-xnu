# ARLinux XNU experiment

An experimental Darwin/iOS compatibility environment for ARLinux on
Linux/Android. **Console and Foundation journeys pass on Linux; no complete UIKit GUI journey
has passed on Linux/Android yet.** XNU, dyld and iOS userland may be
adapted or replaced with Linux-backed services where application semantics are
preserved. Running an unchanged kernel is not a requirement.

The source-owned [app collection](docs/APPS.md) drives development. Each small app
owns its sources, build declaration and short journeys. There is no all-purpose
workbench, freestanding syscall probe, alternate IPC backend selector or separate
kernel-boot experiment in the build. Required failures remain red.

## Build and observe

The sole workstation entry is `./build.sh`; Bash, Python 3 and authenticated
GitHub CLI are required. Compilation and execution happen only on hosted runners.

```bash
./build.sh build                         # entire app collection plus two-app scenarios
./build.sh build --app counter           # explicitly partial feedback
./build.sh build --app counter --reference # same input and journey on Linux and Apple host
./build.sh status --run-id RUN_ID
./build.sh report --run-id RUN_ID
./build.sh download --run-id RUN_ID --artifact ios-app-results-linux
```

One macOS Intel job uses Xcode 16.4's real iPhoneOS SDK to cross-compile ARM64
C, Objective-C and Swift apps. Linux ARM64 builds the production runtime with ccache, verifies
the exact same-run app products and prepares the pinned iOS 16.7.16 userland.
Each independent journey gets a fresh shared Mach session and fresh app state;
relaunch keeps only that journey's state. Concurrency scenarios alone share
multiple app processes within one journey. Failures do not block unrelated apps.
Selected subsets are reported as partial; they cannot certify the whole suite.

The optional Apple-host reference reuses the same input binaries and journey
manifests with pinned PlayCover conversion and real host desktop input. It is a
behavior comparison, not Linux or Android acceptance. No firmware, Apple SDK,
framework or extracted Apple executable is uploaded. Retained app binaries and
platform adapters are built from this repository's source. Downloaded products
are checked against their same-source/run/attempt receipt and file hashes.

Scope: ARM64, 16 KiB guest pages, one actual scheduled/advertised CPU, non-arm64e,
selected iOS 16.7.16 frameworks, software framebuffer. Multiple independent apps,
real service lifetimes, input, persistence and Android acceptance remain required.
See [architecture](docs/ARCHITECTURE.md), [diagnostics](docs/DIAGNOSTICS.md) and
[current evidence](docs/EXPERIMENT.md). Unsupported behavior fails explicitly.

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
  userland bring-up on Redmi Note 10 Pro. Its bring-up and upstream build recipe informed our historical full-system
  baseline, now retired from the build. Credit belongs to its authors/contributors.

* [wmww/tawc](https://github.com/wmww/tawc): the tawcroot in-process syscall
  interception and host-libc-independent runtime model informed the native path.
* [PlayCover](https://github.com/PlayCover/PlayCover): a reference for running
  decrypted iOS apps on Apple Silicon Macs, application adaptation and input
  mapping. It relies on macOS's existing iOS runtime; our Linux/Android target
  must also supply the underlying Darwin and framework compatibility.
* [Darlingserver](https://github.com/darlinghq/darlingserver): a reference for
  reusing XNU Mach IPC in a Linux userspace server and integrating Mach-port
  kqueue events with epoll.
  [VibeDarling's community fork](https://github.com/VibeDarling/darlingserver)
  supplies the ARM64 build/clock path used by the integrated Darling backend.
  The runtime is now an [in-tree hard fork](components/darling/README.md): server,
  required XNU support, MIG and headers are maintained here together with our
  native client. Import revisions live in components/darling/ORIGINS.json;
  builds do not fetch a separate Darling fork. Embedding and guest geometry
  changes are included. Copyright notices and GPL/source licenses are retained. Historical console evidence exists on this backend;
  iOS UI and Android compatibility remain unverified.
* [Apple Open Source](https://github.com/apple-oss-distributions): XNU and the
  matching libdispatch kernel firehose implementation; licenses remain upstream.

Preserved forks: [taowen/linux-um-arm64](https://github.com/taowen/linux-um-arm64)
and [taowen/ios-sweet](https://github.com/taowen/ios-sweet). Fixed source commits
and upstream repositories are recorded in `sources.lock.json`.

These projects make the research worth attempting, but cannot simply be combined.
Older ARM64 apps with simple UI are a long-term hypothesis, not demonstrated
compatibility. Preserve upstream attribution and licenses when reusing code.
