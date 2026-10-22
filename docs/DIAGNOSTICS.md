# Diagnose the original app journey

Read the exact run before changing behavior. Use the sole build.sh entry:
status and report inspect an existing run; download selects its retained evidence.
See README for commands and APPS.md for acceptance. Build requires an explicit
app or journey selection; full acceptance uses --app all. Compilation and guest execution
remain on hosted runners, never WSL or the workstation.

The default failure collection symbolizes original stacks and records the live
process. Original IPA downloads race the canonical URL and declared mirrors
under one 65-second outer deadline. Each names the same pinned object; the first
completed download must pass size/SHA-256 before use. Pending curls are killed,
reaped and their partial files removed. An unavailable or mismatched input fails
explicitly; no other binary is substituted. APP_IPA_DOWNLOAD records actual
download seconds, route count and selected URL alongside input provenance.
Do not attach a standing catalogue of private-service symbol dumps to
every failure. Use explicit targeted cache lookups only for the current failed
effect. Production framework adaptation is allowed; the diagnostic harness must
remain read-only with respect to app/framework behavior.

The Apple-host reference can prepare a declared IPA through the same pinned
download, archive safety and original executable checks as Linux. Its copied
bundle's Mach-O images (including bundled frameworks/extensions) then use the
pinned PlayCover converter and ad-hoc signing. Input metadata keeps original
hashes separate from converted/signed hashes. Conversion is an input step, not
compatibility acceptance; actual launch, window, input and persistence can still
fail. Apple-host behavior is a comparison and never Linux/Android acceptance.
Original IPA references use external macOS accessibility and screenshots, without
injecting the UIKit inspection library. AX rectangles are actual desktop points,
converted through the observed display scale and window pixel bounds. The last
bounded raw accessibility observation is retained beside the process logs;
permission failures are infrastructure failures. This isolates observer startup
effects but does not establish their involvement in any original app failure.
An original journey may explicitly declare reference.viewport=desktop and
reference.startupDialogs for genuine desktop-only startup prompts. A desktop
viewport observation records phoneDimensionsVerified=false and the requested
and actual dimensions; the Linux viewport check remains exact. Startup prompts
use the existing external helper's bounded macOS accessibility traversal and
exact button title, then AXPress on that actual button. Missing permission,
ambiguous buttons and failed actions are observable failures. No app method,
notification or acceptance callback is supplied, and a required UI effect after
the prompt must still pass. The optional prompt observation is retained even
when absent; it is never proof that the application reached its main screen.
For an IPA with an early Apple-host trap and no OS crash report, use that same
entry with --reference-only --lifecycle-context. The owned launch PID stops in
a diagnostic Python wrapper before exec; LLDB follows it into the original
executable, records the actual first exception/stop and bounded thread stacks,
then detaches within eight seconds. No replay, callback or return modification
is used. This changes timing and cannot establish ordinary launch acceptance.

For the current lifecycle boundary investigation, opt in with
`./build.sh build --app counter --lifecycle-context`. The injected platform
library reads actual loaded Objective-C method signatures and samples the real
host window-state API for up to 25 seconds on the genuine main run loop. Evidence is
in the app's stderr (`IOS_LIFECYCLE_ABI`, `IOS_HOST_WINDOW`). The same profile
compares the loaded FBSSceneUpdater protocol (including inherited protocols,
instance/class and optional/required methods) with the real adapter. It reports
missing methods and raw encoding differences together, with explicit bounds and
truncation. Equal encodings do not certify behavior; missing optional methods do
not establish the cause of a failure. It invokes no app
actions or private framework lifecycle methods. This flag defaults off and is
not injected as a Linux platform implementation on the Apple-host reference.
The firmware reference observer may read the same public view/screen
geometry there. Original IPA references remain externally observed.
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
Bounded DARWIN_KEVENT_ERROR records include ident, filter, flags, fflags, xflags,
the real result, data and udata. Data is the requested threshold for NOTE_LOWAT;
udata identifies the actual subscription. Do not interpret rejected registration
as lost wakeup or enable flags without implementing their lifetime semantics.
The same opt-in profile exposes each live SIGSYS handler's original kernel-owned
ucontext to the existing LLDB reader. It follows at most 24 actual ARM64 frame
records in readable non-executable mappings, rather than scanning for plausible
addresses when host unwinding loses the signal boundary. Context pointers are
cleared on handler return and thread-slot reuse; this changes no guest result.

