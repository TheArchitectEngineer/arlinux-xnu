# Bulk and volume attribute regressions

From the repository root, on Linux with Ruby and Clang:

```
ruby darling/tests/check-bulk-attribute-position.rb .
ruby darling/tests/check-bulk-packed-records.rb .
ruby darling/tests/check-bulk-error-packing.rb .
ruby darling/tests/check-returned-resource-fork.rb .
```

These compile actual source functions with ASan/UBSan and controlled adapters.
They cover directory retries and rewind errors, invalid record lengths, packed
fields and masks, per-entry errors, resource forks, FinderInfo, timestamps,
volume roots and mount status. Real Linux files, xattrs and directory syscalls
are used where applicable; guest namespace expansion is a test adapter.
The returned-attribute suite runs path/fd and statx-present/absent variants.
Its optional differing-credential cases require `sudo -n` when enabled by the
test environment; ordinary cases run unprivileged.

`bulk-attributes-guest.c` is a Darwin executable for an **isolated disposable
Darling guest** with the candidate kernel library. Do not run it against a
valuable guest root: it temporarily replaces that root's FinderInfo xattr.
Compile with Darling's SDK and its Darwin-targeting Clang. It requires a
writable guest root, tests both path and fd APIs, and checks volume support,
root-selected FinderInfo, root/child mount status, and small-buffer bulk retries
including a dangling symlink. Require its PASS message and exit zero.

The guest test passed on ARM64 with the same five implementation files from
the integration candidate, 298 rebuilt emulation objects and 581 rebuilt
libsyscall objects, linked with staged RPC/static dependencies. The existing
guest kernel missed the dangling symlink. After transplanting onto upstream
with the prerequisite PRs, all host suites and six ordinary/dyld compilation
units pass again. A fresh combined build of this exact upstream branch, x86_64
guest execution, nested bind-mount scenarios and mount replacement races have
not been tested. Unavailable metadata is omitted from returned validity masks;
PACK_INVAL preserves the corresponding zero slots, not a claim of support.

This branch includes prerequisite PRs #28, #45, #46 and #47 with their ancestry.
Merge those first if reviewing just the bulk/volume delta.
