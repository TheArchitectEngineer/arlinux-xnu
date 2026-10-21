# Diagnose the original app journey

Read the exact run before changing behavior. Use the sole build.sh entry:
status and report inspect an existing run; download selects its retained evidence.
See README for commands and APPS.md for acceptance. The default build runs the
independent app collection, not a timeout/readiness check. Compilation and guest execution
remain on hosted runners, never WSL or the workstation.

The default failure collection symbolizes original stacks and records the live
process. Original IPA downloads have a shared retry budget and a 185-second
outer deadline; an unavailable input fails that app and does not silently choose
another binary or hold the collection for four independent three-minute waits.
Declared mirrors name the same pinned object and share this budget; size/hash
validation is unchanged, and input provenance records the actual download URL.
Do not attach a standing catalogue of private-service symbol dumps to
every failure. Use explicit targeted cache lookups only for the current failed
effect. Production framework adaptation is allowed; the diagnostic harness must
remain read-only with respect to app/framework behavior.

For the current lifecycle boundary investigation, opt in with
`./build.sh build --app counter --lifecycle-context`. The injected platform
library reads actual loaded Objective-C method signatures and samples the real
host window-state API for ten seconds on the genuine main run loop. Evidence is
in the app's stderr (`IOS_LIFECYCLE_ABI`, `IOS_HOST_WINDOW`). The same profile
compares the loaded FBSSceneUpdater protocol (including inherited protocols,
instance/class and optional/required methods) with the real adapter. It reports
missing methods and raw encoding differences together, with explicit bounds and
truncation. Equal encodings do not certify behavior; missing optional methods do
not establish the cause of a failure. It invokes no app
actions or private framework lifecycle methods. This flag defaults off and is
not injected as a Linux platform implementation on the Apple-host reference.
The original-app reference observer may read the same public view/screen
geometry there. It is diagnosis, not launch success.
For the Workers queue investigation, it also observes actual UIControl action
dispatch through a typed, bounded forwarding wrapper. The original method gets
unchanged arguments; no missing action is supplied. APP_WORKER_QUEUE records
real submit/background/main execution in the source-owned app. These stages
localize delivery failures but never replace changed label/pixels and normal
exit as acceptance.

Explicit runtime tracing also keeps bounded observations of actual header-only
Mach wakeup sends and receives: ports, thread IDs, counts and real results, with
an omitted-entry count. It changes no messages or scheduling. On an original
Workers delivery failure, the existing collector resolves matching-cache main
queue symbols and reads at most two validated, writable non-executable metadata
ranges alongside the original stacks. Optional metadata errors cannot suppress
stack collection. A prior successful callback does not hide a later stall.
Network's source-owned phase records distinguish task creation/resume, actual
completion and main-queue delivery. They log status/error metadata, not payloads;
only the HTTP peer record plus visible answer/pixels and normal exit pass.

