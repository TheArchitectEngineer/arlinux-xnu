# Experiment contract

## Current handoff

The active target is a Darwin/iOS compatibility environment. XNU and iOS
userland may be modified, and Linux-backed implementation is allowed. Native
ARM64 execution is preferred where supported by evidence. Unmodified XNU boot
is a comparison, not a prerequisite or an application acceptance gate.

Native console journey passed:
[39320163457](https://github.com/taowen/arlinux-xnu/actions/runs/39320163457),
source `809b6abcb14e99224b4ae5c10cbdecce5510778c`, job `118291838758`.
The runner executes ARM64 directly on Linux with 4 KiB host pages. Private image
adaptation rewrote two instructions: the program's Darwin TSD and CPU-metadata
reads. SIGSYS handlers continued working after guest TLS replaced host TLS.

The actual program allocates/touches two 16 KiB pages, forks, changes child TLS
and memory, frees the child's mapping and normally exits 37. The parent checks
the exact child PID/status, unchanged parent memory and TLS, performs a real
protection call, consumes +/q input, prints counter=1, unmaps its memory and
normally exits 0. The second independent process launch completes the same
journey. Logs show the actual BSD/platform calls, both child exit 37 and parent
exit 0; no host kill is used to simulate guest exit.

- Guest console SHA-256: `813686803bb8f8dc470327bd036d05cbb1e37e43f9732352b46043d0672138c8`.
- Native runtime SHA-256: `ff72fc4aa045cb46d47f4b2b207580951e569af81d4041997a01f548479a6f91`.
- Receipt SHA-256: `fa8e2c69020824042346108d6dd100c2cc6517ccbcde9da6ea3af62f36980672`.
- Text evidence: build/runs/39320163457/native-job.log. Exact binaries,
  disassembly, per-launch stdout and syscall logs remain in the run artifact.

This proves only the supplied freestanding console journey on the Linux runner.
It does not prove a protection-fault journey, POSIX threads, Mach IPC, exceptions,
dyld, genuine frameworks, arbitrary instruction rewriting, an iOS UI App or the
Android app sandbox. Next connect a genuine dynamic userland: process startup,
LC_LOAD_DYLINKER and executable mappings must be implemented rather than treating
loader metadata as inert. Require a real program journey at each extension.

New, not yet validated: ./build.sh build --mode dyld re-runs the native
console journey, then obtains iPhone7,2 iOS 12.5.7 (16H81) from Apple's CDN on a
Linux ARM64 runner. It verifies the complete IPSW SHA-256, uses Apple's manifest
to select the filesystem, and extracts the real non-arm64e /usr/lib/dyld.
Apple binaries stay transient; only input hashes, load commands and execution
logs are uploaded. The runtime now accepts a Mach-O dylinker/ARM64 UNIXTHREAD
entry, slides the interpreter, and constructs the initial app-header/argc/argv/
envp/apple stack. It still lacks the wider dyld system interfaces. The run must
show real startup or its actual fault/unsupported-call path, not substitute a
handwritten loader and label it dyld. File/executable mmap and library services
remain incomplete. The static journey remains a required predecessor.

First real-userland input attempt [39320874526](https://github.com/taowen/arlinux-xnu/actions/runs/39320874526)
passed the native console prerequisite but did not execute dyld. The complete
Apple IPSW SHA-256 matched. 7-Zip exposed the APFS compressed file's empty data
fork and its alternate streams; the extracted dyld was zero bytes, and the
Mach-O loader rejected it. Failure receipt SHA-256:
`ec93ca4de939db0e368c90aab1ba2dbaed2023c357d9d4b1394a7d835f8e8dc0`.
The input preparation now builds pinned unmodified apfs-fuse/LZFSE sources on
the runner and reads the file through its read-only compressed-file decoder.
This changed preparation and actual interpreter startup await verification.

Earlier native build failures remain in Actions and local text logs:
39319498437 (cross-section ADR), 39319703701 (cstring layout), 39319854670 (host
page signedness). No program ran in those attempts. The engine-dependent kernel
candidate, execution bridge and build jobs have been removed; source history
preserves prior experiments without keeping them in the current build graph.

The full-system reference
[39311945725](https://github.com/taowen/arlinux-xnu/actions/runs/39311945725)
ran original XNU exec/VM/fork/COW/exit/wait with the static PID 1 journey:

- Kernel SHA-256: `d2e148a3982925554ee990810b7d7ec037aafcf93ac77dae7b801423f706d7b2`.
- Mach-O SHA-256: `f884156eb2847161a41634c90c92d477f4ad2ac7983d5738ddc82b81fd380a74`.
- Compiler receipt: `c07c44b204a34e5833d3e605a72b870b3116d648c2690c919969890fa1b6d609`.
- Boot receipt: `06bbf549740ca1a0e09db1d99d1607222d970a811da7b82dd158c09b5bdca639`.

The final circular framebuffer snapshot showed the completed journey and zero
unknown glyphs; it is not a complete event trace. Exact artifacts remain in
Actions. Reference success does not establish native usermode compatibility.

## Evidence boundaries

| Experiment | Last established result | Does not establish |
| --- | --- | --- |
| `native-userland` | [Native console: VM/COW/TLS/input/normal exit/restart](https://github.com/taowen/arlinux-xnu/actions/runs/39320163457) | dyld, genuine frameworks, iOS UI App or Android integration |
| `host-substrate` | [ARM64 ptrace/register and 16 KiB mapping probe](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU VM or Android sandbox operation |
| `macho-userland` | [Freestanding ARM64 Mach-O output/error/exit via host shim](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU execution, dyld or frameworks |
| `xnu-baseline` + `xnu-reference-boot` | [Full-system XNU exec, memory/COW, child exit and parent wait](https://github.com/taowen/arlinux-xnu/actions/runs/39311945725) | Userspace XNU, dyld, iOS frameworks or apps |

These are historical observations at their runs' source revisions, not promises
about current HEAD. No Android app-sandbox journey or third-party iOS app has
been validated. Required gates remain memory/tasks/exceptions/IPC as needed,
dyld and system libraries, frameworks/display/input, then an actual App journey
on Android. Kernel initialization is not a prerequisite when Linux-backed
services supply the required application behavior. Record the highest gate
actually observed; a green probe never advances a later gate.

## Working loop

1. Read this handoff and the exact failing run before changing code. State one
   hypothesis and the observable result that would confirm or reject it.
2. Commit the source, dependency pins and workflow changes together. Dispatch
   only the relevant mode through `build.sh`; `both` explicitly runs all stages.
3. Track the exact run ID and attempt. Inspect its status after interruptions;
   a local wait timeout is not grounds to dispatch another build.
4. Inspect that run's Actions logs, exit/result and printed receipt/executable
   hashes. Download a specific artifact only when its contents are needed for
   diagnosis or durable preservation, not as a routine workstation build step.
   Preserve failures; never relax checks to make an unfinished stage green.
5. Replace the current handoff when the blocker changes. Update the table only
   when an acceptance gate changes. Keep trial history in Actions and Git,
   rather than appending run diaries to README or this document.

Avoid changing unrelated layers in one trial. Re-run the affected upstream and
downstream stages when interfaces change. An unchanged bare-metal baseline need
not run for every Linux host edit. Source pins fix source identity, but hosted
runner images/package repositories still change: retain environment versions,
and do not claim bit-for-bit reproducibility from pins alone.

## Evidence lifecycle

Each executed job writes `receipt.json` alongside its artifacts, including the
Git revision, run/attempt/job identity, dependency lock, host identity, execution
outcome before artifact upload and SHA-256 of retained files. The Actions job
conclusion remains authoritative for upload failures/cancellation; a receipt
cannot certify events after it was written. Checkout/runner failures can prevent
receipt creation, so missing evidence is never a passing result.

Artifacts are retained for 30 days. Download useful failures and milestone runs
through `build.sh download --run-id RUN_ID` before expiry; local downloads are
not a durable shared archive. Before relying on a milestone long-term, preserve
its exact artifact ZIPs, receipts and workflow logs in a versioned external
archive or immutable release. Until that archive exists, old Actions links may
lose their binaries. Do not silently replace expired evidence with a new build.

Git stores source, pins and this short handoff. Actions stores run history and
logs. Artifacts store bytes and provenance. These roles should stay separate.

The default `build.sh build` now selects the real kernel path. Standalone early
diagnostics require `--diagnostics`; host/shim and bare-metal runs require their
explicit modes. The VM startup task links the complete declared service graph and attempts
execution. The former full-link job duplicated the exact same link and exited;
it has been removed. Optional diagnostics do not turn failed kernel execution
into a passing result.
