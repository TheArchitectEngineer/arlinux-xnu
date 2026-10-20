# Darwin/iOS compatibility architecture

The target is a real iOS application journey on ARLinux, not faithful execution
of an unmodified kernel. ARM64, 16 KiB guest pages, one advertised CPU and
non-arm64e code define the initial scope. Compilation and execution occur only
on GitHub runners through the root build.sh entry.

## Native execution first

macho-native loads a bounded static Mach-O image. Its executable instruction
sections are adapted in private memory: Darwin TPIDRRO_EL0 reads use Linux's
writable TPIDR_EL0; original Darwin TPIDR_EL0 CPU metadata reads return CPU zero.
The file is unchanged. This has executed our console, the pinned Apple dyld and system-library
initializers in the pinned non-arm64e cache. It does not establish arbitrary
instruction/data mixtures, later executable mappings, JIT or arm64e.

Darwin platform set/get-TSD calls maintain actual guest TLS. Borrowing tawcroot's
approach, the SIGSYS runtime never calls host libc after guest TLS takes over.
Raw Linux syscalls have one exact allowed instruction site; other syscalls trap.
Linux signal return is allowed separately. This is an ABI transport, not a
security sandbox. Unknown Darwin calls return ENOSYS; unsupported executable,
file or fixed mapping modes return errors rather than fake support. The
read-only firmware filesystem supports the loader's actual open/stat/read
operations, with Darwin metadata layouts translated from Linux statx.

The Linux kernel supplies real anonymous 16 KiB-aligned mappings, protection,
read/write, fork/COW, wait and process exit. The kernel validates user buffers.
The runtime converts Linux error results to Darwin's carry/positive-errno ABI.
The local IPC backend does not link XNU. The opt-in Darling backend runs real
XNU IPC code in a separate Linux process. The historical ptrace hello shim
remains a separate host diagnostic, never app acceptance.

