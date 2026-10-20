# Diagnose the original app journey

Read the exact run before changing behavior. Use the sole build.sh entry:
status and report inspect an existing run; download selects its retained evidence.
See README for commands and APPS.md for acceptance. The default build runs the
independent app collection, not a timeout/readiness check. Compilation and guest execution
remain on hosted runners, never WSL or the workstation.

The default failure collection symbolizes original stacks and records the live
process. Do not attach a standing catalogue of private-service symbol dumps to
every failure. Use explicit targeted cache lookups only for the current failed
effect. Production framework adaptation is allowed; the diagnostic harness must
remain read-only with respect to app/framework behavior.

For the current lifecycle boundary investigation, opt in with
`./build.sh build --app counter --lifecycle-context`. The injected platform
library reads actual loaded Objective-C method signatures and samples the real
host window-state API for ten seconds on the genuine main run loop. Evidence is
in the app's stderr (`IOS_LIFECYCLE_ABI`, `IOS_HOST_WINDOW`). It invokes no app
actions or private framework lifecycle methods. This flag defaults off and is
not forwarded to the Apple-host reference. It is diagnosis, not launch success.

## Short feedback loop

Use `./build.sh build --app counter --lifecycle-context --wait --evidence` for
one current boundary. It compiles only Counter plus its real shared adapter;
Linux preparation runs concurrently with the SDK job. The app artifact name
binds the producer receipt digest and run attempt; source/run/attempt plus every
product hash are checked before execution. No older app binaries are reused.
The default `all` still builds/runs the full collection and cross-app scenarios.

`--wait` follows the exact dispatch URL, never another run with the same commit.
Job/step transitions and completed-job failures are shown while the run is live.
Observation failures keep following that run without redispatch. It then reports
results and optionally downloads the safe execution artifact, verifying its
receipt against the exact producer log and every retained file. For an existing
run use `./build.sh report --run-id RUN_ID --job native-userland --evidence`.
Verified evidence is isolated under build/runs/RUN_ID/attempt-N/ARTIFACT.

APP_DIAGNOSIS records the first failed step, blocked steps, original process
identity, screenshot paths/hashes and bounded runtime/window/session witnesses.
The step's effect wait and diagnostic collection time are recorded separately.
These are observed facts, not an automatic root-cause verdict. Full logs and
original-process stacks remain available. Compare dispatch-to-completion and
job start offsets; compiler cache hits alone do not measure feedback latency.

