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
cc "${flags[@]}" "${entry_flags[@]}" -c src/xnu-host/host.c -o "$output/host.o"
cc "${flags[@]}" -c src/xnu-host/memory.c -o "$output/memory.o"
cc "${flags[@]}" -c src/xnu-host/native_time.c -o "$output/native_time.o"
cc "${flags[@]}" -c src/xnu-host/native_entropy.c -o "$output/native_entropy.o"
link_flags=(-no-pie -pthread -Wl,--gc-sections -Wl,-Ttext-segment="$base"
  -Wl,-T,src/xnu-host/startup.ld -Wl,-Map,"$output/link-map.txt")
if [[ $stage == early ]]; then
  objects=(build/kernel/startup.o build/kernel/xnu_bootstrap.o build/kernel/context.o
    build/kernel/pmap.o build/kernel/time.o build/kernel/rtclock.o
    build/kernel/OSAtomicOperations.o build/kernel/entropy.o build/kernel/image.o)
else
  ar rcs "$output/libxnu.a" build/kernel/*.o
  objects=(-Wl,--start-group "$output/libxnu.a" -Wl,--end-group)
fi
cc "${link_flags[@]}" "$output/host.o" "$output/memory.o" "$output/native_time.o" "$output/native_entropy.o" "${objects[@]}" \
  -o "$output/xnu-host" 2>&1 | tee "$output/link.log"
file "$output/xnu-host" > "$output/file.txt"
timeout 30 "$output/xnu-host" startup_debug=1 > "$output/stdout.txt" 2> "$output/stderr.txt"
cat "$output/stdout.txt" "$output/stderr.txt"
if [[ $stage == early ]]; then
  # A previous ASLR-dependent brk collision escaped a single successful startup.
  # Preserve every independent process start, including its actual native brk.
  for attempt in {2..8}; do
    timeout 30 "$output/xnu-host" startup_debug=1 \
      > "$output/stdout-$attempt.txt" 2> "$output/stderr-$attempt.txt"
    cat "$output/stdout-$attempt.txt" "$output/stderr-$attempt.txt"
  done
fi
(cd "$output" && sha256sum xnu-host > SHA256SUMS)