An app may declare diagnosticSymbols for an original image's exported symbols.
Only --lifecycle-context enables the existing launcher to stop before dyld and
attach the bounded original_trace.py collector. It first observes the actual
main entry, resolves the requested symbol with the existing LLVM reader (including
stripped Mach-O export tries and original instance-method names written as
`-[Class selector:]`. Those method addresses come from the same image's LP64
Objective-C class/method metadata, including relative lists, validated against
file-backed executable segments)
and actual dyld load address, and records its real breakpoint hit/stack. An
explicit callbackRegister may observe the supplied code address and its later
execution; it never invokes that callback.
An optional afterSymbol arms a one-shot symbol only after the named entry is
actually hit, separating initial setup from the update following a request.
observeReturn follows that hit's actual link register on the same thread and
matches its entry stack pointer, so a nested call at the same return address
cannot consume the observation. Mismatches are counted with four sample records. It
does not change control flow or infer a successful operation from a return.
The 20-second observation window,
unhit breakpoints, unavailable metadata and detach result stay in the generation's
startup.lldb.log. Not hitting a breakpoint proves nothing beyond that window.
This profile alters timing and cannot be used as ordinary launch-performance
acceptance. The original UI/task/persistence journey remains independently red
until its actual effects pass; debugger success cannot replace those effects.
Reports retain hits, unhit observations, collector errors and detach status as
separate startupTrace evidence. Startup tracing time counts as diagnosticSeconds,
so it is not mistaken for ordinary app effect time.

For an original-app ordering failure that changes under debugger attachment,
an app may instead declare kernelTraceSymbols (bundle-relative image and exact
exported symbol). Firmware dyld-cache probes are rejected: Mach remapping can
copy the temporary breakpoint into a shared memfd outside the original inode's
probe ownership. Run 39732010413 faulted with SIGTRAP at the probed pthread entry
after its page became `darling-shared`; its original instruction is an ordinary
stack store. This is a diagnostic-interference hypothesis, not an original-app
crash verdict. Removing that probe in 39735050451 eliminates the recorded trap
but preserves the original Inbox stall. Observe actual workqueue upcall arguments at the runtime boundary
instead; these bounded records do not alter executable bytes. A stripped app
callback may instead declare its observed
fileOffset, exact imageSHA256, observedRun and label; the image hash is required
before registration. Such sites come from actual original call arguments or
the pinned disassembly of an observed handler's direct call/block literal, and
stay in the pinned app declaration, never production compatibility code.
--lifecycle-context then enables entry/return uprobes through
the existing journey launcher, before the app starts, without a debugger entry
stop. LLVM resolves actual Mach-O file offsets and the metadata records image
hashes. Only declared function arguments/receiver/caller addresses are recorded;
no app action is invoked. Each generation owns a private 128 KiB-per-CPU tracefs
instance and removes its events before failure debugger attachment or on normal
exit. The monotonic capture interval is retained. No extra runner package or
separate execution path is used. Setup/cleanup errors, available kernel hit counts,
ring-buffer CPU statistics and original trace records remain beside that
generation's logs. An unavailable kernel facility or an overflowing trace is
not evidence of non-execution. Kernel tracing also changes timing; the original
journey's effects and ordinary execution still require independent acceptance.
The global uprobe_profile counter for a return probe counts breakpoint hits,
not successfully observed function returns. Use the actual return records in
the private instance trace to establish a return within its capture interval.
For a point explicitly marked dispatch-submit, the existing stopped-process
metadata reader also reads at most four observed queue receivers using the
actual libdispatch-registered serial/label offsets. Unreadable labels stay
unavailable; no queue readiness or ownership is inferred from their names.
File-mapping failures have a separate eight-request observation budget, retaining
the original arguments, real descriptor path and saved caller frames even when
successful startup mappings exhausted the ordinary trace limit. This addresses
the original Things run's many later mmap errors without guessing from frequency
or treating a missing image as proof of a mapping cause. Results are unchanged;
the same cache symbolicator resolves these callers without blanket code dumps.
When a declared dispatch-submit trace supplies an actual receiver, the stopped
reader retains at most four 128-byte object-word snapshots in validated readable
non-executable mappings. The same failure symbolicator reads the original
libdispatch worker, root-drain and root-poke functions (at most 2048 bytes each).
Interpret private queue fields only after matching those actual instructions;
upstream source or a queue serial alone is not a layout/ownership verdict.
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

