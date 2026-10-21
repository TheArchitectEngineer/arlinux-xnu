# Darwin/iOS compatibility architecture

Native ARM64, 16 KiB guest pages, one actual CPU affinity, non-arm64e, selected
iOS 16.7.16 userland. The goal is useful application behavior on Linux/Android,
not an unchanged XNU boot. Current compatibility is incomplete. All compilation
and execution use GitHub runners through build.sh.

## Why the experimental structure needed replacement

The earlier feedback loop rewarded one more successful lookup or a longer-lived
process. It mixed private wire codecs, process ownership, UIKit attachment,
renderer policy and diagnostic dumping. A zero-sized window reached a readiness
callback; a black buffer reached a compositor frame callback. Neither meant an
app worked. A wrong syscall ID also left an implemented operation unreachable.
These are architecture and acceptance problems, not reasons to add more stubs.

Rewrite policy: preserve verified memory/thread/Mach behavior and the imported
Darling/XNU implementation; replace the surrounding ownership/API/verification
boundaries. Moving files alone is not completion. Unsupported behavior must
remain an observable failure, and each replacement must run real app journeys.

## Ownership and interfaces

### Selective userland reuse

The compatibility boundary follows application effects, not the boundary of an
unchanged Apple binary. Preserve complex shared semantics when that saves work;
replace platform dependencies when reproducing their Apple implementation would
cost more. A whole framework need not be retained or replaced as one unit.

Prefer a native, in-process adaptation at the lowest shared boundary that still
avoids reproducing Apple platform machinery. Keep Objective-C objects, resource
parsers, layout and event semantics above that boundary. Back file access,
windows, input and process ownership with actual Linux capabilities below it.
Cross-process Mach transport is for real cross-process consumers, not a required
round trip for every local platform operation. Never substitute a successful
result for an operation whose app-visible effect is required.

XPC dictionary decoding is maintained in the pinned libspc fork. The native
receiver supplies the actual bounded inline span or single OOL CPX5 body; both
use the same decoder. Nested objects have checked byte/count/depth bounds and
return an error instead of terminating the service on a value decoding failure.
The decoder adopts transferred send rights into owned values and releases
unconsumed rights. The receiver releases the delivered OOL mapping through real
Mach VM deallocation after decoding, without copying an extra full body.
Received OOL bodies are limited to 16 MiB by the native receiver. OOL port arrays,
receive-right adoption and arbitrary descriptor forms are still unsupported;
this is not a general untrusted-peer XPC implementation. Decoding a bootstrap
request does not implement the requested service operation or helper launch.

An unsupported syscall is a diagnostic observation, not a work queue. Establish
its causal connection to a failing app effect before expanding the adapter.
In run 39523544166, resource metadata and filesystem queries succeeded but the
Counter button remained invisible with the same frame hash and CoreUI file-read
error. These ABI additions have not demonstrated progress on that GUI failure.
The subsequent run 39524469630 verifies the narrower shared NSURL file
reachability boundary: actual Linux stat replaces Apple's volume/property
checks, and original CoreUI parsing produces the real blue button. Keep data
objects and resource parsing original. No fixed color or app rule is supplied.

Use multiple independent app journeys to check reuse. A boundary should solve a
shared capability without app names, firmware offsets or per-app patches. Native
execution avoids instruction interpretation; shared buffers and batched host
events can avoid unnecessary IPC/copies. Current software composition still has
copying and CPU costs: hardware acceleration and zero-copy are not established.