Measured Counter lifecycle-diagnostic feedback: run
[39503145757](https://github.com/taowen/arlinux-xnu/actions/runs/39503145757) took
241 seconds; parallel run
[39504112474](https://github.com/taowen/arlinux-xnu/actions/runs/39504112474) took
158 seconds (34% shorter). Linux job start moved from second 79 to second 6;
selected SDK compilation took 18 seconds. Both fail the same required GUI step.
The new run separates 8.024 seconds of effect waiting from 12.297 seconds of
original-process diagnosis. These are individual measured runs, not a latency
promise; runner queues/network still vary. Firmware remains transient and fully
hash-verified, and no Apple firmware/cache is uploaded for caching.

## Start with the effect that failed

| Question | Authoritative evidence | Limit |
| --- | --- | --- |
| Which user step failed? | apps/result.json and APP_CASE / APP_SUITE | Blocked steps have not run; submitted input is not observed delivery |
| Was content actually displayed? | Unmodified composed screenshot, exact window/control geometry, pixel statistics/hash | Raw CG buffers, readiness and frame callbacks are insufficient |
| Did the original app crash? | Its generation's runtime log, fault PC and same-cache symbols | A debugger replay can have different scheduling |
| Which process was inspected? | Recorded PID/start ticks/executable; original-process.lldb.log | LLDB status 0 means collection succeeded, never app acceptance |
| Which threads/queues wait? | Original-process bounded stacks/registers plus actual runtime wait/queue traces | Empty queues identify a wait, not the missing sender |
| Did a message arrive? | Real send/receive rights, header, OOL descriptors and receiver continuation | Producer/reply logs alone do not prove consumption |
| Did a service really work? | App-visible response plus helper process/session identity and lifetime | A name lookup or an empty receive endpoint is discovery only |
| Did close/relaunch work? | Actual exit status, fresh generation identity, still-interactive peer | Forced cleanup and readiness are not normal close |
| Did a UIKit exception explain failure? | Original stack/register evidence; same-cache exception symbol addresses for targeted LLDB breakpoints | Never use po/injected guest calls to invent an exception reason |

Verify downloaded receipt.json against the exact run's published receipt SHA-256,
then verify each relied-upon artifact against that receipt. Selected downloads
are isolated under build/runs/RUN_ID/ARTIFACT_NAME to prevent receipt collisions.
Missing artifacts are
unavailable, not passing. Apple firmware/cache/core/executables stay on the runner;
retain only safe text, screenshots and open-source products.

Original fault symbolication retains bounded decoded instructions from the
same-cache function and up to eight named display-protocol callees, at most
three call levels. Use actual decoded reply validation and field loads to
determine private wire layouts; a receive-buffer allocation is not necessarily
the message length. Disassembly is read-only diagnostic evidence, not a runtime
patch or proof that the service implements the requested effect.

The Linux journey harness enables ARLINUX_STOP_ON_FAULT for original app
processes. A fault first records registers and bounded kernel mappings, marks
the journey failed, and stops the original process for existing read-only LLDB
collection. Debugger collection or forced cleanup never counts as normal exit.
Atlas Image metadata reads require a matched actual fault symbol and a complete
writable-map bounds check; no guest calls or executable-memory dumps are added.
The same original-process collection can decode its registered dyld image-info
and version-0 compact snapshot as bounded metadata. It emits image paths/addresses,
not raw bytes, and rejects unknown layouts. Compare these with actual VM traces
and dyld's own library/segment logging. Only those two explicit diagnostic env
switches are forwarded through the guest environment allowlist.

Each original app generation has .stdout, .stderr and .runtime.log. The harness
passes an actual inherited trace descriptor to the runtime; guest writes to stderr
remain on descriptor 2. Native per-syscall traces no longer split dyld diagnostics
into tiny interleaved fragments. Host loader/Wayland diagnostics may still share
.stderr; neither stream alone is an acceptance oracle. Source-owned apps export read-only
UIKit/DOM state; GUI acceptance additionally requires actual composed pixels.
No OCR or AI recognizer is used.

## Runtime boundaries

src/diagnostics/ios-ui.h owns optional capture/view-tree diagnostics. Those run on
the genuine UIKit main loop and use raw syscalls, never host libc/TLS. They do not
change layout, call control actions or supply implementation. API export/code
inspection is opt-in with ARLINUX_DIAGNOSTICS; production imports are the distinct
linkage contract in ios-framework-api.h. Required imports are not feature coverage.

UI_LAYER_FRAME means the real layer renderer returned into shared CG storage.
WAYLAND_APP_FRAME_COMMITTED/CALLBACK mean the Wayland transport accepted a frame;
none proves useful pixels or physical presentation. UI_BOUNDS_SIZE holds exact
ARM64 CGRect double bits; report also prints measured dimensions. Zero bounds
were the actual cause of earlier black frames, despite readiness and frame events.

Original and replay process/port namespaces must never be numerically correlated.
The imported Mach core owns the real queues. Legacy local endpoint arrays are not
Darling's queues. S2C copy, final permissions and icache flush are actual effects;
a successful allocation or server producer marker is not a copy/delivery proof.

## One useful feedback loop

1. Identify one failed user effect and its original process/run/source identity.
2. Read the corresponding screenshot, runtime/fault and saved arguments/stack.
3. State a mechanism and the real-app observation that would distinguish it.
4. Change the owner of that mechanism, not a fixture action or acceptance rule.
5. Run the full affected journeys on the exact pushed source; retain failures.
6. Replace EXPERIMENT.md's current handoff. History remains in Git/Actions.

Cache reuse uses compiler-content validation through ccache with no sloppiness.
Final linking, source/run hashes, real console prerequisites and the complete app
suite still run. Reports cache completed raw logs only after matching run/attempt/
source/job identity and their recorded SHA-256. --verbose retains full observations.
Shortening feedback must not create a second builder, fake backend, probe-only
acceptance or an exception that turns an unimplemented app effect green.
