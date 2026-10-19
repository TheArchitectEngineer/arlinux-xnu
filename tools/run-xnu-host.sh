#!/usr/bin/env bash
# Runner stage called only by the repository's build.sh workflow.
set -euo pipefail
stage=${1:?expected early, vm or full}
case "$stage" in
  early) output=build/startup; entry_flags=() ;;
  vm) output=build/vm; entry_flags=(-DXNU_HOST_BOOT_VM) ;;
  full) output=build/full; entry_flags=(-DXNU_HOST_BOOT_VM) ;;
  *) echo "Unknown XNU startup stage: $stage" >&2; exit 2 ;;
esac
mkdir -p "$output"
cp build/kernel/receipt.json "$output/compiler-receipt.json"
uname -a > "$output/environment.txt"
cc --version >> "$output/environment.txt"
# Obtain the link base from the same platform header used by XNU and native C.
base=$(printf 'XNU_HOST_KERNEL_BASE\n' | cc -E -P -x c \
  -idirafter third_party/xnu/osfmk/arm -include usermode_vm.h -)
flags=(-Wall -Wextra -Werror -O1 -g -pthread -Isrc/xnu-host/include
  -idirafter third_party/xnu/osfmk/arm -ffunction-sections -fdata-sections)
cc "${flags[@]}" "${entry_flags[@]}" -c src/xnu-host/host.c -o "$output/host.o"
cc "${flags[@]}" -c src/xnu-host/memory.c -o "$output/memory.o"
cc "${flags[@]}" -c src/xnu-host/native_time.c -o "$output/native_time.o"
cc "${flags[@]}" -c src/xnu-host/native_entropy.c -o "$output/native_entropy.o"
cc "${flags[@]}" -c src/xnu-host/native_fault.c -o "$output/native_fault.o"
# Native Linux calls must never resolve to same-named XNU implementations.
# Inspect the actual objects, including future providers, before linking them.
python3 - "$output" <<'PY'
from pathlib import Path
import subprocess
import sys

output = Path(sys.argv[1])
def symbols(objects, mode):
    listing = subprocess.check_output(
        ['nm', '-g', '--format=posix', mode, *map(str, objects)], text=True)
    return {fields[0] for line in listing.splitlines()
            if len(fields := line.split()) >= 2 and len(fields[1]) == 1}

native = symbols(sorted(output.glob('*.o')), '--undefined-only')
kernel = symbols(sorted(Path('build/kernel').glob('*.o')), '--defined-only')
collisions = sorted(name for name in native & kernel
                    if not name.startswith('xnu_host_'))
report = output / 'native-kernel-collisions.txt'
report.write_text('\n'.join(collisions) + ('\n' if collisions else 'No native/kernel ABI collisions.\n'))
print(report.read_text(), end='')
if collisions:
    raise SystemExit('Native Linux imports collide with XNU definitions: ' + ', '.join(collisions))
PY
link_stage=0
if [[ $stage == vm ]]; then link_stage=1; fi
cc -E -P -x c -DXNU_HOST_VM_STAGE="$link_stage" src/xnu-host/startup.ld > "$output/startup.ld"
link_flags=(-no-pie -pthread -Wl,--gc-sections -Wl,-Ttext-segment="$base"
  -Wl,-T,"$output/startup.ld" -Wl,-Map,"$output/link-map.txt")
if [[ $stage == early ]]; then
  objects=(build/kernel/startup.o build/kernel/xnu_bootstrap.o build/kernel/context.o
    build/kernel/platform.o build/kernel/device_tree.o
    build/kernel/pmap.o build/kernel/time.o build/kernel/rtclock.o
    build/kernel/OSAtomicOperations.o build/kernel/entropy.o build/kernel/image.o
    build/kernel/ticket.o build/kernel/usermode_ticket.o
    build/kernel/copyio_probe.o build/kernel/usermode_copyio.o
    build/kernel/codec_probe.o build/kernel/WKdmCompress_16k.o
    build/kernel/WKdmDecompress_16k.o build/kernel/WKdmData.o build/kernel/lz4.o)
else
  ar rcs "$output/libxnu.a" build/kernel/*.o
  objects=(-Wl,--start-group "$output/libxnu.a" -Wl,--end-group)
fi
cc "${link_flags[@]}" "$output/host.o" "$output/memory.o" "$output/native_time.o" "$output/native_entropy.o" "$output/native_fault.o" "${objects[@]}" \
  -o "$output/xnu-host" 2>&1 | tee "$output/link.log"
file "$output/xnu-host" > "$output/file.txt"
if [[ $stage == full ]]; then
  (cd "$output" && sha256sum xnu-host > SHA256SUMS)
  echo 'Full startup registration graph linked; not executed.'
  exit 0
fi
run_host() {
  local suffix=$1
  local status=0
  timeout 30 "$output/xnu-host" startup_debug=1 \
    > "$output/stdout$suffix.txt" 2> "$output/stderr$suffix.txt" || status=$?
  # Preserve the actual failure and its output in Actions logs, not just in
  # an archive that would require downloading all objects to inspect a panic.
  cat "$output/stdout$suffix.txt" "$output/stderr$suffix.txt"
  return "$status"
}
run_host ""
if [[ $stage == early ]]; then
  # A previous ASLR-dependent brk collision escaped a single successful startup.
  # Preserve every independent process start, including its actual native brk.
  for attempt in {2..8}; do
    run_host "-$attempt"
  done
fi
(cd "$output" && sha256sum xnu-host > SHA256SUMS)