Guest anonymous memory remains within the actual low address geometry required
by the retained Objective-C runtime. The maximum virtual reservation follows
that geometry rather than a 4 GiB byte cap. A cursor is a placement hint;
MAP_FIXED_NOREPLACE owns the real range atomically. On conflicts, a bounded
raw-syscall reader searches actual /proc/self/maps holes, so a large reservation
can fit after dyld without replacing it or skipping the entire requested size.
Private anonymous mappings use Linux MAP_NORESERVE for demand-paged backing,
following WebKit's Linux allocation strategy; allocation still reports actual
kernel errors and does not change host overcommit policy. Shared anonymous
memory retains real shared backing. See the [WebKit allocation discussion](https://bugs.webkit.org/show_bug.cgi?id=183329)
and [Linux commit accounting](https://docs.kernel.org/mm/overcommit-accounting.html).

The ARM64 suspension bridge follows Darling's existing interrupt-enter,
thread_suspended and interrupt-exit RPC sequence. It saves actual integer/NEON
signal context, waits on the integrated XNU suspend count, restores the returned
state and resumes the interrupted thread. This is needed by JavaScriptCore's
allocator scavenger; ignoring its suspend notification would violate allocator
synchronization. The signal number comes from the same generated header as the
core. General debugger register editing and arbitrary suspension workloads have
not been independently accepted by the app collection.

| Capability | Preferred implementation | Required observable effect |
| --- | --- | --- |
| Objective-C/Swift, Foundation objects and serialization | Original userland where usable | Real language/object behavior across independent apps |
| UIKit controls, layout and text | Original UIKit and reusable text/graphics code | Actual views, hit testing, layout and changed app state |
| QuartzCore/CoreGraphics output | Reuse usable drawing; adapt or replace platform/compositor parts | Real app content submitted to the host surface |
| Window, focus, keyboard, pointer/touch | Wayland backend with a guest input bridge | Host input reaches the real UIKit event path |
| Application launch, activation, scenes and termination | An owned Linux-backed lifecycle adapter | Coherent scene state and real normal close/relaunch |
| Memory, threads, files, network and Mach transport | Linux operations plus shared Darling/XNU semantics | Actual storage, synchronization, communication and process isolation |
| Private services and device/privilege policy | Only required behavior; replace expensive dependencies | App-visible effects; no requirement to recreate SpringBoard/launchd wholesale |
| Web content | Evaluate original WebKit reuse and a host-backed engine boundary | Real navigation, scripting and embedding; neither path is implemented by this decision |

WebKit's WebContent and Networking helpers are browser-engine workers, not
prerequisites for every UIKit app. Original helper reuse is a bounded experiment:
retain engine semantics if a small Linux-owned launch/IPC/display boundary makes
them usable. Do not reproduce an expanding Apple service hierarchy to preserve
their bootstrap protocol. If that dependency dominates, move the replacement
boundary up to the web-view platform implementation. A host WebKit port still
needs WKWebView object/callback, JavaScript bridge, storage and embedded-input
semantics; displaying a separate browser window would not satisfy the contract.

Wayland does not implement UIKit scenes, event delivery, text layout, CoreGraphics
or Metal. The adapter must bridge those responsibilities explicitly. Retaining
original QuartzCore does not imply that its Apple GPU/compositor backend works.
The present software layer renderer is a starting point, not completed graphics
compatibility.

The foreground HTTP boundary uses a public NSURLProtocol subclass in
the injected platform library, preserving original NSURLSession task/client and
Foundation response/JSON semantics. Linux libcurl owns actual HTTP/HTTPS, host
DNS and normal certificate validation on a host-TLS pthread started before guest
entry. Sized platform API v5 submits copied requests and polls immutable results;
no guest object/callback enters host libc. Eight bounded slots share one serial
transport worker, with real timeouts/cancellation and explicit response release.
Requests/responses are buffered to 16 MiB, headers to 64 KiB. Foundation receives
actual status, headers, bytes and errors; it owns redirect decisions. Background
sessions, streamed request bodies, custom trust/authentication challenges and
complete ATS/cookie/cache compatibility are not established. Insecure HTTP is
limited to explicit arbitrary-load policy or explicitly allowed local names;
unimplemented exception-domain rules fail closed. The foreground HTTP/JSON flow
passes in two fresh process generations with real peer requests, visible pixels
and normal exits. HTTPS, redirects and cancellation still need their own app
journeys before compatibility is claimed. It does not
implement NECP or arbitrary BSD sockets, WebKit networking or Apple services.

The first transport attempt exposed a real CFRunLoopWakeUp send rejection:
options 0x500000011 specify a two-vector message, and the native adapter rejected
nonempty auxiliary data. The candidate internal Mach RPC now carries bounded
opaque auxiliary bytes into the actual kmsg, preserves ownership across queueing
and blocking receive continuations, copies them to an offered receive vector,
and releases them with the message. Scalar receivers omit optional metadata;
vector receivers get actual metadata or empty EOF. No success is returned for a
discarded send. Auxiliary sizes are bounded to 16 KiB; full modern oversized
receive/pseudo-receive behavior remains unverified. The end-to-end HTTP flow now
passes; byte-for-byte auxiliary receive semantics are not independently certified
by a wakeup flow. Ownership fields add to the fixed kmsg allocation rather than
reducing its original inline/preallocated-message capacity.

The software backend's ios-rendering.m selects the original CAContext local
constructor when UIKit requests its window context. It preserves original
context/layer behavior while avoiding a nonexistent Apple render server. Hosted
run 39514010952 confirms original UIKit text in composed pixels. ios-input.m owns actual
UIKit key-window focus locally instead of exporting the local scene token to
Apple's remote keyboard arbiter. Primary Wayland pointer input, normal close and relaunch pass Counter and
Swift/UIKit journeys. Host keyboard text commits and deletion enter the
actual first responder's UIKeyInput methods. Notes verifies basic text entry,
atomic save and new-process readback; broader text-input compatibility is not
established. Keyboard availability
comes from the actual Wayland seat and is shared consistently with GraphicsServices
and UIKit. Key repeat, IME composition and general key commands remain unsupported.
The backend replaces CALayer's default `_display`
storage with a CPU bitmap/CGImage and keeps original public display delegation,
layerWillDraw and drawInContext behavior. It supplies a balanced UIKit graphics
context and the actual view's current-trait drawing environment; real dynamic
system-blue output is verified. CoreGraphics/CALayer still compose actual app
content into the Wayland surface. No IOSurface driver is advertised. The latest
verified frame shows the real Counter label and blue button. External clicks
change app state and composed pixels, including after a fresh-process relaunch.
Remote context sharing is unsupported.

WebCore's existing setMaximumSize(IntSize) platform interface supplies the same
8192-per-dimension bound enforced by CPU CALayer backing. The actual iOS 16.7.16
setter/getter ABI was inspected from the producing cache; no firmware address or
version switch selects it. The original getter reads this configuration before
attempting an IOKit limit query. This publishes software backing policy, not an
IOSurface kernel driver, successful surface allocation or GPU capability.
Unavailable exports retain the original unsupported path. WebContent process
launch, surface sharing and actual DOM rendering still need end-to-end proof.
The paired bytesPerRowAlignment platform configuration uses four-byte BGRA row
alignment, matching the actual width * four bytesPerRow passed to
CGBitmapContextCreate. It uses WebCore's own scalar setter/getter interface;
the input semantics are declared in WebKit's
[IOSurface.h](https://github.com/WebKit/WebKit/blob/main/Source/WebCore/platform/graphics/cocoa/IOSurface.h).
Run 39555581254 verifies both original getters and passes Canvas/Counter again.
Web now reaches actual WebContent/Networking discovery rather than aborting in
the IOSurface property queries; no DOM content has passed. Real helper launch,
connection and retirement remain required under the Linux-owned session.

The session's bootstrap-setting operation retains the actual codec-owned
initialization dictionary and Mach rights per audited caller PID/incarnation
and service name, with a pidfd-backed lifetime. Replacing or retiring a context
releases its real ownership. Its acknowledgement means configuration stored;
it neither returns a connection port nor claims a helper was started. Actual
instance-scoped service check-in and usable connection remain required.

The candidate Linux service launcher keys one-shot instances by audited caller,
service and actual UUID. Firmware preparation registers actual XPC bundle
identifiers/relative paths once in a root-bound plist index, recording its hash
with the input identity. This is operational lookup data, not a feature checklist
or evidence of service compatibility. Runtime launch reads only the indexed
candidate bundles, revalidates their identifiers and confines their canonical
executables to that firmware root. It resolves CFBundleIdentifier/CFBundleExecutable
with libplist and executes the original Mach-O
through the same native launcher in the headless service role. Root, dyld and
container come from the verified caller's real command line, not guest-provided
paths. Only explicit session variables, XPC_SERVICE_NAME and an explicitly
enabled stop-on-fault diagnostic option enter the child; host credentials are
not inherited. pidfds, real exit status, caller-death
termination and a parent-death signal own its process lifetime. Starting an ELF
launcher or entering a Mach-O is not XPC readiness. Instance lookup creates a
real Mach queue only for a validated firmware service. Check-in transfers its
unique receive right to the audited child, preserving the actor send reference
for subsequent lookups. Queued messages remain in the kernel during startup.
Child/owner failure retires the endpoint; a successful handoff is not an app effect.
The bootstrap-copy candidate returns the caller's original dictionary and
actual 32-byte kernel audit token only to its owned child PID/incarnation. The
session retains the original objects/send rights; reply descriptors copy send
rights instead of consuming that ownership. Unsupported right forms or oversized
inline responses fail. This supplies real initialization data, not a ready
connection or a fabricated managed-service identity.
Service-metadata replies resolve the caller's actual selected firmware bundle
and return its XPCService plist with the validated identifier, only to the
owned child PID/incarnation. Scalar/container values retain their actual types;
unknown types, depth/node limits and oversized replies fail. Metadata is frozen
from the validated bundle when the instance is created. Metadata and endpoint
transfer do not certify the original client/helper connection or DOM rendering.

Font files use Darwin MAP_RESILIENT_MEDIA. The memory adapter provides private
eager file snapshots with zero-filled EOF/unavailable media, real descriptor
validation and final memory protections. Shared mappings follow XNU's existing
ignore-MEDIA rule; resilient executable mappings are explicitly unsupported.
The real Helvetica/SFUI files now map successfully and original text renders
instead of LastResort glyphs. This is common mmap behavior, not a font-name fix.

File resource metadata uses the same Linux object as actual reads. The
getattrlist adapter holds an O_PATH descriptor, reads statx/fstatfs and packs
the XNU four-byte attribute ABI with actual returned masks. Missing birth time,
Finder metadata, data-protection flags and other Apple-only fields stay absent;
ctime is never substituted for birth time. Unsupported non-mask requests fail.
Filesystem enumeration exposes only the declared root/bundle filesystems, using
the same fd-backed statfs metadata; no Apple filesystem identity is invented.
ios-filesystem.m implements file URL reachability using actual stat and returns
Cocoa errors with the real underlying POSIX failure. Non-file URL schemes retain
their original implementation. It preserves CoreUI parsing, Foundation URL/data
objects and subsequent real open errors.

Each application launch declares a canonical private Linux home/container,
shared only with that app's subsequent generation in the same journey. Repeated
path separators are collapsed before matching the declared roots; dot components
and actual symlink traversal are preserved. This prevents a remaining leading
slash from making Linux ignore the firmware directory descriptor. Storage writes
use real openat2/mkdirat/renameat/unlinkat/fsync operations under the container,
with beneath/no-magic-link resolution; firmware and bundle writes are rejected.
Darwin stat/fstatat share actual Linux statx metadata and guest record packing.
This provides platform file operations, not Apple sandbox or data protection.
End-to-end persistence acceptance belongs to the Notes journey in EXPERIMENT.md.

### Lifecycle boundary

The previous implementation encoded legacy FrontBoard launch/scene events in
native-xpc.c and ios-scene-codec.c. That path did not complete UIKit launch on
the selected iOS 16 input and has been removed, including its reply state and
workspace registration. The service endpoints still record authenticated
requests and return unsupported operation errors. They are not a lifecycle API.

Introduce a lifecycle contract owned by the session and consumed by the guest
platform adapter: create/configure a scene using actual host geometry, activate
or deactivate it on real focus changes, and request normal termination on host
close. Each event belongs to a specific process incarnation and scene; queued
events, acknowledgements and cleanup must agree across process exit/relaunch.
Rendering and input remain separate consumers of the same scene identity and
geometry. The lifecycle event contract is a design target. The version-4
platform API currently exposes an actual window snapshot: agreed buffer
geometry, refresh, compositor activation, explicit close request, backend
liveness and audited app PID/incarnation. A bounded sequence-checked read
avoids torn host updates; kernel child status overrides stale liveness after
an abrupt backend exit. Its bounded single-producer/consumer input queue
preserves actual Wayland contact transitions, keyboard commits/deletion and
monotonic timestamps; overflow
requires cancellation rather than a synthetic successful click. The guest
main-loop adapter constructs real IOHID/UITouch events and sends them through
UIKit hit testing and UIApplication sendEvent:. Primary-pointer contacts and
keyboard text commits/deletion are implemented; scroll and multitouch are not.
The host-close flag now
enters original UIApplication termination. Counter and Swift/UIKit verify these paths end to end; no full collection
or Android claim follows. The guest ios-lifecycle.m attempts real scene construction on its main
loop using the specification's actual settings/client/transition classes and
host-derived process/geometry identity. It supplies a local updater and enters
FBSScene's genuine agent creation notification. That path binds the agent to
its scene before its original event handler dispatches into UIKit. The removed
direct UIApplication workspace callback skipped that required binding. The
software rendering boundary produces original Counter/Swift content and actual
input changes. Their complete selected GUI journeys pass. Actual compositor
focus changes now enter the original scene updater on its callout queue, using immutable settings and FBSSceneSettingsDiff. Visible live
windows stay foreground; our owned nonzero deactivation contribution represents
host focus loss without pretending to encode Apple's lock/background policy.
The original completion response remains opaque and is not acceptance.
The independent focus-activation journey verifies public UIApplication state,
original UIKit notifications, changed pixels after reactivation and both normal
exits. Geometry propagation exists but resize, minimize/background and Android
have not been verified. Session scene registration, complete workspace enumeration,
host-process/remote endpoints and arbitrary helper cleanup remain unfinished.
Unsupported operations fail visibly.

Use the existing injected platform library as the adaptation seam. Inspect the
real UIKit/FrontBoard startup path to choose the narrowest framework-facing
lifecycle entry points. Adapting or replacing a dependency-heavy framework
component at that seam is allowed. Do not drive the app delegate/control actions
from the test harness, force a readiness flag, or suppress a wait without owning
the state transition it represents. A direct delegate call alone would omit
scene/event-loop setup and would not implement launch.

Keep the existing Mach/bootstrap support for genuine consumers. Add private
wire support only when it is a small, reusable part of this contract; do not
build a parallel service hierarchy solely to preserve an internal wire format.
The removed legacy path had no passing GUI journey. Preserve verified shared
transport/runtime behavior while implementing the replacement.
Counter and Swift/UIKit now prove launch, external input and close/relaunch
against the same adapter. The independent collection exposes remaining platform
boundaries. An app-specific exception is evidence to revisit the boundary, not
a new policy.

| Owner | Responsibility | Must not own |
| --- | --- | --- |
| macho-native / native-launch.h | Explicit console/application/service role; image loading, guest stack, session join | Guessing GUI role from inherited environment |
| dyld-cache.h / arm64-adapt.h | Bounds, relocation, TLS instruction adaptation, final protections | UIKit object policy or app-specific drawing |
| ios-framework-api.h / ios-framework-bindings.h | One linkage contract; function/data imports, actual mapping permissions | Capability claims based on found symbols |
| darwin-native runtime | Darwin carry/errno, memory, threads, synchronization, raw Linux transport | Host libc calls after guest TLS takes over |
| components/darling | Shared Mach core and genuine cross-task ports/messages/VM operations | A second competing port-ownership implementation |
| native-xpc.c | Audited peer identity/incarnation, registered displays, bootstrap and required service transport | UIKit policy or legacy FrontBoard lifecycle emulation |
| ios-scene-token.m | Immutable local scene identity/copy/equality without a remote Apple endpoint | Encoding a fake remote capability |
| ios-input.m | Actual host contacts, UIKit hit testing and original event dispatch | Control actions or invented successful input |
| ios-filesystem.m | Linux-backed file URL reachability with real errors | Replacing resource parsers or returning unconditional success |
| ios-lifecycle.m | Guest main-loop scene construction and framework-facing lifecycle adaptation | App delegate/control callbacks or acceptance decisions |
| ios-platform.c / ios-platform-abi.h | Real dyld library constructor and versioned private platform attachment | Bootstrap lookup side effects or calling frameworks from syscall signals |
| darwin-ui.h | Per-process UIKit/CG state; real main-loop layer rendering | Replacing app views or synthesizing control callbacks |
| native-display.h | Host-TLS Wayland child; surface, shared buffers and release events | Guest UIKit/libc calls |
| src/diagnostics | Optional bounded evidence | Supplying runtime semantics or declaring acceptance |
| apps/shared/Inspect.m | Read-only real UIKit/accessibility properties and frames in source-owned apps | Calling control actions, setting state or substituting a framework |
| tools/app_suite | External user actions, state plus composed pixel effects | Guest method injection, fake backends, runtime workarounds |

Application and service processes join the shared session before any optional
display registration. Headless services must not create a window or depend on
a display child's lifetime. Bootstrap provider liveness is checked against its
actual PID/incarnation; display liveness is a separate concern. The application
role requires a configured Wayland display. Console diagnostics remain explicit.

The shared session links libarlinux_mach_core.so into its own process and owns
bootstrap, peer and display-registration state. Lifecycle ownership will use
this session, but its host-backed contract is not implemented yet. Each app has its own Linux address space, guest TLS,
threads, renderer and Wayland child. Rights/messages use Darling/XNU across real
processes. A virtual PID 1 is not invented. Full task-right/PID verification,
helper launch policy and crash resource cleanup remain unfinished.

Before guest entry the loader publishes the interpreter's actual
`__all_image_info` Mach-O section through Darling's dyld-info registration API.
TASK_DYLD_INFO is then a genuine MIG reply about storage maintained by dyld;
returning zero metadata would not implement that loader contract. The scalar
mach_msg2 adapter decodes Apple's packed arguments and uses those same Mach
queues/kobjects. Vectors carry bounded opaque auxiliary bytes in the real kmsg,
with actual receive data or no-auxiliary EOF. Separate-header forms remain
unsupported; Apple's CFI routing policy is not enforced by this trusted-input
runtime. Registering metadata or translating a request alone is not IPC acceptance.

The application loader inserts the source-built libarlinux-ios.dylib through
the genuine dyld environment. Its constructor queries a versioned private ABI
and calls the returned attachment entry point as a normal guest call. It installs
the real main-run-loop observer before application main. Bootstrap lookup no
longer attaches UIKit. Missing or failed library insertion is fatal. A plaintext
LC_ENCRYPTION_INFO64 (cryptid 0) preserves its already mapped bytes; nonzero
encryption remains unsupported. This lifecycle ran in the actual app journeys;
it does not establish working controls or input.

## Required semantics

Memory mappings preserve requested alignment, protection and real storage.
Anonymous Darwin shared mappings use actual Linux MAP_SHARED backing with the
same low-address/alignment/protection rules as private allocations. This supports
original UIKit's HangTracer allocation during reactivation without patching that
framework; it does not establish working fork/Mach task inheritance.
Guest allocations stay in the low-address range required by this Objective-C
ABI; host signal stacks may use host addresses. Real fork/COW, wait and thread
join must not be replaced by success values. The single-threaded, pre-workqueue
console fork boundary now creates a genuine Linux COW child, closes only its
inherited RPC references, checks in with the existing Mach core, and acquires its
own task/host/thread rights. Original libpthread may register its child callbacks
again. A real pipe holds the parent until child registration completes; failure
kills/reaps that child. Dyld metadata points to the child's inherited COW storage.
The Mach core owns task special-port inheritance; arbitrary parent receive rights
are not copied. Live framework sessions, native kqueues and multithreaded fork
quiescence remain unsupported and fail before clone. Unknown calls fail. A single CPU
is enforced with Linux affinity before threads start and is reported consistently.

Raw Linux syscalls use the allowed assembly site. Guest callbacks use no host
errno/malloc/stdio/TLS. Floating-point Objective-C/CG ABI calls use explicit ARM64
veneers. Shared framebuffer slots follow FREE -> DRAWING -> READY -> BUSY -> FREE;
only the actual compositor buffer-release event returns BUSY storage to FREE.
A frame callback is not proof of physical presentation or useful content.

Workqueue return operations are continuation transfers, not ordinary syscall
results. After real event collection, the saved signal context re-enters the
registered libpthread callback with its actual event buffer/count and reuse
flag. Ordinary completed worker batches enter libpthread's exit upcall and real
cleanup. Linux rt_sigreturn restores the signal mask/alternate-stack state before
guest execution; a guest callback is never invoked inside the syscall handler.

Framework/userland code may be adapted in private mappings. Privilege or device
interfaces may be simplified only when their required application effects are
preserved. Required synchronization, memory, input, IPC or lifecycle effects may
not be replaced by unconditional success. The current firmware pin defines the
supported private ABI; no claim of arbitrary iOS version support is made.

## Remaining replacement work

- Extend private-container storage for further app-required operations; Notes
  now verifies original Foundation atomic write and new-process readback.
- Verify scene geometry, minimize and background behavior from real host
  changes. Visible-window focus activation/deactivation, primary pointer input,
  basic text commits and normal close pass their recorded journeys;
  broader key/IME/multitouch behavior is pending.
- Extend rendering coverage and evaluate real GPU-backed presentation. Original
  UIKit text, controls, system colors and CPU drawing already pass on this input;
  remote contexts, IOSurface/Metal and hardware acceleration remain unverified.
- Implement application-requested service launch and lifecycle, then independently
  verify rights/identity cleanup after normal exit and crashes. The suite includes
  a real UIActivityViewController client and bundled share extension for this purpose.
- Decompose the remaining header-assembled syscall runtime behind explicit owned
  interfaces using the real journeys as the behavioral guard, then validate the
  same path on ARLinux/Android. Linux runner results never imply device acceptance.

## Verification and provenance

[App collection](APPS.md) define the executable effect-based suite. Failure and
blocked steps fail the case. [Diagnostics](DIAGNOSTICS.md) define evidence collection;
[EXPERIMENT.md](EXPERIMENT.md) retains the current handoff, not the design history.
Each run records source, environment, input identities and retained-file SHA-256.
Apple firmware/cache/executables remain transient on runners. Upstream identities
and licenses live in component ORIGINS.json and README acknowledgements.

Primary execution references: [tawcroot](https://github.com/taowen/tawc), imported
Darling/XNU code, [Apple TSD](https://github.com/apple-oss-distributions/xnu/blob/1b191cb58250d0705d8a51287127505aa4bc0789/libsyscall/os/tsd.h),
[Wayland](https://wayland.freedesktop.org/docs/html/apa.html), and
[iOS app extensions](https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/ExtensionScenarios.html).

Main-thread stack limits share the launcher's actual allocation identity. The
launcher allocates/registers its guest main stack once; both the Apple main_stack
vector and getrlimit(RLIMIT_STACK) describe that mapping. Linux's unrelated host
stack limit must not be exposed as this guest stack. Other supported Darwin
resource IDs map explicitly to Linux prlimit64 with infinity conversion; invalid
IDs and guest pointers retain actual errors. setrlimit is not implemented.

Current-task vm_protect applies actual Linux read/write protection for both
set_maximum forms, matching integrated Darling's duct-tape memory adapter. It
does not maintain a separate Mach maximum-permission ceiling: trusted guest
code can subsequently request writable protection. No sandbox or permanent
anti-tampering guarantee is claimed. Executable protection remains unsupported
on this direct trap path; real command failures are returned.

Anonymous VM_FLAGS_PURGABLE allocations start with real nonvolatile, zero-filled
backing. This flag does not itself discard memory; later volatile/discard state
control remains unsupported. Do not report such control as completed or infer
shared-memory/IPC conformance from initial allocation success.

Interrupt-enter can receive a completed outer RPC reply before its own reply.
The native client uses Darling's existing push_reply protocol to return that
exact body and its SCM_RIGHTS descriptors to the server's interrupt stack. A
pipe acknowledgement proves the server copied the body before the generated
buffer can be reused. Original received FDs close after transfer; interrupt_exit
lets the server deliver the saved outer reply normally. No fabricated reply or
parallel client-side reply queue is introduced.

Named-memory export and mapping use real IKOT_NAMED_ENTRY ports and the existing
Darling memfd descriptor/map ownership. Linux S2C mapping consumes one actual
SCM_RIGHTS FD, maps the same file backing and closes the temporary descriptor.
The named port holds one descriptor reference; installed mappings hold their own
references until task-map retirement. No-senders destruction releases the port's
reference. Export and shared remap share one backing-conversion implementation.
Mapped device/inode/offset identity prevents stale mapping records from silently
binding a reused address to an old object. Unsupported overlaps, parent entries,
copy-on-write and fixed named mappings return errors. The source-private to
shared conversion retains upstream's snapshot/rebind race with concurrent writers;
it is not arbitrary concurrent VM conformance. Per-unmap registry retirement also
remains incomplete; ambiguous/reused ranges fail rather than returning old backing.
No empty capability, copied destination snapshot or unmapped-success reply counts
as shared-memory behavior. Process named-memory alias/fork interaction passes on
Linux; cross-independent-task port transfer and Web rendering remain unverified.
Aligned shared-range extraction can retain zero data offset; masked permissions
intersect the actual source protection before conversion. Unaligned data-address
extraction and other memory-entry modes remain unsupported. The flag extension passes the Process app flow and moves original WebContent
past handle export; the GPU receiver still fails to map the transferred stream.