For finished jobs, the workstation follower reports and verifies published
evidence as soon as all job steps finish, even if GitHub's job/run conclusion
lags behind. It continues following the same run; it does not report the
workflow completed or redispatch. Transient evidence failures retry after
30 seconds. Reports separate dispatch-to-first-step delay (dependencies plus
runner wait), completed step time, actual app effect time and diagnosis time.
These are observations, not billing estimates or a pure queue-time measurement.

Apple launch faults keep bounded registers, a same-stop OS mapping table and
atos symbols for at most twelve fault-thread frames in four actual mapped
images. Prefer the mapped image/symbol over an LLDB label when image tracking
failed during exec; retain both sources. No target expression is evaluated.
The original stack plus mappings should arrive together, avoiding a separate
runner iteration just to identify an unknown PC. This enhanced symbolication
still needs hosted validation when another actual launch trace is required.

Read the actual screenshot before changing a boundary. Run 39795030664's
ordinary Apple Things launch crosses the prior SQLite assertion after real
home preparation, but stops on the phone viewport requirement and visibly
shows the original iPad-upgrade dialog. That is a host geometry/dialog
comparison issue, not evidence of Linux's white-screen cause. Keep Linux phone
geometry acceptance strict; do not turn this case green by ignoring its
required user effects. No OCR or AI recognition is needed for this evidence.

Runner time is spent to answer a question, not to validate every edit. Use this
decision table before dispatching; it does not add a new build mode or test suite.

| Change or question | Work before spending another runner | Next execution |
| --- | --- | --- |
| Documentation, report formatting, lost download | Read or retrieve the exact retained run; inspect the diff locally | None |
| Collector path/import/configuration repair | Inspect the whole dependency chain: standalone imports, IPA versus shared-cache backing, caller identity and mount access; perform available local syntax/import checks without executing guest code | One affected case, with the requested observation enabled |
| New compatibility mechanism | State the failed effect, competing explanations, changed boundary and expected distinguishing evidence | One affected original-app case; a focused probe only when needed to isolate the boundary |
| Target effect improved | Compare receipt-verified evidence with the existing baseline | Related journeys sharing the changed implementation |
| Candidate for acceptance | Review focused results and remaining known failures | Full collection once; Apple reference only for a specific unresolved behavior |

Keep a short current plan in EXPERIMENT.md: baseline run, failed effect,
hypothesis, selected cases, observations and how each outcome changes the next
action. Replace that plan when the question changes; do not accumulate a second
historical run diary here. One diagnostic run should observe the known producer,
submission and consumer together where practical. If it answers none of the
competing explanations, do not repeat it with another timeout or unrelated probe.

Classify failure before retrying: infrastructure before app launch, collector
failure after launch, or actual failed app effect. Repairing collection does not
establish compatibility progress. A repeated infrastructure failure calls for
reviewing the full collector dependency chain before another dispatch, rather
than fixing one newly exposed prerequisite per run. Local checks are development
hygiene, never app acceptance; Linux-only behavior still needs the hosted runner.