Primary sources: [tawcroot runtime](https://github.com/taowen/tawc/blob/bee5ae2d216f6283a8a6e6b60f18e2a7b6d8451d/notes/tawcroot/seccomp-filter.md),
[Apple TSD](https://github.com/apple-oss-distributions/xnu/blob/1b191cb58250d0705d8a51287127505aa4bc0789/libsyscall/os/tsd.h)
and [platform calls](https://github.com/apple-oss-distributions/xnu/blob/1b191cb58250d0705d8a51287127505aa4bc0789/osfmk/arm64/machine_routines.c).

## ABI definitions and adapter policy

`src/darwin-abi.h` owns guest syscall/trap IDs, errno and MIG values, flags,
commpage offsets and wire structures. Its source references identify the Apple
interfaces; named fields replace pthread-registration byte offsets. Host errno
uses Linux headers and is explicitly translated, even where values coincide.
`src/darwin-machine.h` owns our address layout, stack limits and local port-name
allocation policy. Add ABI values to the former and adapter choices to the
latter; keep interface-specific meanings distinct when numbers happen to match.

## Guest address geometry

All guest heap/stack allocations use disjoint MAP_FIXED_NOREPLACE ranges below
the legacy 36-bit iOS commpage, with 16 KiB guest pages and requested power-of-two
alignment preserved. Linux automatic mappings can exceed the address bits packed
into real Objective-C metadata/isa fields. Such host addresses are not suitable
guest pointers even on the same ARM64 CPU. Host-only signal/runtime memory can
remain at normal Linux addresses. See Apple's [packed isa definition](https://github.com/apple-opensource/objc4/blob/master/runtime/isa.h).

## Genuine shared cache

Before guest entry, the loader maps the selected firmware's legacy ARM64 cache
into private host mappings at its unslid addresses. It decodes version-2 pointer
chains, adapts marked executable sections once per unique image, flushes code
and restores final protections. Unknown formats fail. Only then does the runtime
expose the real mapped base through shared_region_check. Apple authentication
is explicitly disabled as a loading policy; verified firmware SHA-256 and
mapping/relocation state are recorded independently. This replaces the kernel's
cache-loading responsibility rather than faking its completion.

Real libSystem/pthread/libc initializers now execute. The observed Host
priority query, Clock service lookup and semaphore creation have passed their
clients' checks. The true dynamically linked C main now executes real libSystem
write/malloc/free calls and real pthread create/join, input, exit and relaunch.
Without a display backend the empty task Bootstrap slot is NULL. A configured
native Wayland backend enables the standalone registry described below;
this is not a full launchd/XPC server. See docs/EXPERIMENT.md for the exact source/run/hash and
remaining acceptance gaps. Cache symbolication preserves actual image and nearest-symbol
metadata from the same input without uploading Apple executable contents.

## Initial Mach services

When WAYLAND_DISPLAY or WAYLAND_SOCKET is supplied, a host child connects
directly to the compositor using wl_compositor, xdg-shell and wl_shm. It runs
before Darwin TLS setup and keeps its own host TLS and protocol event loop.
After configure/ack, a real XRGB memfd buffer is attached and a frame callback
confirms backend readiness. wl_surface.enter identifies the output whose current
wl_output mode supplies the actual refresh rate. The guest process maps the same framebuffer.
The child is terminated when its parent dies. Initial geometry is retained;
resize, UIKit rendering, input forwarding and application close are unfinished.
An empty mapped surface never counts as displaying the application.

The task then gets an actual standalone bootstrap endpoint. The private cache's
public bootstrap_look_up3/register2/check_in2 entries are redirected by symbol to a libc/TLS-independent
registry adapter; no firmware-specific address is used. The live backend
registers separate display and render endpoints named
com.apple.backboard.display.services and com.apple.CARenderServer; lookup grants
a tracked send reference. A mutex-protected bounded registry accepts application
name/send-right bindings, with separate global and per-PID namespaces. Each
binding owns a server reference independent of the client's send references;
lookup grants a client reference to that same queue, and NULL registration
removes a binding. Duplicate names and unknown services fail. No on-demand
services are configured: check-in returns UNKNOWN_SERVICE or SERVICE_ACTIVE,
never invented receive rights. Nonlocal PID lookups and unsupported flags fail.
Receive-right destruction and cross-process registry ownership remain absent;
destroyable timer ports cannot be registered. This replaces name discovery at the
API boundary rather than pretending to implement launchd's XPC dictionary
protocol. Display requests/replies still use real Mach queues and MIG layouts.
The measured 24-byte screen-info request uses routine 6001004; its 52-byte reply
contains configured width/height, scale 1 and rotation 0 for the actual default
Wayland buffer transform. Backend liveness is checked before lookup and query.
Render routine 40231 returns a property-list array describing that same virtual
display, through a fresh 16 KiB mapping and a LP64 Mach OOL descriptor. The
actual client checks descriptor type/size, copies it into CFData, parses it and
builds its own CADisplay object. Routine 40232 supplies that display's configured
bounds/frame, one current/preferred mode and actual output refresh periods.
The measured CADisplayMode encoding uses 16-bit width/height and Q16.16 refresh;
MIG variable strings and the packed reply prefix/tail are declared in
darwin-display.h. A NULL hardware memory entry describes the absent Apple
hardware mapping; the actual client skips it. This provides geometry, not
UIKit rendering or Apple display hardware.
This narrow server-generated transfer does not implement arbitrary client OOL
sends, cross-process ownership or discarded-message resource cleanup.
Unimplemented display/render routines return MIG_BAD_ID. See the current handoff for
which requests have actually completed under genuine UIKit.

Protocol reference: [Wayland specification](https://wayland.freedesktop.org/docs/html/apa.html).

Process-local reply rights have distinct names and Linux SOCK_SEQPACKET queues.
The observed combined send/receive exchanges enqueue a MIG response and then
receive it through the real queue. Unknown service routines get a MIG error;
unknown destination names get transport errors. Host priority ranges describe
the Darwin ABI policy, not implemented realtime scheduling. Host and Clock send
references are tracked; Clock service replies transfer a registered service
right using the normal complex descriptor format. Clock time uses Linux clocks.

mach_port_construct also allocates a real socket queue/receive right and records
its guard/context and requested initial send right. Queue limits use synchronized
packet counts, not an approximation based on socket byte capacity. The observed
XPC options include 0x31, 0x3b and 0x3f (including actual requested queue limits).
Temporary ownership names the creating task; importance receiver eligibility
uses the standalone ordinary Linux scheduling policy;
there is no App Nap or cross-process importance inheritance. Inline local
COPY_SEND/MOVE_SEND messages with no body rights use real queues and bounded waits;
guard destruction and broader port ownership remain unsupported. A successful
allocation is not evidence of an XPC server or a complete IPC journey.

Inline messages have a bounded 16 KiB capacity, not full XNU reliable-message
capacity. Oversize sends fail with SEND_TOO_LARGE; unreadable user buffers fail
with INVALID_DATA. Fixed-size timer notifications use fixed-size stack storage.
Larger message ownership/storage still needs implementation.

Queue packets capture the actual Linux sender audit identity at send time.
Receives construct packed Darwin trailers from that identity, the port context
and its actual receive sequence. Too-small LARGE receives retain the message
for a real retry. Complex descriptors, reply/voucher transfers and external
servers are not silently replaced by inline message success.

Thread special-reply ports own real receive/send rights and are bound to actual
Linux threads. A blocking special-reply receive yields that thread's workloop
ownership and waits on real readiness; no completion is invented. Receive
deadlines survive races with another consumer. Port allocation returns NULL on
failure, never a kern_return value that userland could misinterpret as a name.
MACH_RCV_SYNC_WAIT verifies the current thread's special reply port, records a
live destination linkage during the actual receive and clears it on completion.
A vanished destination does not fail the receive, matching XNU. This ordinary
Linux scheduling policy performs no cross-task priority donation.
Combined send/receive calls follow XNU's separate path without special-reply
linkage. SEND_OVERRIDE, SEND_SYNC_OVERRIDE, SEND_PROPAGATE_QOS and
SEND_NOIMPORTANCE are accepted scheduling hints under this policy; their
priority argument is not interpreted as a notification port. Actual transport,
right and buffer errors still fail the operation.

The observed semaphore-create request allocates an actual Linux eventfd counter.
Signal/wait traps are implemented, but have not passed a complete
multithreaded user journey. The host wait helper accepts deadlines; guest timed
wait traps remain unsupported. Contended FIFO ordering and concurrent namespace
mutation remain unverified. General descriptors, client out-of-line memory,
notifications, arbitrary rights transfer and cross-process namespace semantics
are still outside the implemented transport. These limitations remain failures,
not empty successful replies.

Current-task receive-right allocation creates the same real socket queues.
Port-set allocation creates a distinct Linux epoll object; insert/extract member
operations register/unregister the actual receive socket, with duplicate/missing
membership errors. Receiving on a set waits on that epoll object and consumes a
ready member's message, retaining the member's name, sequence and context.
Each receive queue can belong to multiple sets. Making a send right from the
same local receive right increments its tracked reference. Port-set destruction,
membership enumeration and other right-transfer operations remain unsupported;
see the current handoff for which operations the real app has exercised.
Current-task mach_port_type reports actual local receive/set rights and tracked
send references through MIG routine 3201. Unimplemented thread-right queries
fail instead of inventing a type.
mach_port_mod_refs retains/releases an existing send or dead-name right using
synchronized counts, rejecting underflow/overflow. Destruction currently covers
empty, unguarded thread special-reply receive rights: shutdown wakes actual
socket/dup-fd pollers, the blocked receive returns MACH_RCV_PORT_DIED, and
remaining send references become dead-name references. A racing Mach-port
knote observes the vanished receive right. Endpoint/socket tombstones remain
allocated until process exit to avoid recycling memory or descriptors held by
a waiter. This consumes bounded endpoint capacity; concurrent right-reference
mutation, general guarded ports, nonempty message cleanup and reclamation still
need implementation/verification. Other destruct requests fail explicitly.

Mach timer ports own a receive socket plus a monotonic timerfd, aggregated by
epoll so a port set or kqueue can wait on either a pending notification or a
new expiry. The same 1:1 nanosecond timebase backs mach_absolute_time and
absolute timer deadlines. Reading an actual expiry queues the 48-byte Darwin
timer message with the compatibility kernel's audit/security trailer; peek and
LARGE retries retain it. Cancel/rearm first captures an expired notification,
so an already queued message survives those operations. Creation failure returns
NULL, never a kern_return masquerading as a name. Normal timers may fire at
their exact deadline within a requested leeway; critical scheduling flags fail.
Concurrent timer destruction/namespace mutation remains unverified. The current
handoff distinguishes implemented paths from actual framework observations.

## Event workers and process services

Native libpthread registration supplies ordinary and basic kevent workqueue
capabilities backed by actual Linux clone threads, private signal/runtime
stacks and a scheduling gate before guest callback entry. The original
libdispatch, pthread callback and user blocks still execute. Native epoll,
eventfd and timerfd supply readiness for descriptor, user and timer filters.
Timer filters select CLOCK_BOOTTIME for continuous time and CLOCK_MONOTONIC
otherwise. Mach units use the same 1:1 timebase; calendar absolute deadlines
are converted once into the selected clock. Absolute timers are one-shot.
The standalone policy delivers at the requested deadline without adding a
coalescing delay, within the supplied leeway. Changing an existing timer's
clock kind remains unsupported.
Memory-status subscriptions sample actual Linux /proc/meminfo once per second;
the compatibility policy uses MemAvailable <=10% for warning and <=3% for
critical pressure. Initial measured state and subsequent transitions are
delivered only when subscribed; sampling ticks alone are not guest events.
UDATA-specific DISPATCH/rearm subscriptions are supported for this filter.
There is no per-process Jetsam cap or Apple MSL status policy in the standalone
runtime, and no process-limit notification is invented. Physical availability
does not yet model cgroup memory ceilings; Android integration must address that
where applicable. Successful registration alone does not verify pressure delivery.
Mach-port filters watch real queue sockets and can receive into explicit guest
buffers or report a genuine too-large message for libdispatch's allocation/retry.
For kevent data_out/data_available, a MACHPORT note with no explicit buffer
probes the queued message under its queue lock, includes its requested trailer,
and carves storage from the supplied region. STACK_DATA grows downward;
ordinary storage grows upward. A too-large message remains queued and reports
the required size/identity. Only an actual successful copy reduces the residual
capacity written back to the caller. Workqueue callbacks receive storage in
the reserved page above their guest stack, disjoint from the event array.
The current handoff distinguishes implementation from observed delivery.
Dynamic queues have distinct IDs/epoll objects and a dedicated serial Linux
worker. Real libpthread receives WORKLOOP flags and the ID immediately before
the event list; its registered workloop callback executes the guest code.
Servicer-side returns stay bound to that actual queue, while foreign synchronous
polls fail. Queue objects use real mmap/epoll allocations with stable published
addresses, replacing the former eight-object limit. They currently live until
process exit; automatic retirement remains incomplete.

Asynchronous EVFILT_WORKLOOP thread requests check the actual guest state against
the supplied mask/value before issuing a real eventfd wake. Ignored stale
requests do not create/modify/wake sources; other stale failures preserve actual
state and operation flags in the error receipt. QoS uses ordinary Linux niceness.
DISCOVER_OWNER resolves a real live thread after restoring the reserved low two
port-generation bits. The worker defers consumption while a client owns the
drain session, using a futex that ownership end/thread exit actually wakes.
Explicit SYNC_WAIT/SYNC_WAKE, full turnstiles, automatic queue retirement and
the workloop control syscall remain unsupported. The capability bit selects real libpthread
workloop registration; it is not a claim of full XNU workloop semantics.
See the exact app run in docs/EXPERIMENT.md before claiming callback delivery.

An implementation reference is [Darlingserver's reuse of XNU](https://docs.darlinghq.org/internals/darlingserver/index.html)
and its [Mach-port kqueue channels](https://docs.darlinghq.org/internals/darlingserver/kqchannels.html).
`--ipc darling` selects the pinned [community ARM64 implementation](https://github.com/VibeDarling/darlingserver).
Our taowen/darling parent gitlink fixes its server, MIG and header dependencies;
the lock records the upstream base. The component adapter consumes upstream
CMake targets. XNU ipc_space, ipc_right, ipc_kmsg and ipc_notify execute in the
server, with upstream dtape scheduling, timers and process_vm memory hooks.
Our server fork adds an explicit `--rpc-only` lifecycle and guest configuration,
without setting up a second full Darling userland or launchd.

The generated RPC client uses per-thread Linux sockets, actual thread checkin
and checkout, raw syscall hooks and no host TLS. Client memory access is
authorized to the exact server PID. Self ports and port/message traps use the
server's namespace; local queue code must not interpret those numeric names
as old local objects. The native display registry retains actual send references;
a host service thread keeps its own libc/TLS and receives/replies through real
Mach ports. Its bootstrap slot is set by XNU with a copied send right. Current-task VM and clocks continue
to use native Linux operations. The strict empty bank-voucher policy below
creates no port. There is no silent fallback to local IPC.

The fork explicitly configures the ARM64 XNU VM granule to 16 KiB and its VM
map shift accordingly. Backend mapping hooks still count Linux host pages;
these units must remain distinct. Client and RPC-server threads are pinned to
one CPU, with matching host/processor information. Run 39367375551 completes
two real dyld/libSystem console journeys through this backend, including a
pthread create/join, input, normal exit and relaunch. Run 39370264712 also
establishes real display queries/OOL copies and a 112-byte kqchannel receive
into the caller's stack data area. Synchronous S2C anonymous mappings use the
native low-address allocator. Guest granules and host-page hook counts remain
distinct. File/fixed/executable mappings and asynchronous S2C signal handling
remain incomplete. XNU's missing-note ADD|DELETE workloop soft-delete semantics
are now preserved before ownership/debounce processing; a plain missing DELETE
still returns ENOENT. Real task audit/security tokens come from Linux credentials,
PID and start ticks in our server fork, rather than a zero token. Native csops
reports actual unsigned status and no entitlement blob for an unsigned Mach-O;
embedded, unvalidated signatures remain unsupported, never labelled CS_VALID.
The UIKit display/input journey and Android integration remain unverified.

First-fit mutex slow waits consume real futex-backed wake credits; the genuine
pthread library still owns its lock state and owner compare-and-swap. Other
psynch protocols require their own behavior. The standalone process chooses
an empty bank-accounting base voucher; it does not mint an Apple identity or
pretend that unrelated voucher attributes exist. Kernel process identity is
translated from Linux PID/start ticks, with the guest executable's actual
Mach-O UUID. TASK_AUDIT_TOKEN uses the same process incarnation and Linux audit,
real/effective UID/GID and process identity; it does not mint an Apple identity. Application bundle reads are resolved to its real bundle directory
while firmware reads remain rooted separately. O_NONBLOCK directory opens,
real Linux filesystem statistics/mount metadata and inode64 directory records
let CoreFoundation discover and read actual bundle metadata. Directory cookies
are retained and unread entries are not dropped on output-buffer exhaustion.
Both paths retain read-only
open policy. These are runtime services, not evidence of Android integration.

Activity IDs reserve unique ranges from a MAP_SHARED atomic counter for this
standalone machine and its fork children; they do not claim Apple host-wide
tracing identity.

## Preserve behavior, permit implementation changes

Reuse and adapt XNU algorithms or services where they shorten the application
path, especially Mach IPC and Darwin thread/exception semantics. Linux-backed
replacement is permitted. Memory lifetime/protection, synchronization, message
ownership, return values and error behavior must meet application requirements.
Hardware or privilege policies can be simplified when that is sufficient.
Returning success for an operation whose missing effect breaks the app is not
an implementation.

Full-system ios-sweet/XNU runs are behavioral references, not proof of ARLinux
compatibility. The active application path uses native ARM64 execution; its
build does not depend on a CPU instruction-emulation engine.

## PlayCover reference boundary

PlayCover's pinned Macho.swift changes iOS Mach-O platform commands to Mac
Catalyst and adjusts selected library paths. Its native host supplies UIKit
from /System/iOSSupport. PlayTools adds method swizzling, display adaptation and
input-to-touch conversion; these are useful application adaptations, not a
Linux Darwin/runtime implementation. Only the explicitly locked reference converter is consumed by the workflow;
research checkouts do not silently become build dependencies.

The reference workflow reads the immutable PlayCover source-lock commit and
compiles its unchanged converter together with our small entry. It compares
original and transformed/signed hashes, retains GPL corresponding source and
license, and observes the actual application under the macOS debugger. Linux
and macOS success remain separate observations: the macOS frameworks are newer
than our pinned iOS 12 firmware, so a different path is not automatically a
Linux implementation bug. No framework binaries from macOS are redistributed.

## Application-driven sequence

1. Run the native interactive Mach-O console end to end: real VM/COW and TLS,
   user input, normal exit and an independent second launch.
2. Add Mach-O process startup/dyld/system-library behavior required by a chosen
   real userland. Extend rewriting when code is mapped, not only at initial load.
3. Provide the services required by genuine iOS frameworks; connect display and
   input to ARLinux. Preserve licenses and do not distribute proprietary assets.
4. Record a named/versioned simple iOS App's launch, display, interaction, exit
   and restart on the actual ARLinux integration.

The console program is an intermediate executable, not the final iOS App.
Do not turn each missing symbol into an isolated probe or count a green build
as a completed application stage. Preserve exact source, hashes, environment,
logs and images for both successful and failed journeys.
