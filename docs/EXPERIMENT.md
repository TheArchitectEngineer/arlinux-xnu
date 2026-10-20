# Current experiment handoff

Target: a modifiable Darwin/iOS compatibility environment on Linux/Android,
native ARM64, 16 KiB guest pages, one actual scheduled/advertised CPU, non-arm64e.
Build/report/download only through build.sh on hosted runners. Design boundaries
belong in [ARCHITECTURE.md](ARCHITECTURE.md); the executable acceptance contract
is [JOURNEYS.md](JOURNEYS.md). Read [DIAGNOSTICS.md](DIAGNOSTICS.md) before changing
runtime behavior. Trial history remains in Git and exact Actions artifacts.

**No complete Linux GUI journey, third-party iOS app or Android acceptance has
passed.** ARHello 1.0 and ARWorkbench 1.0 use real pinned iOS 12.5.7
UIKit/Foundation/libSystem; bundled ARJourneyWorker is a real Foundation XPC
service. The suite requires seven composed-pixel/input/lifecycle journeys.
Blocked effects are not exercised or passing.

## Latest actual runtime

[39448373812](https://github.com/taowen/arlinux-xnu/actions/runs/39448373812),
source **7296e4ce842dbb1f0d4f0a33cd8009c0a3bba7c7**, completed failure.
Whole hosted run: 235 seconds; app journeys: 116 seconds. Compiler cache:
141 direct hits, 33 preprocessed hits, one miss. Real native console and pinned
dyld prerequisites pass. All seven app cases fail on missing visible controls;
subsequent dependent steps remain blocked. No job remains live for this work.

Dyld evidence receipt SHA-256:
31e25cb062d8339ce3e1fa08c376cde672db9bff4991041b0cac6095c8d1d68b.
The selected artifact is linux-arm64-apple-dyld-evidence. Its journeys/result.json
records every action, original process identity, screenshots/OCR, blocked steps,
cleanup and same-process debugger evidence. Downloaded receipts and relied-upon
files must match hashes before using them as evidence.

The preceding run [39446941536](https://github.com/taowen/arlinux-xnu/actions/runs/39446941536),
source **090b0d5629317872f36af010b09bd7b3e6716549**, independently confirms the
new genuine platform constructor installs the main-run-loop observer. Actual
plaintext mremap_encrypted returns zero; app pixels reach the composed desktop.
Receipt 2841ca77014567ba44ef99c54e0229769e6339e6c1da25033baf2f97d3dcd8a5
was verified against result.json, original runtime and composed failure PNG.
The real UIKit tree contains UIButton and its Increment label, but that label
is absent from the composed image. Current window bounds are 320 x 480, not the
older 1024 x 640 capture. Do not assume the old geometry still describes this run.

## What was replaced

- Timeout/readiness acceptance became actual app -> genuine nested Wayland input
  and composition -> screenshot/OCR/effect/lifecycle validation. Failure cleanup
  never certifies normal close. Two apps are launched together for concurrency;
  a deliberately killed peer cannot certify normal exit.
- Process roles and shared-session membership are explicit. Display registration
  no longer defines service membership; provider liveness is separate from GUI
  liveness. Full headless-service behavior remains unverified.
- Scene serialization, audited IPC state, framework import binding, per-process UI
  state and optional diagnostics now have separate ownership. The remaining
  header-assembled syscall runtime still needs decomposition, not a claim that
  the entire project has already been rewritten.
- UI attachment moved out of bootstrap lookup into a real dyld-inserted platform
  library constructor and versioned private ABI. Failed insertion is fatal;
  cryptid 0 is genuine plaintext loading, nonzero encryption stays unsupported.
- Runtime traces use a dedicated inherited descriptor, distinct from original
  guest stdout/stderr. App failure inspection attaches the original process;
  an unrelated diagnostic replay is not substituted for the failing journey.

## Positive reference and limits

Apple-host [39446975446](https://github.com/taowen/arlinux-xnu/actions/runs/39446975446),
source 090b0d5629317872f36af010b09bd7b3e6716549, passed two actual ARHello click
-> Count: 1 -> normal exit/status 0 cycles. Verified receipt:
77bad7b0b3ccbf936e698cbfac7bdf2fe9758eb464e679f580cccfb47b3b2f6c.
Both screenshots and original LLDB logs match it. The second screenshot was
visually inspected. macOS supplies its native runtime; this is not Linux/Android
acceptance and does not execute all seven manifest cases.

## Next implementation boundaries

1. Diagnose missing real control pixels using the original UIKit layout/layer/
   color evidence. Do not draw replacement buttons or alter fixture actions.
2. Implement actual focus/pointer/keyboard and normal-close delivery to the guest
   main loop. BackBoard HID focus discovery is not implemented input semantics.
3. Own writable application containers and guest file operations. Harness cwd is
   not a data-container implementation. Persistence currently stops before input.
4. Implement app-requested service launch and retirement; then prove XPC payload,
   independent process/session identity, lifetime and restart through its journey.
5. Establish independent Mach-right/audit/resource cleanup evidence after crashes,
   complete the two-app lifecycle and repeat on ARLinux/Android with real input.

Imported Darling/XNU remains the shared Mach owner, with original licenses and
ORIGINS.json. No virtual PID 1 or successful stub replaces required behavior.
The pinned firmware identity and input SHA-256 live in sources.lock.json and
per-run receipts. Apple firmware/cache/executables/cores stay transient on runners.
Only safe text/screenshots and open-source products are retained/downloaded.