Use `./build.sh build --app counter --lifecycle-context --wait --evidence` for
one current boundary. It compiles only Counter plus its real shared adapter;
Linux preparation runs concurrently with the SDK job. The app artifact name
binds the producer receipt digest and run attempt; source/run/attempt plus every
product hash are checked before execution. No older app binaries are reused.
`build` without --app or --case fails locally before GitHub dispatch. Full
collection and cross-app acceptance requires explicit --app all; the Actions
form also requires an explicit app input. `cases` lists journeys without using
a runner.

Choose runs by the question they can answer. Before dispatch, write down the
failed effect, the competing explanations, the bounded observations that separate
them, and what each outcome would change. Trace the producer, submission and
consumer together where possible; do not spend one fresh runner on every edge
of an already identifiable call chain. Keep sites tied to the exact input image
and preserve the ordinary journey as acceptance.

An explicitly requested kernel trace must report active setup before the app
launches. A failed setup stops that journey as an infrastructure failure, keeping
setup/state logs and cleanup, rather than waiting for an unobserved app to time
out. If no app launched, skip its screenshot and debugger collection. This does
not establish an app failure or pass; fix the collector before retrying. The two
invalid trace attempts 39715347208 and 39716080783 show why this matters.


1. Read/report the existing exact run first. Re-symbolize retained evidence or
   inspect pinned source locally where possible. A failed artifact download,
   missing log or report error calls for retrieval of that run, never a new build.
2. State the blocked original-app effect, suspected platform boundary, and the
   observation that would distinguish the hypothesis. Run one --case, adding
   --lifecycle-context only if ordinary retained evidence cannot answer it.
   Add a small probe only when the original app cannot isolate that boundary;
   probe success does not establish the original app's cause or acceptance.
3. Check that execution actually reached the changed path and produced the
   required real result. If it did not, improve targeted observation rather than
   adding unrelated journeys or repeating the same timeout without a hypothesis.
   Do not shorten acceptance waits or remove failed effects to save runner time.
4. After the target effect improves, select related existing journeys by the
   shared implementation (for example storage/persistence, scheduling or input).
   Preserve fresh journey state, close/relaunch and actual effect checks.
5. Run --app all once for a candidate revision after focused work, or explicitly
   for a cross-cutting change whose impact cannot be bounded. Keep pre-existing
   failures visible and compare the same journey definitions. Use --reference
   only for a concrete Apple behavior question; it is not a routine extra job.

Avoid concurrent duplicate runs of the same revision/question. Inspect status
before retrying an uncertain dispatch; superseded diagnostic runs may be
cancelled, but an independently useful acceptance run should finish. Do not
automatically cancel other users' runs. Keep ordinary and traced outcomes
separate, because attachment changes scheduling.

The existing Linux ccache already reuses unchanged compilation. Full run
39709336319 recorded 162 hits / 8 misses, but still spent 286 seconds executing
journeys; Things accounted for about 42 seconds. Dispatch-to-completion was
429 seconds, with 56 seconds of toolchain installation and 41 seconds of userland
preparation in the Linux job; the parallel Mac compilation took 65 seconds.
These are measured durations, not a billing calculation or a promise that a
focused run eliminates setup costs. Scope reduction is the first improvement.
The two valid scoped Things trace runs 39716512266 and 39717413546 still took
185 and 206 seconds, respectively. They narrowed request delivery and predicate
consumption, but neither fixed the app effect. Treat that as diagnostic progress,
not compatibility progress. If another run would only reproduce the same failure
without separating an explanation, inspect the existing trace and pinned binary
first. Documentation/report-only changes need no execution run. Do not launch
another full collection until there is a relevant implementation change or a
specific regression question.

Further cache work should target measured repeated preparation and source-owned
SDK products using complete input/output hashes, while retaining fresh run
provenance. Do not cache test results as current execution, or bypass input
verification with an old artifact. Do not add another orchestration framework
or a separate heavyweight cache pipeline before demonstrating its savings.

