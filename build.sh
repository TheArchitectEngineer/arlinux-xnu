#!/usr/bin/env bash
# Sole public entry. Compilation runs on GitHub-hosted runners.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$root/tools/github-build.py" "$@"
