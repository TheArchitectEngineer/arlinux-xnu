#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 1 ]]; then
    echo "Usage: $0 <darlingserver build directory>" >&2
    exit 2
fi
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
objects="$1/duct-tape/CMakeFiles/darlingserver_duct_tape.dir"
executable=$(mktemp)
trap 'rm -f "$executable"' EXIT
"${CC:-cc}" -Wall -Wextra -O2 -pthread "$here/activity-ids.c" \
    "$objects/src/traps.c.o" \
    "$objects/xnu/osfmk/ipc/ipc_voucher.c.o" \
    "$objects/xnu/libkern/gen/OSAtomicOperations.c.o" \
    -Wl,--gc-sections -o "$executable"
"$executable"