The same opt-in profile observes failing original CoreUI color lookups. Its
typed forwarding wrapper keeps the original arguments, NSError pointer and
return object, recording the real error domain/code, underlying error and path.
It neither issues an extra color lookup nor substitutes a color.
The unsuccessful NSError-construction observers have been removed after the
file reachability boundary fixed the real color-resource failure. On a future
actual color lookup failure, the targeted CoreUI lookup follows catalog loading,
not a standing dump of every color method. BSD ENOSYS
also retains original arguments and caller frames once per syscall (at most 32
distinct calls, with an explicit truncation marker). The symbolicator resolves
those saved return addresses against the same cache without adding instruction
dumps for every frame. Getattrlist calls record bounded path/attribute
metadata for the first 32 calls; the guest output buffer is never read or changed.
The failure summary includes eight syscall frequency entries and eight resource
paths; full arguments, callers and truncation markers remain in runtimeLog.
Frequency is not a root-cause verdict.
File-mapping failures have a separate eight-request observation budget, retaining
the original arguments, real descriptor path and saved caller frames even when
successful startup mappings exhausted the ordinary trace limit. This addresses
the original Things run's many later mmap errors without guessing from frequency
or treating a missing image as proof of a mapping cause. Results are unchanged;
the same cache symbolicator resolves these callers without blanket code dumps.
For the original IOSurface initialization abort, targeted reads now resolve the
actual WebCore size/row-layout configuration and IOSurface property boundary,
rather than unrelated QuartzCore backing-store functions. Current selected Web
evidence has passed those queries and records unsupported WebContent/Networking
discovery instead. Configuration logs identify a platform boundary; DOM pixels,
real input and helper lifetimes remain the required effect.
Service bootstrap diagnostics retain bounded key/type metadata only. The
session owns actual initialization dictionaries and their rights under audited
PID/incarnation and pidfd lifetime; logs omit nested environment values. A
successful bootstrap-setting reply reports stored configuration, not process
launch, connection readiness or service functionality.
The failure summary also retains bounded actual service resolution conflicts,
spawn errors and child start/exit witnesses from session.log. A child start is
not check-in; its PID/start ticks and original execution log distinguish spawn
from a working service. The full session log retains every instance.
If a failed journey has actual SESSION_SERVICE_STARTED records, the same
original-process collector also reads bounded stacks from its owning session
and at most two live original helper PID/start-tick identities, preferring the
latest live instance of each distinct service. Reused/exited PIDs
are marked unavailable; further helpers are explicitly counted as omitted.
Service-log symbolication runs after the owning session has retired and its
log is closed, to include exits/faults during collection and cleanup. These attachments run after the failed effect and never count as service
readiness or normal exit. Helpers that have not reached Mach-O entry may still
be in host cache preparation; inspect the original stacks before adding IPC.
The runner session explicitly enables the existing stop-on-fault option for its
owned helpers, and the launcher forwards only that named diagnostic option.
Their actual fault records in session.log are symbolicated against the same
firmware cache. This preserves original exception stacks for attachment rather
than replaying the helper or treating exit 133 as a service result.
The Mach core's suspension notification uses its generated rtsig.h, passed
through the existing build handoff. It is not a hardcoded RT offset. The
temporary diagnostic stop exposed JavaScriptCore scavenger -> stop_allocator ->
thread_suspend in run 39567204970: this is a real allocator synchronization
request, not an invalid guest instruction. The replacement native handler
follows Darling's interrupt-enter/thread_suspended/interrupt-exit protocol and
preserves ARM64 integer/NEON state. Bounded BEGIN/RESUMED records observe the
actual wait, not a fake resume or a Web acceptance result. RPC failures retain
the original caller through the existing fatal collector.
Observed unsupported check-in operation 0x325 selects the actual bootstrap and
connection check-in consumers. --lookup-fragment reads at most four matching
function symbols; ambiguous sets remain metadata only. The earlier discovery
listing is removed once the real names are known. No lookup result implements
the requested receive-right handoff.
For native XPC reception, correlate XPC_OOL_RECEIVED with XPC_OOL_RELEASED and
its actual deallocation status in the same session.log. XPC_DECODE_FAILED records
the message ID, byte count and decoder errno; subsequent replies/requests and
the original app stack identify whether the session survived. Only routing
metadata is logged, never raw OOL bodies or nested environment dictionaries.
Successful decoding/release does not establish service, helper or app success.
The same opt-in profile records an uncaught NSException's actual name, reason
and at most 24 original throw-stack addresses. It chains the previous uncaught
handler and preserves normal termination. Those frames are symbolicated from
the same original process stderr/cache; an abort symbol alone is not a cause.

## Short feedback loop

Use `./build.sh build --app counter --lifecycle-context --wait --evidence` for
one current boundary. It compiles only Counter plus its real shared adapter;
Linux preparation runs concurrently with the SDK job. The app artifact name
binds the producer receipt digest and run attempt; source/run/attempt plus every
product hash are checked before execution. No older app binaries are reused.
The default `all` still builds/runs the full collection and cross-app scenarios.

For one worker behavior or cross-app scenario, use `--case` with exact IDs from
`./build.sh cases`. Dispatch, SDK compilation and execution share one selector:
only the chosen journeys' app dependencies are built, including scenario peers.
`--app` can restrict the allowed apps; a conflicting case is rejected. Any case
selection is partial, even when all named cases pass. Each retained APP_CASE
reports total journey time, effect time and diagnosis time separately. Normal
close, screenshots and fresh state are unchanged.

`--baseline-run RUN_ID` on report or build --wait compares two completed runs.
It verifies both receipts and result hashes before comparing shared cases,
reports pass/fail transitions and failed-step movement only for unchanged journey
definitions, and keeps omitted cases explicitly untested in that selection.
Failure reasons and diagnosis/total durations remain visible; reaching a later
step is not a passing journey or a root-cause claim. A single failed-to-passed
transition is labelled "now passes; cause not established", since timing changes
and intermittent failures can produce the same transition without a repair. The comparison is saved
beside the current run report. This avoids rereading two large logs to find
whether a common boundary improved another app or regressed an existing flow.

