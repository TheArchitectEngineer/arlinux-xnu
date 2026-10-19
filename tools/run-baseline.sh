#!/usr/bin/env bash
# Internal runner stage of build.sh --mode baseline; no local compilation.
set -euo pipefail
input="$PWD/out-baseline"
output="$PWD/build/reference"
mkdir -p "$output"
cp "$input/receipt.json" "$output/compiler-receipt.json"
cp sources.lock.json "$output/"
uname -a > "$output/environment.txt"
qemu-system-aarch64 --version >> "$output/environment.txt"
llvm-nm --version >> "$output/environment.txt"
(cd "$input" && sha256sum -c SHA256SUMS) | tee "$output/input-verification.txt"
cp "$input/SHA256SUMS" "$output/input-SHA256SUMS"
cat "$output/input-SHA256SUMS"
kernel="$input/kernel.development.sweet"
python3 "$input/reference-tools/mkadt.py" "$output/guest.adt" --qemu
python3 "$input/reference-tools/mkpayload.py" "$kernel" "$output/guest.adt" "$output/payload.bin" \
    --ramdisk "$input/init.macho" \
    --cmdline 'debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=md0 sweetlog=0 sweetusb=0 sweettouch=0 sweetkeys=0'
# Upstream captures actual guest registers, framebuffer and exceptions. Its
# "no_panic_observed" alone is insufficient: require the completed process
# journey from the decoded guest console, and reject unknown/blank snapshots.
python3 "$input/reference-tools/qemu-sweet.py" "$output/payload.bin" "$output" \
    --secs 180 --kernel "$kernel" --fail-on-panic
cat "$output/fb.txt"
python3 - "$output" <<'PY'
import json
from pathlib import Path
import sys

directory = Path(sys.argv[1])
screen = (directory / 'fb.txt').read_text()
capture = json.loads((directory / 'result.json').read_text())
passed = (capture['status'] == 'no_panic_observed'
          and 'XNU JOURNEY PASS: memory COW exit wait' in screen
          and 'XNU JOURNEY FAIL' not in screen)
result = {
    'execution': 'qemu-system-aarch64 full-system reference, not userspace XNU',
    'journey': 'original XNU exec PID1, mmap/touch, fork/COW, child munmap/exit37, parent wait4/munmap',
    'passed': passed,
    'capture': capture,
    'limitation': 'final framebuffer snapshot; no dyld, iOS frameworks or Android validation',
}
(directory / 'journey.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result), flush=True)
if not passed:
    raise SystemExit('Real XNU process journey not observed; inspect framebuffer/registers/exception log')
PY
