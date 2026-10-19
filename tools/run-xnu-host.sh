#!/usr/bin/env bash
# Runner stage called only by the repository's build.sh workflow.
set -euo pipefail
stage=${1:?expected early or vm}
case "$stage" in
  early) output=build/startup; entry_flags=() ;;
  vm) output=build/vm; entry_flags=(-DXNU_HOST_BOOT_VM) ;;
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
# The guest CPU engine is a pinned fork dependency. It supplies
# instructions/registers only; this does not add a substitute Darwin runtime.
engine_objects=()
engine_link=()
if [[ $stage != early ]]; then
  python3 - <<'ENGINE_PIN'
import json
from pathlib import Path
import subprocess
lock = json.loads(Path('sources.lock.json').read_text())['guestExecutionEngine']
actual = subprocess.check_output(['git', '-C', lock['path'], 'rev-parse', 'HEAD'], text=True).strip()
if actual != lock['commit']:
    raise SystemExit(f"Guest CPU source mismatch: {actual} != {lock['commit']}")
print(f"GUEST_CPU_SOURCE {lock['repository']} {actual}")
ENGINE_PIN
  cmake -S third_party/unicorn -B "build/engine-$stage" \
    -DCMAKE_BUILD_TYPE=Release -DUNICORN_ARCH=aarch64 \
    -DUNICORN_BUILD_TESTS=OFF -DUNICORN_INSTALL=OFF -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$PWD/$output/engine"
  cmake --build "build/engine-$stage" --target unicorn -j2
  flags+=(-Ithird_party/unicorn/include)
  cc "${flags[@]}" -c src/xnu-host/native_guest.c -o "$output/native_guest.o"
  engine_objects=("$output/native_guest.o")
  engine_link=(-L"$output/engine" -lunicorn '-Wl,-rpath,$ORIGIN/engine')
fi
cc "${flags[@]}" "${entry_flags[@]}" -c src/xnu-host/host.c -o "$output/host.o"
cc "${flags[@]}" "${entry_flags[@]}" -c src/xnu-host/memory.c -o "$output/memory.o"
cc "${flags[@]}" -c src/xnu-host/native_time.c -o "$output/native_time.o"
cc "${flags[@]}" -c src/xnu-host/native_entropy.c -o "$output/native_entropy.o"
cc "${flags[@]}" -c src/xnu-host/native_fault.c -o "$output/native_fault.o"
cc "${flags[@]}" -c src/xnu-host/native_console.c -o "$output/native_console.o"
cc "${flags[@]}" -c src/xnu-host/native_boot_image.c -o "$output/native_boot_image.o"
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
# Dynamic dependencies also import host libc. Include their actual imports in
# the same collision audit so kernel exports cannot interpose on the engine.
for library in (output / 'engine').glob('*.so.*'):
    if library.is_symlink():
        continue
    listing = subprocess.check_output(['nm', '-D', '-g', '--undefined-only',
                                      '--format=posix', str(library)], text=True)
    native.update(line.split()[0].split('@')[0] for line in listing.splitlines()
                  if len(line.split()) >= 2)
original_objects = sorted(Path('build/kernel').glob('*.o'))
definitions = symbols(original_objects, '--defined-only')
# Keep the explicit xnu_host_ bridge ABI. Isolate every other actual kernel
# definition, including its references in other kernel objects, from host libc
# and the CPU engine. Do this to copies: compiler receipt hashes stay valid.
# Object linkage avoids C macros corrupting sysctl names/structure members.
renames = output / 'kernel-symbol-namespace.txt'
renames.write_text(''.join(f'{name} __arlinux_xnu_{name}\n'
                          for name in sorted(definitions)
                          if not name.startswith('xnu_host_')))
kernel_objects = output / 'kernel-objects'
kernel_objects.mkdir(exist_ok=True)
for original in original_objects:
    subprocess.run(['objcopy', '--redefine-syms=' + str(renames),
                    str(original), str(kernel_objects / original.name)], check=True)
kernel = symbols(sorted(kernel_objects.glob('*.o')), '--defined-only')
collisions = sorted(name for name in native & kernel
                    if not name.startswith('xnu_host_'))