Pixel observation requires real contrasting ink on a light fixture surface,
including native gray controls; it does not require a majority of near-white
pixels inside a button. Linux and Apple-host samplers share the same ink
threshold, so the light-pixel complement applies equally to both. Blank frames
still fail; changing reported text must also change the actual target pixels.

If GitHub still reports a running job after all its steps finish, report
--evidence may read its available producer log and verify already-uploaded
effects against the same receipt/source/run/attempt/file hashes. The job/run
status remains pending; this does not declare workflow success or permit
completed-run comparison.

`--wait` follows the exact dispatch URL, never another run with the same commit.
Job/step transitions and completed-job failures are shown while the run is live.
Observation failures keep following that run without redispatch. It then reports
results and optionally downloads the safe execution artifact, verifying its
receipt against the exact producer log and every retained file. For an existing
run use `./build.sh report --run-id RUN_ID --job native-userland --evidence`.
Verified evidence is isolated under build/runs/RUN_ID/attempt-N/ARTIFACT.
Downloads enter a unique partial directory and become reusable only after full
receipt/file verification. A damaged existing cache is preserved as unverified
and fetched again from that exact run; it never triggers a replacement build.
This recovery was exercised against the genuinely damaged cached evidence of
run 39547510074, then all 239 freshly downloaded files were verified.

APP_DIAGNOSIS records the first failed step, blocked steps, original process
identity, screenshot paths/hashes and bounded runtime/window/session witnesses.
The summary retains the first two and final two screenshots when there are more
than four. screenshotsRecorded and fullJourney identify the full retained case;
this bounds reporting only, never capture, acceptance or evidence retention.
The step's effect wait and diagnostic collection time are recorded separately.
After receipt verification, the report totals journey and included diagnosis time
and lists the three slowest journeys with their failed effect. These numbers
identify feedback cost, not root causes. A failed application-state step records
the observed public state, real notification counts and PID/start identity;
diagnosis targets the actual failed peer rather than whichever window was last
focused. Resolved scene-updater symbol scans are removed from automatic failure
collection; original-process stacks and fault symbolication remain.
The same opt-in profile reads at most eight real pointer dispatch snapshots:
actual touch/event gesture selectors, hit-view coordinates/ancestors and bounded
recognizer state before/after original UIApplication dispatch. It invokes no
gesture action and changes no recognizer state. On a pointer-related failure,
the collector resolves the actual UITouchesEvent add/clear consumers from the
same cache. A community selector hint must be confirmed on the pinned runtime;
setGestureView: is absent here and cannot be a required input binding.

These are observed facts, not an automatic root-cause verdict. Full logs and
original-process stacks remain available. Compare dispatch-to-completion and
job start offsets; compiler cache hits alone do not measure feedback latency.
The failure summary includes the actual fault PC's nearest symbol, displacement
and bounded referenced literals from the same cache, with the full symbolication
file retained. These literals are context rather than an automatic diagnosis.
Scene stages and exceptions are retained separately so a later marker cannot
overwrite the exception reason. Targeted investigations can select
`--lookup-bytes` on the existing runner-side symbolicator (up to 4096 bytes,
with truncation reported). The resolved CoreUI color and CALayer storage
investigations no longer cause standing private-function dumps on every
Counter failure. Actual fault and original-process stack symbolication remain.

The default workstation report prints the failed effect, separate effect and
diagnosis times, original identity, errors, syscall witnesses, resource paths
and latest screenshot/pixel evidence. Complete APP_DIAGNOSIS observations remain
in the saved report and producing job log; `--verbose` prints them unchanged.
Neither screenshot capture nor acceptance checks are reduced by this display.

The workflow starts the declared firmware download while apt installs tools,
then joins that same-run process before leaving toolchain setup. Download-only
is an internal phase of the existing preparation script, not another build
entry. APPLE_DOWNLOAD_TIMING explicitly reports unvalidated downloaded bytes;
the preparation stage still checks their size and SHA-256 before extraction.
Without an earlier download, preparation overlaps it with building the pinned
APFS reader. APPLE_PREPARATION_TIMING records readerSeconds, downloadSeconds,
parallelSeconds and downloadedBeforePreparation to distinguish the two paths.
Decoder timeout/failure now has an explicit per-context status; it does not
discard independent symbols. Runtime symbolication failure is recorded without
skipping original-process LLDB collection.

