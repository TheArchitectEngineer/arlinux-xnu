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
through build.sh. The collection has 11 source-owned apps, 14 short journeys
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

Full run [39610149135](https://github.com/taowen/arlinux-xnu/actions/runs/39610149135),
source **0324dec9a985a70043fe6c2cc14bb909ee02df09**, attempt 1, passes **15/17**
journeys. The collection remains **failed**. Receipt
**ee366e2be3cf22eeb3e84148cac63f1cbee68adcad2c7a6d8eb82f05c60296d7**
and all 333 retained files were verified. Journeys total 223.2 s, including
30.8 s of diagnosis; dispatch-to-completion is 345 s. Compiler cache has 170
hits and zero misses. The new named-memory alias/fork flow passes alongside
all 14 previously passing flows. Web and Share remain failed.

Console, Foundation, Counter, Swift, Notes, Canvas, Process and all three Workers
journeys pass. Concurrent relaunch passes all 29 steps; crash isolation and focus
activation each pass all 22 steps. These scenarios do not establish arbitrary
helper cleanup or background lifecycle behavior. The earlier kmsg preallocation
regression is repaired: auxiliary ownership adds to the allocation and leaves
the original inline-message capacity intact.

The earlier full run [39604373891](https://github.com/taowen/arlinux-xnu/actions/runs/39604373891)
passes Network's 14 steps. Processes **7842/46583** and **7891/46948**
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
| Web | WebContent rejects export flags 0x500043, then traps in StreamConnectionBuffer::createHandle. Networking also waits on original synchronous preferences. No DOM or helper-lifecycle acceptance. |
| Share | Process 6226/64986 does not display EchoExtension. The actual session also rejects discovery of com.apple.extensionkitservice. Extension discovery, launch/payload/retirement have not passed. |

Web app **6445/67882** shares owned session **6442/67876**. WebContent requests
2,097,408 bytes at **0xd92d64000**, protection **0x500043**: READ/WRITE,
VM_PROT_IS_MASK, MAP_MEM_VM_SHARE and MAP_MEM_USE_DATA_ADDR. The implementation
accepts plain READ/WRITE only and returns unsupported for this flag combination.
The original consumer traps at **0x1990c9b60**; the shared session no longer aborts.
The implementation builds named entries from real Linux memfd backing, reusing Darling's
shared-remap descriptor/tree ownership and XNU's real port/no-senders lifetime.
S2C file mapping transfers an actual FD; source and target aliases retain the same
backing. The former no-op-success mach_vm_map_external is replaced by actual mapping
or an explicit error. Source overlap/permissions, mapping identity and descriptor
ownership are checked. Concurrent snapshot/rebind conversion and per-unmap registry
retirement remain limited; general shared-memory correctness is not claimed.
Selected run [39609101471](https://github.com/taowen/arlinux-xnu/actions/runs/39609101471),
source **c02e04b1a811cdf60ac1acb90eec5319298b42fd**, attempt 1, passes Counter,
Process fork/COW and the new Process named-memory command; Web fails (3/4,
partial). Receipt **adb60b3062d44b368c0894a48aa1b0df349e8f881c35274aa8b110f7e881c030**
and 150 files were verified. Process **6024/67764**, session **6021/67758**,
uses app binary **e1a0c40a94504881a6a818b1f2e3429a9b382d8330da33806be45de777a7fd91**.
The original SDK-supported vm interfaces export **0x3021a8000**, map live alias
**0x3021b0000**, preserve initial 23, observe alias-to-source 31, drop the named
port, then observe real child exit 39 and shared value 57. Source-to-alias 61
still works; both unmaps succeed and the app consumes q and exits 0 without
forced cleanup. The post-start memory command takes 0.05 s to produce its real
observed answer. This proves that path, not unrelated-task port transfer, arbitrary
concurrent export or per-unmap registry reclamation. The earlier SDK header build
failure was corrected using iPhoneOS's actual vm_* interfaces, with no fabricated
SDK declarations. Web remains blocked in original Networking preferences before
reaching the new memory boundary. Total feedback is 225 s; journeys take 82.275 s
including 24.008 s of diagnosis. The collection now contains 17 journeys; its
expanded full regression is pending. Resolved identifier/stream diagnostic dumps
are removed in favor of the actual preferences producer/consumer pair.

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

## Current candidate

The next candidate accepts aligned shared-range extraction with USE_DATA_ADDR
(data offset zero) and applies VM_PROT_IS_MASK against the actual source mapping
before backing conversion, following the in-tree XNU vm_user.c semantics. Other
flags and unaligned data-address extraction remain unsupported. The existing
Process named-memory command now requests the same flag combination as original
WebKit; this candidate still needs runner app-flow validation. Networking's
preference producer is inspected separately; no cfprefsd service is fabricated.
