# ARM64 cached Darwin TSD reads

On Linux ARM64, with Ruby and Clang installed:

```
ruby tests/arm64-cache-tsd-trap.rb
```

This extracts the production decoder and checks every destination register,
XZR, unchanged SP/NZCV/unrelated registers, PC advancement, and rejection of
unrelated instructions. It then executes 4,000 real UDF traps on four Linux
pthreads, with separate mock Darwin TSD arrays, checking native pthread identity
and errno. The native signal adapter is test scaffolding; it does not install
the Darling signal dispatcher.

`arm64-cache-tsd-guest.c` is a separate Mach-O guest regression. Build it against
Darling libSystem and run it with the new kernel and a loader linked against the
new kernel archive. It compares actual trap results with
`sys_thread_get_tsd_base`, exercises four Darling pthreads, and checks pthread
keys and errno. It was run using the staged ARM64 probe recipe; no complete
upstream configure/build is claimed.

Protocol: dyld emits `UDF #0xda00 + destination_register` for a cached Darwin
thread-pointer read. Only synchronous SIGILL with that exact instruction range
is consumed. The handler changes Xd and PC only (XZR writes are discarded),
using the existing per-thread Darwin TSD lookup. It leaves TPIDR_EL0, native
ELF TLS, and other SIGILL delivery unchanged. Install the kernel-side support
before using a dyld cache translator that emits these instructions, including
the static kernel copy linked into dyld. Each translated read incurs signal
delivery overhead; this change prioritizes correct thread isolation.

The optional native-slot optimization reads `darling_tsd_slot_offset=<hex>`
from the apple vector and publishes registered Darwin TSD at native TP plus
that offset. It accepts aligned offsets 16..32760 (the veneer LDR range).
Older loaders omit the metadata and retain the trap/table path. The loader
must own this slot independently of legacy bridge callback state.
`arm64-native-tsd-slot-guest.c` checks slot/table agreement on the main thread
and four worker threads, along with traps, pthread keys and errno. Build as a
Darling guest C program and run with the cooperating loader/kernel. The slot
getter is resolved dynamically so the test can link against an older SDK.
