#!/bin/bash
# The boot screen (#430) in the resim VC4 simulator (vc4emul): bootscreen.c
# draws the monitor's bs steps, a handover and a long line into memory; the
# panel's band sums are compared with bootscreen-model.py, which also prints
# the CRC-32 that bs must report on the Pi.
#
#   bootcode/tests/bootscreen-sim.sh <vc4-prefix> <vc4emul>
#
# KEEP=dir keeps the build and the simulator output there.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
bc=$(cd "$here/.." && pwd)
fw=$(cd "$bc/.." && pwd)
prefix=${1:?usage: bootscreen-sim.sh <vc4-prefix> <vc4emul>}
emul=${2:?usage: bootscreen-sim.sh <vc4-prefix> <vc4emul>}
if [ -n "${KEEP:-}" ]; then t=$KEEP; mkdir -p "$t"; rm -rf "${t:?}"/*
else t=$(mktemp -d); trap 'rm -rf "$t"' EXIT; fi

# resim has no "switch" instruction: no jump tables
cflags=(-std=c11 -Os -ffreestanding -fno-builtin -fno-jump-tables
	-Wall -Wextra -Werror -Wno-multichar -D__VIDEOCORE4__ -DMONITOR=0 -DHDMI=1
	-I"$bc" -isystem "$fw" -isystem "$fw/vc4_include")
for f in "$here/bootscreen-sim-main.c" "$bc/bootscreen.c" "$bc/hvs.c" "$bc/printf.c"; do
	"${prefix}gcc" "${cflags[@]}" -c "$f" -o "$t/$(basename "$f" .c).o"
done
"${prefix}gcc" -T vc4-sim.ld "$t"/*.o -o "$t/sim.elf"
"${prefix}objcopy" -O binary "$t/sim.elf" "$t/sim.bin"
timeout 600 "$emul" "$t/sim.bin" 0 0 > "$t/out" 2> "$t/err" || { tail -20 "$t/out" "$t/err"; exit 1; }

{
	echo "PLANE 640x160 16 bit order 1: 47005814 FF1CC280 40A00280 C0C0C0C0 02000000 C0C0C0C0 00000500"
	python3 -I "$here/bootscreen-model.py" "$bc/font8x16.h" | sed -n 1p
	python3 -I "$here/bootscreen-model.py" "$bc/font8x16.h" | sed -n 2p
	echo "PLANE 640x160 16 bit order 1: 47005814 FF1CC280 40A00280 C0C0C0C0 02000000 C0C0C0C0 00000500"
	python3 -I "$here/bootscreen-model.py" "$bc/font8x16.h" | sed -n 3,4p
} > "$t/want"
fail=0
if grep -E '^(RESULT|PLANE)' "$t/out" | diff -u "$t/want" - > "$t/diff"; then
	echo "ok   plane, demo, handover, long line and title ($(grep -cE '^(RESULT|PLANE)' "$t/out") lines)"
else
	echo "FAIL"; cat "$t/diff"; fail=1
fi
grep -q "bootscreen-sim: end" "$t/out" || { echo "FAIL did not reach the end"; fail=1; }
grep -q "^bootscreen: handed over" "$t/out" || { echo "FAIL no handover line"; fail=1; }
echo "Pi: bs -> crc32 $(python3 -I "$here/bootscreen-model.py" "$bc/font8x16.h" crc)"
[ $fail = 0 ] && echo "all ok"
exit $fail
