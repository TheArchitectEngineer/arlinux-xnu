# Experiment contract

## Current handoff

The active target is a Darwin/iOS compatibility environment. XNU and iOS
userland may be modified, and Linux-backed implementation is allowed. Native
ARM64 execution is preferred where supported by evidence. Unmodified XNU boot
is a comparison, not a prerequisite or an application acceptance gate.

New native console journey: src/macho-native.c loads the actual Mach-O image,
adapts marked instruction sections for TSD/CPU-register reads, and enters native
ARM64 code. src/darwin-native.c handles Darwin BSD/platform calls through a
TLS-independent SIGSYS/raw-syscall runtime. guests/console.S allocates/touches
16 KiB pages, installs and reads guest TLS, forks with child TLS and COW changes,
waits for normal child exit 37, checks parent state, handles +/q input, unmaps
memory and exits normally. The runner launches it twice. This is pending runner
verification, with no dyld, genuine frameworks, UI App or Android sandbox proof.

First native runner attempt [39319498437](https://github.com/taowen/arlinux-xnu/actions/runs/39319498437)
failed guest assembly: Mach-O does not provide the cross-section ADR relocation
used for console strings. The code now uses ADRP/PAGEOFF pairs. No guest ran.
Failure receipt: `1b72b99bbc93d68c4ee423b33125a39681d5843bff63da79970ebb3f054724a6`.
Local text log: build/runs/39319498437/native-job.log.

The previous kernel candidate run
[39318615195](https://github.com/taowen/arlinux-xnu/actions/runs/39318615195),
root `4ca6ab1c137c655517d10013b2212edeefd75b5c`, compiled the full configured BSD
graph and the matching real libdispatch firehose implementation. Compiler job
`118286849038` passed, receipt SHA-256:
`be767233c1a04ccec259226c5198120fbc9ac7b4eb42bf65452707c65c13f8e7`.
VM job `118287652980` failed executable linkage; receipt SHA-256:
`c3c98e02203d66e471f0aa1185b829f70b5128f001600c86e054dbbd6a2d3678`.
No native XNU executable or process ran. Logs: build/runs/39318615195.
Keep this candidate available for useful code reuse; symbol completion is not
the active application milestone.

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
| `host-substrate` | [ARM64 ptrace/register and 16 KiB mapping probe](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU VM or Android sandbox operation |
| `macho-userland` | [Freestanding ARM64 Mach-O output/error/exit via host shim](https://github.com/taowen/arlinux-xnu/actions/runs/39214608040) | XNU execution, dyld or frameworks |
| `xnu-baseline` + `xnu-reference-boot` | [Full-system XNU exec, memory/COW, child exit and parent wait](https://github.com/taowen/arlinux-xnu/actions/runs/39311945725) | Userspace XNU, dyld, iOS frameworks or apps |
| `xnu-kernel` | [Selected real kernel graph compiled](https://github.com/taowen/arlinux-xnu/actions/runs/39309052509) | Executable kernel or guest execution |
| `xnu-link-inputs` | Run 39309052509: relocatable merge passes | Executable linkage or kernel boot |
| `xnu-early-startup` | [Latest link fails on lock startup imports](https://github.com/taowen/arlinux-xnu/actions/runs/39270528453); prior raw diagnostics passed | VM initialization, tasks or guest execution |
| `xnu-vm-startup` | [Executable link fails; native engine compiled](https://github.com/taowen/arlinux-xnu/actions/runs/39309052509) | VM bootstrap or guest execution |

These are historical observations at their runs' source revisions, not promises
about current HEAD. No Android app-sandbox journey or third-party iOS app has
been validated. After kernel initialization, separate acceptance gates remain:
guest memory/tasks/exceptions/IPC, dyld and system libraries, frameworks/display/
input, then an actual app journey on Android. Record the highest gate actually
observed; a green probe never advances a later gate.

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