report = output / 'native-kernel-collisions.txt'
report.write_text('\n'.join(collisions) + ('\n' if collisions else 'No native/kernel ABI collisions.\n'))
print(report.read_text(), end='')
if collisions:
    raise SystemExit('Native Linux imports collide with XNU definitions: ' + ', '.join(collisions))
PY
cc -E -P -x c src/xnu-host/startup.ld > "$output/startup.ld"
link_flags=(-no-pie -pthread -Wl,--gc-sections -Wl,-Ttext-segment="$base"
  -Wl,-T,"$output/startup.ld" -Wl,-Map,"$output/link-map.txt")
if [[ $stage == early ]]; then
  objects=(build/kernel/startup.o build/kernel/xnu_bootstrap.o build/kernel/context.o
    build/kernel/platform.o build/kernel/device_tree.o build/kernel/machine.o
    build/kernel/pmap.o build/kernel/time.o build/kernel/rtclock.o
    build/kernel/OSAtomicOperations.o build/kernel/entropy.o build/kernel/image.o
    build/kernel/ticket.o build/kernel/usermode_ticket.o
    build/kernel/copyio_probe.o build/kernel/usermode_copyio.o
    build/kernel/codec_probe.o build/kernel/WKdmCompress_16k.o
    build/kernel/WKdmDecompress_16k.o build/kernel/WKdmData.o build/kernel/lz4.o)
  for index in "${!objects[@]}"; do
    objects[$index]="$output/kernel-objects/$(basename "${objects[$index]}")"
  done
else
  ar rcs "$output/libxnu.a" "$output"/kernel-objects/*.o
  # KEEP cannot extract otherwise-unreferenced archive members. Bring every
  # declared kernel object into the link so its startup/type registrations
  # exist, then let section GC remove only unreferenced non-registration code.
  objects=(-Wl,--whole-archive "$output/libxnu.a" -Wl,--no-whole-archive)
fi
cc "${link_flags[@]}" "$output/host.o" "$output/memory.o" "$output/native_time.o" "$output/native_entropy.o" "$output/native_fault.o" "$output/native_console.o" "$output/native_boot_image.o" "${engine_objects[@]}" "${objects[@]}" "${engine_link[@]}" \
  -o "$output/xnu-host" 2>&1 | tee "$output/link.log"
file "$output/xnu-host" > "$output/file.txt"
run_host() {
  local suffix=$1
  local status=0
  local boot_inputs=()
  if [[ $stage != early ]]; then
    (cd build/kernel/guest && sha256sum -c SHA256SUMS)
    cat build/kernel/guest/SHA256SUMS
    boot_inputs=("root_image=$PWD/build/kernel/guest/init.macho" rd=md0)
  fi
  timeout 30 "$output/xnu-host" startup_debug=1 "${boot_inputs[@]}" \
    > "$output/stdout$suffix.txt" 2> "$output/stderr$suffix.txt" || status=$?
  # Preserve the actual failure and its output in Actions logs, not just in
  # an archive that would require downloading all objects to inspect a panic.
  cat "$output/stdout$suffix.txt" "$output/stderr$suffix.txt"
  if [[ $stage != early ]]; then
    # PID 1 must stay alive. A controlled timeout is accepted only after the
    # real guest completed its memory/COW/exit/wait journey and woke again.
    # An early zero exit, panic, empty output or generic quiet boot is failure.
    python3 - "$output" "$status" <<'JOURNEY'
import json
from pathlib import Path
import sys

output = Path(sys.argv[1])
status = int(sys.argv[2])
console = (output / 'stdout.txt').read_bytes() + (output / 'stderr.txt').read_bytes()
observations = console.count(b'XNU JOURNEY PASS: memory COW exit wait')
passed = status == 124 and observations >= 2 and b'XNU JOURNEY FAIL' not in console
result = {'execution': 'native Linux-hosted XNU with emulated ARM64 user instructions',
          'processStatus': status, 'controlledTimeout': status == 124,
          'completedJourneyObservations': observations, 'passed': passed}
(output / 'journey.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result), flush=True)
if not passed:
    raise SystemExit('Original XNU process journey not completed; inspect actual boot/failure logs')
JOURNEY
    return $?
  fi
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
