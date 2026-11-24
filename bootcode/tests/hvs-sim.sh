#!/bin/bash
# HDMI step 2 (redox-rpi #442): the HVS plane for the ARM's framebuffer.
# Runs mbox.c (with HDMI=1) and hvs.c in the resim VC4 simulator (vc4emul):
# framebuffer requests go through the real mailbox tag path, and the plane
# words that hvs.c builds for each request are compared with the expected
# ones (layout from Linux vc4_regs.h; format, order and expansion as
# start.elf's plane in Pi test row 337). The test pattern's bytes are
# checked against a Python model, which then prints the CRC-32s that the
# monitor's fb command must report on the Pi.
#
#   bootcode/tests/hvs-sim.sh <vc4-prefix> <vc4emul>
#
# KEEP=dir keeps the build and the simulator output there.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
bc=$(cd "$here/.." && pwd)
fw=$(cd "$bc/.." && pwd)
prefix=${1:?usage: hvs-sim.sh <vc4-prefix> <vc4emul>}
emul=${2:?usage: hvs-sim.sh <vc4-prefix> <vc4emul>}
if [ -n "${KEEP:-}" ]; then t=$KEEP; mkdir -p "$t"; rm -rf "${t:?}"/*
else t=$(mktemp -d); trap 'rm -rf "$t"' EXIT; fi

# resim has no "switch" instruction: no jump tables
cflags=(-std=c11 -Os -ffreestanding -fno-builtin -fno-jump-tables
	-Wall -Wextra -Werror -Wno-multichar -D__VIDEOCORE4__ -DMONITOR=0 -DHDMI=1 -DMBOX_TRACE=0
	-I"$bc" -isystem "$fw" -isystem "$fw/vc4_include")
"${prefix}gcc" "${cflags[@]}" -c "$here/hvs-sim-main.c" -o "$t/main.o"
"${prefix}gcc" "${cflags[@]}" -c "$bc/hvs.c" -o "$t/hvs.o"
"${prefix}gcc" "${cflags[@]}" -c "$bc/printf.c" -o "$t/printf.o"
"${prefix}gcc" -T vc4-sim.ld "$t/main.o" "$t/hvs.o" "$t/printf.o" -o "$t/sim.elf"
"${prefix}objcopy" -O binary "$t/sim.elf" "$t/sim.bin"
timeout 300 "$emul" "$t/sim.bin" 0 0 > "$t/out" 2> "$t/err" || { tail -20 "$t/out" "$t/err"; exit 1; }

# the pattern model: bars and border as in hvs.c fb_pattern(); prints the
# CRC-32 (zlib, as the bootcode's crc32()) or, with "hex", the bytes
pattern_crc() { # w h depth order pitch [hex]
	python3 -I - "$@" <<'P'
import sys, zlib, struct
w, h, depth, order, pitch = map(int, sys.argv[1:6])
bars = [0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x000000]
def pix(rgb):
    if (not order) if depth == 16 else order:  # hvs.c fb_pixel(), #82
        rgb = (rgb & 0x00ff00) | ((rgb >> 16) & 0xff) | ((rgb & 0xff) << 16)
    if depth == 16:
        return ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x1f)
    return rgb
fmt = "<H" if depth == 16 else "<I"
out = bytearray()
for y in range(h):
    row = bytearray()
    for x in range(w):
        rgb = 0xffffff if x in (0, w - 1) or y in (0, h - 1) else bars[x * 8 // w]
        row += struct.pack(fmt, pix(rgb))
    row += bytes(pitch - len(row))  # fb_pattern() leaves the pad; the sim memory is 0 there
    out += row
print(bytes(out).hex().upper() if sys.argv[6:] == ["hex"] else "%08X" % zlib.crc32(bytes(out)))
P
}

cat > "$t/want" <<W
RESULT size 1920 1080
PLANE 1920x1080 32 bit order 1: 47005817 FF000000 44380780 C0C0C0C0 FE400000 C0C0C0C0 00001E00
RESULT fb32 ok base FE400000 pitch 7680
PLANE 1024x768 16 bit order 0: 47007814 FF09C1C0 43000400 C0C0C0C0 FE400000 C0C0C0C0 00000800
RESULT fb16 ok base FE400000 pitch 2048
PLANE 1024x768 16 bit order 0: 47007814 FF09C1C0 43000400 C0C0C0C0 FE400000 C0C0C0C0 00000800
PLANE 1024x768 16 bit order 0: 47007814 FF09C1C0 43000400 C0C0C0C0 FE580000 C0C0C0C0 00000800
PLANE 1024x768 8 bit order 0: none
PLANE released
RESULT after-release done
RESULT pattern 16x4x32 RGB bad 0 bytes $(pattern_crc 16 4 32 1 64 hex)
RESULT pattern 16x4x16 BGR pitch 40 bad 0 bytes $(pattern_crc 16 4 16 0 40 hex)
W
fail=0
if grep -E '^(RESULT|PLANE)' "$t/out" | diff -u "$t/want" - > "$t/diff"; then
	echo "ok   plane words, mailbox path and pattern ($(grep -cE '^(RESULT|PLANE)' "$t/out") lines)"
else
	echo "FAIL"; cat "$t/diff"; fail=1
fi
grep -q "hvs-sim: end" "$t/out" || { echo "FAIL did not reach the end"; fail=1; }

# CRCs the monitor's fb command must print on the Pi (pitch = width * bytes)
echo "Pi: fb 780 438 20 1 (1920x1080 32 bit RGB) -> crc32 $(pattern_crc 1920 1080 32 1 7680)"
echo "Pi: fb 400 300 10 0 (1024x768 16 bit BGR)  -> crc32 $(pattern_crc 1024 768 16 0 2048)"
[ $fail = 0 ] && echo "all ok"
exit $fail