Resource review of focused run 39739768119: Linux toolchain installation plus
overlapped firmware download took 47 seconds, userland preparation 42 seconds,
core/runtime compilation 3 + 7 seconds, and journey execution including diagnosis
15 seconds. The parallel Mac SDK build took 29 seconds. Compiler reuse was
169 hits / 1 miss. Thus another compiler-cache layer is not the first target;
parallel preparation reduces feedback latency but still occupies two runners.
`report` now records completed job active seconds and runner labels separately
from dispatch-to-completion, including all jobs when --job filters detail.
This is observed runner occupancy, not GitHub billing or a cost estimate.

Prioritize savings in this order:

1. Avoid an execution altogether when retained evidence, pinned source or local
   collector syntax/import checks answer the question. Batch the known producer,
   submission and consumer observations in one selected journey. Do not dispatch
   merely to validate documentation or this resource-accounting change.
2. On implementation changes, run the blocked case first. Schedule related
   regression once the changed boundary is actually crossed, then full acceptance
   once per candidate; do not repeatedly rerun unrelated known failures.
3. If repeated runtime-only edits still make SDK work material, cache the
   source-owned SDK products inside the existing apps job, keyed by all selected
   app/shared-adapter inputs, recipe, target and actual SDK/toolchain identity.
   Verify output hashes and issue a fresh same-run receipt on reuse. This cache
   is not implemented yet; do not claim savings or substitute an arbitrary prior
   run's artifact. Do not duplicate the build graph with manual cache inputs.
4. Profile the 42-second userland preparation before changing it. The explicit
   OS-volume input cache below replaces repeated CDN download/extraction, without
   putting Apple inputs into execution artifacts or releases. A fixed
   source APFS reader took only 1.385 seconds in this run, so caching that
   product cannot explain or remove most of the 42 seconds. Measure firmware
   hashing, extraction and mounting before proposing another product cache.

Fresh app data, actual input/display, persistence, normal close/relaunch and
receipt verification are acceptance work. Their removal is not a resource
optimization. A diagnostic trace run that advances a callback still needs
ordinary execution before claiming the user journey passes.

Cache snapshot writes are conditional on actual compiler-cache misses. In run
39723784073, a 104 MB ccache snapshot restored in about two seconds, then all
170 compilation calls hit; saving a new per-commit snapshot added no reusable
objects. The workflow now records misses and skips that upload when the count is
zero. Cache restore and fresh same-run build/evidence receipts remain required.

The recent shared-cache trace setup exposed backing-file, standalone import and
FUSE-access prerequisites over several separate runs before the app launched.
Run 39727204563 alone spent 153 seconds overall (64 seconds installing the
toolchain, 47 seconds preparing userland) to reach a 0.3-second infrastructure
failure. This is avoidable feedback waste, not app progress. Review these
prerequisites together when changing a collector. The subsequent run exposed the
remapping hazard described above; shared-cache uprobes and the extra FUSE access
have been removed. Historical run details belong in Git/Actions.

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
The workflow restores only build/cache/ios-volumes by a key derived from the
complete iosUserland lock entry, with no fallback restore key. It holds the OS
and Cryptex1,SystemOS DMGs actually mounted by this runtime, not the complete
IPSW, a copied home or an app prefix. Their sizes and hashes were pinned from
receipt-verified run 39739768119. On a cold run the full IPSW SHA-256 and actual
BuildManifest must match before extraction; on every run both DMGs must match
the locked sizes and SHA-256 before mounting. Invalid cache bytes fail explicitly.
Save only after successful preparation; app failure does not invalidate immutable
input bytes. A warm run records inputValidation.mode=pinned-volume-cache and
verifiedFirmwareSHA256=null: it verifies the derived volumes this run, rather
than claiming a fresh whole-IPSW hash. The source IPSW identity remains recorded.
The two raw DMGs total 7,344,446,573 bytes before Actions cache compression; this
is CDN/extraction avoidance, not a small framework subset. Measure actual restore
and save cost before claiming resource savings. Missing files must not be hidden
by pruning framework resources to one observed app trace. Fresh mounts, app data,
execution, receipts and effect checks still run. Cold run 39744136865 saved
5,651,344,265 compressed bytes in 29 s; warm run 39744680047 restored them in
57 s and made zero Apple downloads. Preparation dropped from 42 to 23 s, but
restore plus preparation was slower than the cold preparation/download on a
healthy CDN. This is a fallback against CDN variability, not evidence of overall
resource savings. Both runs passed their selected journeys, not the full suite.
No Apple input is included in
execution artifacts or releases. Current measurements and acceptance are recorded once in
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
unavailable, not passing. Apple firmware-derived inputs use only the named hosted
input cache; Apple core dumps/executables are excluded from result artifacts;
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
Composed screenshots remain the display/effect evidence at the original journey
steps. The main loop no longer rewrites a diagnostic bitmap on every paint.
On failure the existing LLDB collector reads one completed source buffer while
the actual process is stopped, excluding drawing slots and validating the
readable non-executable mapping and a 64 MiB byte budget. It records the original
PID, buffer sequence, slot and dimensions; this is not proof of presentation.
Raw captures publish by atomic rename only after complete write/close. Partial
captures remain unpublished and are removed before the existing PNG conversion;
published-frame size checks still fail on corruption. A successful journey needs
no additional raw bitmap because its actual composed screenshots are retained.

