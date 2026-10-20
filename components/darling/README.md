# Integrated Darwin IPC runtime

This directory is an in-tree hard fork of the Darling runtime used by the
native iOS experiment. Edit server, XNU support, MIG and RPC changes in this
repository; there is no external Darling gitlink, download or patch stage.
The native TLS-free client lives in ../../src/darling-*.h; its RPC definitions
and generator live in source/src/external/darlingserver. They are built from
the same enclosing repository commit. Source subdirectories retain their
historical layout to preserve upstream relative includes and symlinks.

ORIGINS.json records the exact import revisions for attribution and historical
comparison, not versions to fetch during a build. The parent support files,
server, XNU, MIG and pthread headers are ordinary tracked source. Unused Darling
desktop frameworks and launchers are not imported. The separately selected
XNU reference experiments remain independent of this runtime.

Build and run only through the repository build.sh entry on GitHub runners;
see the root README and docs/DIAGNOSTICS.md. The server and generated RPC files
are intermediate products of that single source revision, not another release
or independently selected binary. Functional acceptance is the actual app
journey, not successful compilation.

## Attribution and licenses

Thanks to Darling, VibeDarling and Apple Open Source. Their copyright notices,
LICENSE files and source-header licenses are retained in source/. Darling's
GPLv3 license is in source/LICENSE; the server carries its own LICENSE and the
RPC generator retains its GPL notice. XNU, MIG, pthread and included third-party
sources retain their respective licenses; importing does not relicense them.
Our previous embedding, 16 KiB-page and single-CPU modifications are included
at the revisions recorded in ORIGINS.json. Future fixes are maintained here.
