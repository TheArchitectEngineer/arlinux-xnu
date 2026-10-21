# Current evidence and next boundary

## Goal and fixed input

Run basic real iOS apps on Linux ARM64 using selected Apple framework semantics
and Linux-backed platform services. Counter must show real UIKit content,
respond to external input with changed state/pixels, close normally and relaunch
interactively. Console/Foundation and Swift/UIKit are independent regressions;
the existing app collection and multi-process scenarios remain required.
Basic Linux GUI flows below pass; the full collection, third-party IPA and
Android acceptance remain incomplete.

sources.lock.json pins iPhone10,1 (iPhone 8/A11), iOS 16.7.16 build 20H392,
6,035,280,776 bytes, SHA-256
601cb23e740dacf8b61271fe02ffd5b3c146b518abb5a6e6b286e5f0475fe43b.
Actual OS and Cryptex1,SystemOS volumes form the read-only filesystem. Apple
executable data stays runner-local. Compilation/execution use hosted runners
through build.sh. The collection has 11 source-owned apps, 13 short journeys
and three cross-app scenarios; selected runs never certify the whole collection.

## Verified focus and reactivation

Selected run [39543092531](https://github.com/taowen/arlinux-xnu/actions/runs/39543092531),
source **9a875fe712eea34ec3005de68bc0bda8878f716c**, attempt 1, passes all 22 steps
of session--focus-activation. Receipt
**b06feda94ad95ae620a8a66f203c1adbbc4229691267ef63feca24877cd1dcca**
and all 29 retained files were verified against the producing log and hashes.
Counter **5831/48779** and Workers **5865/49140** (PID/startTicks) follow actual
compositor focus. Original UIApplication state changes active/inactive with real
notifications. Counter reaches didBecomeActive=3 / willResignActive=2, remains
interactive with Count 1/2 composed pixels, and both apps exit normally with 0.
This proves visible-window focus, not minimize/background, resize or Android.

The original scene updater receives immutable settings/diffs on its own queue;
its opaque completion object is not acceptance. Original HangTracer also needs
shared anonymous memory on reactivation. Actual Linux MAP_SHARED backing fixes
that shared runtime boundary without a HangTracer patch. The dedicated flow takes
12.362 s, 134 s including preparation/queues. Compiler cache: 167 hits, one miss;
toolchain setup 38 s and userland preparation 43 s dominate preparation. These
are measured costs for this run, not a general latency guarantee.

## Working adaptation boundaries

Notes now passes its complete edit/save/reopen journey in selected run
[39537811333](https://github.com/taowen/arlinux-xnu/actions/runs/39537811333),
source **45c9b1f54025bb66d5fd5fec9f0b3abeb8c4a3d4**, attempt 1. Console,
Foundation, Counter and Swift/UIKit also pass at that source. Receipt
**ed4215d621f1d1231096ec4c811038ff23836ddec6fa23a714054ee7a86dd6c4**
and all 74 retained files were verified. This remains a partial collection.
Notes starts empty, receives actual keyboard input `app-note-42`, saves through
original Foundation atomic file writing, closes with exit 0, and reads the
content after a new-process launch before another exit 0. Process identities are
**6113/92769** and **6197/93216** (PID/startTicks). The saved label region hash
**c2480e721003b4d440cdf1b11d286c22e6fde6355850acf930dcc4d1507817cb**
matches after relaunch; initial blank frames remain rejected. Actual Linux
temporary-file creation and rename are retained in the runtime log. This run
took 156 s overall and 30 s in journeys.

- Genuine FBSScene agent creation enters original UIKit launch. Local CAContext
  and CPU CALayer backing preserve original layout/text/drawing. Actual resilient
  file mappings load Helvetica/SFUI. This is software composition, not GPU proof.
- NSURL file reachability uses real Linux stat and real Cocoa/POSIX errors;
  original CoreUI resource parsing produces systemBlueColor. Extra filesystem
  metadata support alone had not fixed that failure. No fixed color is supplied.
- Actual Wayland seat pointer transitions enter a per-process bounded queue.
  The guest main loop creates IOHID/UITouch events, uses original hit testing and
  UIApplication event dispatch. The harness sends only external desktop input.
- Host Alt-F4/xdg-close enters original UIApplication termination. Counter and
  Swift verify normal process exits and fresh identities. The input harness
  checks actual pointer coordinates rather than waiting for movement when the
  pointer is already at its target.
- Ineffective NSError-construction observers are removed; original-process
  fault/unsupported-call/LLDB evidence remains available. Historical diagnosis
  failures and their repairs remain in Git/Actions rather than a growing diary.

- The pinned libspc fork **654ecfd5fb9ffbea2e86242bc4d87cf83a274459** now
  decodes bounded inline/OOL dictionary spans with owned values and transferred
  rights. Web's actual 32,768-byte CPX5 body was decoded, then released through
  the real Mach VM deallocation RPC with status 0. The session continued handling
  later requests. Unknown bootstrap operations remain unsupported; no helper
  launch is claimed. Decoder changes live in the public fork, not a build patch.
  The temporary raw framing probe was removed after establishing this layout.

## Complete collection and remaining failures

Full run [39564800794](https://github.com/taowen/arlinux-xnu/actions/runs/39564800794),
source **d88d586edfc530138f91b2ac32916f7e80b70e1a**, attempt 1, passes **14/16**
journeys. The collection is still **failed**. Receipt
**a706fefc4630de978c19fc3def9bb55437e1d219d45742e01110400deb89f8fe**
and all 257 retained files were verified. Individual journeys total 175.825 s,
including 27.552 s of diagnosis. Total feedback is 323 s; firmware transfer takes
15.611 s, toolchain setup 47 s and pinned-userland preparation 46 s. This full
regression retains the 14 passing journeys after the virtual-memory fixes.
Both WebContent helpers now pass the real 38 GiB reservation and reach the
service metadata boundary below. This is not a Web journey pass.

Console, Foundation, Counter, Swift, Notes, Canvas, Process and all three Workers
journeys pass. Concurrent relaunch passes all 29 steps; crash isolation and focus
activation each pass all 22 steps. These scenarios do not establish arbitrary
helper cleanup or background lifecycle behavior. The earlier kmsg preallocation
regression is repaired: auxiliary ownership adds to the allocation and leaves
the original inline-message capacity intact.

Network passes all 14 steps. Processes **7288/39683** and **7338/40128**
(PID/startTicks) each receive an actual status 200 / 13-byte response through
Linux libcurl and original NSURLSession completion. JSON parsing and main-queue
delivery produce Network: 42 in composed pixels, the peer records two requests,
and each app exits 0. This proves foreground HTTP across close/new-process
relaunch. HTTPS, redirects, cancellation and background networking still need
independent app journeys; NECP and arbitrary BSD sockets are not implemented.
The earlier selected 4/4 run 39552858185 also passed this flow, with its second
composed screenshot visually inspected.

Workers queue now requires successive Queue 3 and Queue 6 effects in each of two
fresh processes, with normal exits. This full run passes that stronger flow.
The earlier no-main-stage failure in run 39545899351 remains an intermittent
observation; one full pass does not prove its isolated cause or general repair.

| Remaining journey | Actual failure evidence / next boundary |
| --- | --- |
| Web | Latest selected run below passes actual bootstrap/service metadata and JavaScriptCore suspension/resume. Real service queue/check-in handoff is implemented; the original network connection still fails. No DOM or helper-lifecycle acceptance. |
| Share | Process 6058/26678 does not display EchoExtension. The actual session also rejects discovery of com.apple.extensionkitservice. Extension discovery, launch/payload/retirement have not passed. |

Selected run [39555581254](https://github.com/taowen/arlinux-xnu/actions/runs/39555581254),
source **35927cc3076e52cfb342ce06a0f2da89f0b1213f**, attempt 1, passes Canvas and
Counter but **fails Web** (2/3). Receipt
**543768b1c2cbf93d5c8a67aad1a451c01d531d2e27c3f113c4ea119c36941b34**
and 49 files were verified. Original WebCore getters return the configured CPU
limit 8192x8192 and four-byte BGRA row alignment; Canvas actually repaints and
Counter still closes/relaunches interactively. Web process **5987/22843** no
longer aborts at the IOSurface limit/alignment queries, but WebCount 0 never
appears in the 8.019 s effect wait. Its real main run loop is waiting, not a
completed page. Total feedback is 156 s, including 9.542 s of diagnosis.

The actual session receives type=5/flags=32 instance-scoped discovery for
com.apple.WebKit.WebContent and com.apple.WebKit.Networking from that audit
identity, then returns error 45. Their nested bootstrap requests (ID 0x400002c6)
also remain unsupported. The next boundary is real Linux-owned helper launch,
bootstrap/connection and retirement, without an empty success endpoint or a
synthetic DOM callback. No IOSurface driver, GPU allocation or Web pass is
claimed. Targeted cache reads of the original configuration interfaces replaced
unrelated standing QuartzCore dumps; the original fault/run-loop evidence stays.

In the current full run, Web **6281/29591** supplies its original bootstrap
dictionary. WebContent children **6321/30067**, **6344/30245**, and Networking
child **6350/30249** run in the headless service role under session **6278/29585**.
Actual PID/start ticks bind each log. Both WebContent processes map the original
0x980000000-byte (38 GiB) private reservation at 0x400090000, after dyld. The
allocator searches real holes with MAP_FIXED_NOREPLACE and uses MAP_NORESERVE;
it preserves the guest address limit and does not disable Gigacage. Earlier
rejections exposed three separate boundaries: a 4 GiB cap, whole-span cursor
skipping over dyld, and Linux commit accounting for the sparse writable mapping.
All three children subsequently request operation 0x2c7 and receive the caller's
original bootstrap dictionary plus its real 32-byte creator audit token. No
nested environment values are logged. All then request 0x2ce and fault in the
original xpc_main -> _xpc_copy_xpcservice_dictionary direct-launch rejection.
The exact reply consumer expects `identifier` and `xpcservice-dict`; implementing
that operation from actual bundle metadata is the next boundary, followed by
real check-in/port handoff and the original DOM/input journey.
Firmware preparation registers actual service identifiers/paths once; the
index hash is 9356cbc9a2e8d549d4acc5708b23c18ad1d04057ae9b0b5afaf990e1a5ca5868.
Runtime validates the indexed bundle instead of repeating a full FUSE glob in
the synchronous session loop. Original session and both WebContent LLDB
collections complete; the third child is explicitly omitted from attachment,
with its actual fault records retained in session.log. No DOM, usable connection,
normal helper retirement, third-party IPA or Android acceptance is claimed.

Latest selected run [39589004917](https://github.com/taowen/arlinux-xnu/actions/runs/39589004917),
source **66377b31d7b7244c8e5f8c22781f1fec8d495169**, attempt 1, passes Counter
and fails the same Web DOM step (1/2, partial). Receipt
**ab3fded0ae98fbec216b1948b71ea9f4df01a2d6bd7657a9a054ba3647bfe527**
and 47 evidence files were verified. App **5996/39571** shares owned session
**5993/39566**. WebContent **6035/39853** now has original IPC::Connection
message dispatch -> WebProcess::initializeWebProcess -> WebCore::commonVMSlow ->
JSC::VM construction in its retained fault stack. This is evidence of actual
client/helper message consumption beyond the receive-grant producer marker.
VM construction traps at **0x19a37c2d8**, with original crash metadata including
stack boundary **0x300100000** and anomalous bound **0xffff55003a506270**.
The root cause is not yet established; next read original pthread stack-query
and StackBounds consumers before changing TLS, stack sizing or checks.
Networking **6057/40269** separately exits with wait status 256 in the original
getConnectionIdentifier/getClientIdentifier failure branch. Same-cache code
confirms real server-port extraction and nonempty client-identifier checks;
which check failed is unproven. Replacement **6066/40330** is retained too.
Failure collection now chooses live distinct services and symbolizes the closed
session log after retirement, preserving the previously missed late VM fault.
Total feedback is 166 s; journeys take 38.927 s including 23.338 s of diagnosis.
No DOM or helper-lifecycle acceptance. The previous full 14/16 result remains
source-specific; the full goal remains active.

Each launch has an explicit Linux-backed private home/container. Firmware and
bundles remain read-only; writes target the actual private container. Primary
pointer contacts, Alt-F4 and actual host keyboard text commits/deletion are
implemented. Notes verifies basic keyboard text entry and persistence;
scroll, IME composition, key repeat and multitouch remain unsupported. Scene
resize/background behavior, broader networking, WebKit and extensions require further work.
No third-party IPA or Android acceptance has passed. The full goal remains active.

The sole entry can select exact app journeys and scenarios through --case;
SDK compilation and execution use the same dependency selection. Comparison
reports require both runs' verified receipts and unchanged journey definitions.
Selected runs remain partial. Feedback measurements belong to the exact run,
not a latency guarantee; firmware stays transient, hash-verified and runner-local.

The latest fork guard is verified separately in selected run
[39546263777](https://github.com/taowen/arlinux-xnu/actions/runs/39546263777), source
**de54ab793d74584732bdb501aaa58f7ae9190b6d**, attempt 1. Receipt
**aa1d6447dd52f4c88d054224faf204ff531b160a001b4ca4f0de2eb64462cbd5**
and 16 files were verified. Parent **6040/270974** and child **6045/271192**
(PID/startTicks) use distinct actual tasks; TASK_AUDIT_TOKEN matches each real
PID, and each allocates/releases its own receive port. Parent private memory
stays 11 while the child's private write is 99; shared memory becomes 42.
The child exits 37, the parent reaps it, accepts external stdin and exits 0.
Full run 39545899351 also passed this strengthened flow (parent **6097/17298**,
child **6098/17543**). The selected run takes 125 s overall / 2.337 s journey.

The previous child failed at original MIG reply-port construction with
MACH_SEND_INVALID_DATA (0x10000002), not MACH_SEND_INVALID_DEST. That identifies
a child RPC/memory boundary failure, not a proven invalid destination right.
The fix renews the child's exact-core memory-access authorization and sockets,
refreshes actual task/host/thread rights and libpthread registration, and
registers its real COW dyld metadata. A pipe holds the parent until registration.
The complete boundary repair is verified; an isolated cause among those missing
initialization steps has not been established.
This proves the single-threaded pre-workqueue console boundary; live framework
sessions/kqueues, multithreaded fork and arbitrary helper lifecycle remain
unsupported. It is not an ordinary iOS application lifecycle claim.

Apple-host reference run
[39456469593](https://github.com/taowen/arlinux-xnu/actions/runs/39456469593)
previously passed the same four-app subset with native desktop events. It remains
a behavior reference, not evidence of Linux/Android capability. Architecture and
diagnostics procedures live in their own documents; no OCR, model recognition,
readiness marker or direct app/control action establishes acceptance here.