## Runtime boundaries

The existing original-process LLDB metadata read also reports at most sixteen
actual runtime workloops: ownership, worker and priority fields from DWARF,
plus guest queue labels/serials at offsets registered by real libdispatch.
Unreadable metadata and truncation stay explicit. This helps distinguish a
waiting application queue from an incidental SDK failure; it executes no guest
code or queue action. Original IPA images now appear in the same dyld image
metadata record as source-owned apps, without copying their binaries.
The opt-in original-app UIKit observer also reads at most sixteen actual view
controllers and their public presentation/transition-coordinator state. It does
not load absent views, change animation policy, invoke app actions or complete
transitions. This distinguishes a persistent presentation transition from an
application initialization wait without treating either as a proven cause.
It also reads UIApplication's actual protected-data availability: the original
Things binary references that getter. It does not change the state or emit an
availability notification. After repeated 120-second original runs retained the
same upgrade modal, the ordinary Things first-Inbox wait returns to 30 seconds;
the required Inbox/pixels and complete task/relaunch flow remain unchanged.

When the original failed process actually requests com.apple.containermanagerd,
its existing cache symbolication reads the Foundation group-container method and
the corresponding libsystem_containermanager C boundary. The bounded disassembly
is evidence for storage adaptation, not a daemon implementation. Embedded XML-only
entitlements require DER transmutation for the native iOS SecTask consumer; a
successful empty DER reply would incorrectly discard the input's declared groups.
The host loader now serializes XML-only dictionaries before guest TLS entry,
retaining original XML and protecting generated DER read-only. Existing embedded
DER is preserved. Dictionary SET entries are sorted by encoded bytes; arrays
retain order. Booleans, integers, strings, data, arrays and dictionaries have
explicit size/depth/node bounds; other value types fail explicitly. The csops
short-buffer protocol remains ERANGE with a big-endian required length. No
entitlement is inferred from an app ID and no signature trust flags are granted.
Format references inspected: ProcursusTeam/ldid af86971ae72ec3ed3d0a699107c4e882324c941b
and Apple Security SecTask.c db15acbe6a7f257a859ad9a3bb86097bfe0679d9. The
original consumer decoding and real user effects remain required validation.

The opt-in public UIKit geometry observer retains four bounded samples (up to
128 visible views each, including actual layer needsLayout/needsDisplay flags) on its existing 200 ms timer: first window and samples
10, 50 and 125. This observes later controller/layout changes without invoking
layout or actions; frame count and timer scheduling do not imply wall-clock
accuracy. Controller metadata remains in the existing APP_INSPECTION records. The same
opt-in records include up to sixteen connected scenes' public activation state,
delegate class, session role and window association, plus actual key-window state.
No scene state or lifecycle callbacks are supplied by these observations.