Runner setup declares the required packages with `--no-install-recommends`;
optional distribution recommendations do not become implicit experiment inputs.
Firmware remains transient and fully hash-verified; no Apple firmware/cache is
uploaded for caching. Current measurements and acceptance are recorded once in
[EXPERIMENT.md](EXPERIMENT.md); historical runs remain in Git and Actions.

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
are isolated under build/runs/RUN_ID/attempt-N/ARTIFACT_NAME to prevent receipt collisions.
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
The same opt-in collector now retains fatal service-request failures: after
recording DARLING_RPC_ERROR and DARLING_RPC_FATAL, the runtime stops the process
with SIGSTOP even when the rejecting thread blocks asynchronous signals,
so waiting original client threads can be inspected. Without the diagnostic
flag the fatal exit remains unchanged. Run 39510457131 retained the actual
UIWindow -> CAContext remote-connection stack that exit_group previously lost.
Atlas Image metadata reads require a matched actual fault symbol and a complete
writable-map bounds check; no guest calls or executable-memory dumps are added.
The same original-process collection can decode its registered dyld image-info
and version-0 compact snapshot as bounded metadata. It emits image paths/addresses,
not raw bytes, and rejects unknown layouts. Compare these with actual VM traces
and dyld's own library/segment logging. Only those two explicit diagnostic env
switches are forwarded through the guest environment allowlist.

Each original app generation has .stdout, .stderr and .runtime.log. The harness
preserves CoreFoundation's actual vectored stderr writes through Linux writev,
including short counts and real errors. Framework log messages are evidence,
not a replacement for the corresponding process stack and app-visible effect.
The harness passes an actual inherited trace descriptor to the runtime; guest writes to stderr
remain on descriptor 2. Native per-syscall traces no longer split dyld diagnostics
into tiny interleaved fragments. Host loader/Wayland diagnostics may still share
.stderr; neither stream alone is an acceptance oracle. Source-owned apps export read-only
UIKit/DOM state; GUI acceptance additionally requires actual composed pixels.
No OCR or AI recognizer is used.
Original firmware apps use the same read-only UIKit observer, compiled into
the injected platform library and explicitly enabled by the runner. It samples
visible windows on the genuine main loop; the app binary and resources are
unchanged. A mixed-theme pixel policy requires actual luminance contrast in
the observed element rectangle, plus changed pixels for changed text. It does
not replace calculation results or invoke control actions. Input preparation
records the original bundle/version, actual Mach-O ISA/encryption/dependencies,
executable and resource hashes; unsupported or unavailable inputs fail their
own journey. No extracted Apple app is uploaded with products or evidence.
With --lifecycle-context, that observer also retains one bounded visible view
tree with screen/native bounds/scales, size classes, actual class ancestry,
view transforms and layer geometry/clipping/contents properties. It changes
no layout or drawing. Use APP_RENDER_GEOMETRY alongside the composed screenshot
to distinguish a real layout overlap from lost masks/transforms in composition;
calculation success alone does not certify rendering fidelity.
Raw frame captures publish by atomic rename only after complete write/close.
An interrupted temporary frame is explicitly unpublished and removed before
encoding; the previous completed frame remains intact. Final published-frame
size checks still fail on corruption. Run 39515854314 validates the encoding path
after the interrupted-writer failure in 39515278534.

## Runtime boundaries

When the original failed process actually requests com.apple.containermanagerd,
its existing cache symbolication reads the Foundation group-container method and
the corresponding libsystem_containermanager C boundary. The bounded disassembly
is evidence for storage adaptation, not a daemon implementation. Embedded XML-only
entitlements require DER transmutation for the native iOS SecTask consumer; a
successful empty DER reply would incorrectly discard the input's declared groups.

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

Web's first DOM observation has an explicit 45-second cold-start budget: its
original helpers must load/adapt the real cache and initialize a VM before
showing content. Run 39600136880's live WebContent still entered its real
five-second database wait during post-failure diagnosis. Diagnosing at the old
eight-second first-paint cutoff interrupts startup before this work completes.
Only this first observation gets the budget; subsequent input/effects use the
normal deadline. Required structured text, composed ink/changed pixels, external
click and normal close remain unchanged. Report actual effect duration; a longer
budget never certifies a blank window or a surviving helper.

The runner's opt-in fault retention also covers native threads in its owned
shared session. SESSION_NATIVE_FAULT triggers the existing original session
LLDB collection before cleanup, rather than losing a crashed core and collecting
only secondary guest RPC failures. It changes no guest result or Mach request;
resuming the handler still exits fatally. Runtime RPC ancillary iteration uses
bounded layout arithmetic, avoiding an implicit host-libc CMSG_NXTHDR call while
Darwin owns TLS. Deferred reply bodies and received FD ownership follow the
integrated upstream server's push_reply/pipe-acknowledgement protocol.
