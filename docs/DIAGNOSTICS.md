# Diagnose the original app journey

Read the exact run before changing behavior. Use the sole build.sh entry:
status and report inspect an existing run; download selects its retained evidence.
See README for commands and APPS.md for acceptance. The default build runs the
independent app collection, not a timeout/readiness check. Compilation and guest execution
remain on hosted runners, never WSL or the workstation.

## Start with the effect that failed

| Question | Authoritative evidence | Limit |
| --- | --- | --- |
| Which user step failed? | apps/result.json and APP_CASE / APP_SUITE | Blocked steps have not run; submitted input is not observed delivery |
| Was content actually displayed? | Full composed screenshot + SHA-256 + OCR TSV/text | Raw CG buffers, readiness and frame callbacks are insufficient |
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

Each original app generation has .stdout, .stderr and .runtime.log. The harness
passes an actual inherited trace descriptor to the runtime; guest writes to stderr
remain on descriptor 2. Native per-syscall traces no longer split dyld diagnostics
into tiny interleaved fragments. Host loader/Wayland diagnostics may still share
.stderr; neither stream is an acceptance oracle.

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
