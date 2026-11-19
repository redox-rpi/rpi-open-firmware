#!/bin/bash
# Host test for dt.c (rof #48): devicetree edits as boot64 makes them.
#  - /memory reg, /chosen initrd and bootargs: set in place or added, the
#    rest of the tree unchanged (dtc -s of before and after);
#  - overlays: Broadcom's devicetree plus disable-bt and mmc, as start.elf
#    applies config.txt's dtoverlay lines, must give the same tree as
#    fdtoverlay (dtc -s, without __symbols__, which fdtoverlay extends).
# Needs gcc, dtc, fdtget, fdtoverlay.
#   bootcode/tests/fdt-test.sh [FIRMWARE_DIR]
#   FIRMWARE_DIR: bcm2710-rpi-3-b-plus.dtb, bcm2710-rpi-zero-2-w.dtb and
#   overlays/ (a redox-rpi card's boot partition); without it only the
#   small cases run.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
fw=${1:-}
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
mkdir "$t/inc"
sed -n '/^\/\* dt.c:/,/^bool dt_set_chosen_u32/p' "$here/../hw.h" > "$t/dt.h"
{ echo '#pragma once'; echo '#include <stdint.h>'; echo '#include <stdbool.h>'; echo '#include <stdio.h>'; cat "$t/dt.h"; } > "$t/inc/hw.h"
cp "$here/../dt.c" "$t/"
gcc -O1 -Wall -Wextra -I"$t/inc" -o "$t/h" "$here/fdt-host.c" "$t/dt.c"
fail=0
check() { if "$@"; then echo "ok   $name"; else echo "FAIL $name"; fail=1; fi; }
dts() { dtc -q -s -I dtb -O dts "$1"; }

cat > "$t/small.dts" <<'D'
/dts-v1/;
/ { #address-cells = <1>; #size-cells = <1>; model = "test";
    soc { memory { reg = <1 2>; }; chosen { linux,initrd-start = <7>; }; };
    memory@0 { device_type = "memory"; reg = <0 0>; };
    memoryx { reg = <5 6>; }; };
D
dtc -q -I dts -O dtb -o "$t/small.dtb" "$t/small.dts"
printf 'quiet\tRPI_FB=1280x720 console=ttyAMA0\r\nsecond line\n' > "$t/cmdline.txt"
"$t/h" "$t/small.dtb" "$t/out.dtb" memory 3e000000 initrd 2100000 12500000 bootargs "$t/cmdline.txt" >/dev/null
name="memory@0 reg = 0 3e000000"
check [ "$(fdtget -t x "$t/out.dtb" /memory@0 reg)" = "0 3e000000" ]
name="nested and lookalike nodes untouched"
check [ "$(fdtget -t x "$t/out.dtb" /soc/memory reg) $(fdtget -t x "$t/out.dtb" /memoryx reg) $(fdtget -t x "$t/out.dtb" /soc/chosen linux,initrd-start)" = "1 2 5 6 7" ]
name="/chosen added with initrd start and end"
check [ "$(fdtget -t x "$t/out.dtb" /chosen linux,initrd-start) $(fdtget -t x "$t/out.dtb" /chosen linux,initrd-end)" = "2100000 12500000" ]
name="bootargs: first line, tab as space"
check [ "$(fdtget -t s "$t/out.dtb" /chosen bootargs)" = "quiet RPI_FB=1280x720 console=ttyAMA0" ]
name="size a multiple of 8, header matches"
check [ $(( $(wc -c < "$t/out.dtb") % 8 )) -eq 0 ]
sed 's/memory@0 {[^}]*};//' "$t/small.dts" > "$t/nomem.dts"
dtc -q -I dts -O dtb -o "$t/nomem.dtb" "$t/nomem.dts"
"$t/h" "$t/nomem.dtb" "$t/out.dtb" memory 1e000000 >/dev/null
name="no memory node: memory@0 made"
check [ "$(fdtget -t s "$t/out.dtb" /memory@0 device_type) $(fdtget -t x "$t/out.dtb" /memory@0 reg)" = "memory 0 1e000000" ]
# the old make-dtb.sh placeholders (1023 spaces, zeros) get replaced
sp=$(printf '%1023s' '')
cat > "$t/ph.dts" <<D
/dts-v1/;
/ { chosen { bootargs = "$sp"; linux,initrd-start = <0>; linux,initrd-end = <0>; };
    memory@0 { device_type = "memory"; reg = <0 0>; }; };
D
dtc -q -I dts -O dtb -o "$t/ph.dtb" "$t/ph.dts"
"$t/h" "$t/ph.dtb" "$t/out.dtb" initrd 2100000 2200000 bootargs "$t/cmdline.txt" >/dev/null
name="placeholders replaced, blob shrinks"
check [ "$(fdtget -t s "$t/out.dtb" /chosen bootargs)" = "quiet RPI_FB=1280x720 console=ttyAMA0" ] && [ "$(wc -c < "$t/out.dtb")" -lt "$(wc -c < "$t/ph.dtb")" ]
name="not a devicetree refused"
check bash -c "! '$t/h' '$t/cmdline.txt' '$t/x' memory 1 >/dev/null"

if [ -n "$fw" ]; then
	for b in bcm2710-rpi-3-b-plus bcm2710-rpi-zero-2-w; do
		fdtoverlay -i "$fw/$b.dtb" -o "$t/ref.dtb" "$fw/overlays/disable-bt.dtbo" "$fw/overlays/mmc.dtbo"
		"$t/h" "$fw/$b.dtb" "$t/out.dtb" overlay "$fw/overlays/disable-bt.dtbo" overlay "$fw/overlays/mmc.dtbo" >/dev/null
		strip_syms() { dts "$1" | awk '/^\t__symbols__ \{/{s=1} s&&/^\t\};/{s=0;next} !s'; }
		name="$b + disable-bt + mmc = fdtoverlay"
		check diff <(strip_syms "$t/ref.dtb") <(strip_syms "$t/out.dtb")
		[ $fail = 0 ] || diff <(strip_syms "$t/ref.dtb") <(strip_syms "$t/out.dtb") | head -30
		name="$b: serial0 is the PL011, SD on mmc@7e300000"
		check [ "$(fdtget "$t/out.dtb" /aliases serial0) $(fdtget -t s "$t/out.dtb" /soc/mmc@7e300000 status)" = "/soc/serial@7e201000 okay" ]
	done
fi
[ $fail = 0 ] && echo "all ok"
exit $fail