src/diagnostics/ios-ui.h owns the optional one-time view-tree diagnostic, using
raw syscalls on the genuine UIKit main loop. Source-buffer capture belongs to
the existing stopped-process collector in tools/app_suite/dyld_metadata.py. They do not
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

Main-executable MH_EXECUTE uprobes are also rejected before registration: the
startup loader copies text from a source mapping to anonymous executable memory,
which can copy a kernel-owned breakpoint out of its original mapping. Run
39767427316 trapped at the newly probed delegate entry, not a proven application
fault. Use the public scene metadata observer for this lifecycle question.

For a blank original-app screen after verified foreground activation, the opt-in
lifecycle observer forwards actual CADisplayLink factory and scheduling calls
unchanged. It retains at most sixteen weak link references, logging original
target class/selector and public timestamp/duration/paused/FPS at bounded samples
0, 50, 100 and 250 on its existing 100 ms timer. No target is invoked and no
refresh is fabricated. This extends that optional timer observation to 25 seconds;
actual timing still depends on the guest run loop. Missing/ticking links alone
do not prove the cause of an app initialization stall.

Original main-image delegate observations use the existing startup LLDB profile,
after macho-image.h has copied the program and the actual main entry is reached.
LLDB owns and removes breakpoints in that mapped code. The manifest declares
Objective-C method names, never numeric program offsets. This avoids copying
file-uprobe traps into anonymous memory; it remains timing-altering diagnosis,
not an ordinary journey acceptance run. The metadata resolver's inspection of
Things 3.17.1 resolves connect/foreground/active to text offsets 0x52c9f4,
0x52cb38 and 0x52cb40; only the class/selector names are configured.

A diagnostic symbol may name callbackBlockRegister to observe an actual supplied
ARM64 Objective-C block. The existing debugger reads its 32-byte Blocks ABI
header (invoke at offset 16), checks readable non-executable metadata and an
executable aligned invoke address, then observes that address. Nil is recorded
as absent. No block is copied, retained or invoked. First invocation is evidence
of actual execution, not proof of user effects or captured-state correctness.

Observed original method returns include raw x0 metadata. Interpret it only
against the actual method ABI: a boolean-return method's value differs from an
object pointer, void result or floating-point return. Geometry samples also read
public per-view trait displayScale, independently of UIScreen.scale.

A named original diagnostic symbol may request observeRegisters (at most eight
ARM64 x0..x7 or d0..d7 names). LLDB records their actual textual register values
at the real entry. Floating-point arguments must be read from their ABI registers,
not inferred from x0 or a different public object's similarly named property.

The opt-in geometry observer also records storage presence for app-owned visible
view classes: up to sixteen declared ivars, runtime-adjusted offsets within the
actual class instance size, and up to sixteen bytes per field. It emits only
field name/offset/bytes-inspected/allZero. It calls no app getter, follows no
stored pointer and exports no field payload. Empty Swift ivar type encodings
are never cast to object references; allZero is storage evidence, not a general
Swift optional/value interpretation. Apple framework classes are excluded.

The four opt-in geometry samples also include the real window CALayer hierarchy,
not only UIView descendants: up to 256 nodes/depth 24 with parent indices, actual
bounds/position/transform, visibility/opacity, content/delegate class names and
mask metadata. No layer is forced to draw/layout and no contents payload is
exported. This distinguishes absent view labels from possible layer-only content;
it does not certify composition or readable pixels. Truncation remains explicit.

Do not equate a synchronous application update block/didFinishUpdate with timed
Core Animation completion. The separate probe-animation journeys observe genuine
UIView completion and CAAnimationDelegate start/stop callbacks, with measured
elapsed time and externally visible results. Apple describes the model,
presentation and render trees separately in its [Core Animation guide](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/CoreAnimationBasics/CoreAnimationBasics.html),
and [animationDidStop:finished:](https://developer.apple.com/documentation/quartzcore/caanimationdelegate/animationdidstop(_:finished:))
distinguishes reaching the animation duration from removal. A passing probe is
not evidence that the original Things app uses the same failing boundary.
