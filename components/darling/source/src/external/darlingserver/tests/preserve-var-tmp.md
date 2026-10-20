# Prefix temporary-data preservation

On a Linux host with Ruby, Clang++ and ASan/UBSan:

```sh
ruby tests/preserve-var-tmp.rb
ruby tests/preserve-var-tmp.rb 105d828633f770495727e344fbbcb99ae49f15cd
```

The candidate passes and the upstream baseline fails because the payload in
var/tmp has been removed.

The harness compiles the actual wipeDir and darlingPreInit functions, with
only the three unrelated symlink/setup helpers replaced by counting adapters.
It executes them against newly created disposable prefixes, never an installed
Darling prefix. Both direct var directories and var -> private/var layouts are
tested at O0/O2 with ASan/UBSan. Each layout undergoes three pre-init calls.
Persistent nested binary payloads must survive byte-for-byte; runtime files,
nested directories and symlinks must disappear without touching symlink targets.

The full changed darlingserver.cpp compiles with the staged ARM64 server
recipe and candidate headers. This is not a complete server link or actual
container restart test. Mount, privilege-transition, unrelated setup-helper
and general cleanup/path-hardening behavior are outside the regression scope.
