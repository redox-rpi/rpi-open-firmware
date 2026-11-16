#!/bin/bash
# Host test for fdt.c: set /memory@0 reg in a devicetree and check it with
# fdtget; the rest of the blob must stay byte-for-byte the same. A blob
# without a memory node and a non-devicetree file must be refused.
# Needs gcc, dtc and fdtget.
#   bootcode/tests/fdt-test.sh [bcm2710-rpi-3-b-plus.dtb]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
mkdir "$t/inc"
cat > "$t/inc/hw.h" <<'H'
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
bool fdt_set_memory(uint8_t *dtb, uint32_t len, uint32_t base, uint32_t size);
bool fdt_set_initrd(uint8_t *dtb, uint32_t len, uint32_t start, uint32_t end);
bool fdt_set_bootargs(uint8_t *dtb, uint32_t len, const char *txt, uint32_t n);
H
cp "$here/../fdt.c" "$t/fdt.c"
gcc -O1 -Wall -Wextra -I"$t/inc" -o "$t/fdt-host" "$here/fdt-host.c" "$t/fdt.c"
fail=0
check() { if "$@"; then echo "ok   $name"; else echo "FAIL $name"; fail=1; fi; }

dtbs=("$@")
cat > "$t/small.dts" <<'D'
/dts-v1/;
/ { #address-cells = <1>; #size-cells = <1>; model = "test";
    soc { memory { reg = <1 2>; }; };
    memory@0 { device_type = "memory"; reg = <0 0>; };
    memoryx { reg = <5 6>; }; };
D
dtc -q -I dts -O dtb -o "$t/small.dtb" "$t/small.dts"
dtbs+=("$t/small.dtb")
for d in "${dtbs[@]}"; do
	b=$(basename "$d")
	"$t/fdt-host" "$d" "$t/out.dtb" 3e000000 >/dev/null
	name="$b: memory@0 reg = 0 3e000000"
	check [ "$(fdtget -t x "$t/out.dtb" /memory@0 reg)" = "0 3e000000" ]
	name="$b: only 4 bytes changed"
	check [ "$(cmp -l "$d" "$t/out.dtb" | wc -l)" -le 4 ]
done
name="small.dtb: nested and lookalike nodes untouched"
check [ "$(fdtget -t x "$t/out.dtb" /soc/memory reg) $(fdtget -t x "$t/out.dtb" /memoryx reg)" = "1 2 5 6" ]

sed 's/memory@0 {[^}]*};//' "$t/small.dts" > "$t/nomem.dts"
dtc -q -I dts -O dtb -o "$t/nomem.dtb" "$t/nomem.dts"
name="no memory node refused"
check bash -c "! '$t/fdt-host' '$t/nomem.dtb' '$t/x' 1 >/dev/null"
# /chosen initrd placeholders (#31): one cell as make-dtb.sh writes them,
# and two cells; a devicetree without them is refused
cat > "$t/initrd.dts" <<'D'
/dts-v1/;
/ { #address-cells = <1>; #size-cells = <1>;
    chosen { bootargs = "x"; linux,initrd-start = <0>; linux,initrd-end = <0 0>; };
    soc { chosen { linux,initrd-start = <7>; }; };
    memory@0 { device_type = "memory"; reg = <0 0>; }; };
D
dtc -q -I dts -O dtb -o "$t/initrd.dtb" "$t/initrd.dts"
"$t/fdt-host" "$t/initrd.dtb" "$t/out.dtb" 1e000000 2100000 12500000 >/dev/null
name="initrd: /chosen start and end set"
check [ "$(fdtget -t x "$t/out.dtb" /chosen linux,initrd-start) $(fdtget -t x "$t/out.dtb" /chosen linux,initrd-end)" = "2100000 0 12500000" ]
name="initrd: nested chosen untouched"
check [ "$(fdtget -t x "$t/out.dtb" /soc/chosen linux,initrd-start)" = "7" ]
name="initrd: no placeholders refused"
check bash -c "! '$t/fdt-host' '$t/small.dtb' '$t/x' 1 2 3 >/dev/null"
# /chosen bootargs from cmdline.txt (#31): the placeholder as make-dtb.sh
# writes it (1023 spaces), the first line only, tabs as spaces
sp=$(printf '%1023s' '')
cat > "$t/args.dts" <<D
/dts-v1/;
/ { #address-cells = <1>; #size-cells = <1>;
    chosen { bootargs = "$sp"; };
    memory@0 { device_type = "memory"; reg = <0 0>; }; };
D
dtc -q -I dts -O dtb -o "$t/args.dtb" "$t/args.dts"
printf 'quiet\tRPI_FB=1280x720 console=ttyAMA0\r\nsecond line\n' > "$t/cmdline.txt"
FDT_CMDLINE="$t/cmdline.txt" "$t/fdt-host" "$t/args.dtb" "$t/out.dtb" 1e000000 >/dev/null
got=$(fdtget -t s "$t/out.dtb" /chosen bootargs)
name="bootargs: first line, tab as space"
check [ "$(echo $got)" = "quiet RPI_FB=1280x720 console=ttyAMA0" ]
name="bootargs: same length, one string"
check [ "$(fdtget -t bx "$t/out.dtb" /chosen bootargs | wc -w)" = 1024 ]
name="bootargs: blob size unchanged"
check [ "$(wc -c <"$t/out.dtb")" = "$(wc -c <"$t/args.dtb")" ]
head -c 2000 /dev/zero | tr '\0' a > "$t/long.txt"
FDT_CMDLINE="$t/long.txt" "$t/fdt-host" "$t/args.dtb" "$t/out.dtb" 1e000000 >/dev/null
name="bootargs: long line cut at 1023"
check [ "$(fdtget -t s "$t/out.dtb" /chosen bootargs | wc -c)" = 1024 ]
name="bootargs: no placeholder refused"
check bash -c "! FDT_CMDLINE='$t/cmdline.txt' '$t/fdt-host' '$t/small.dtb' '$t/x' 1 >/dev/null"
head -c 4096 /dev/urandom > "$t/junk"
name="non-devicetree refused"
check bash -c "! '$t/fdt-host' '$t/junk' '$t/x' 1 >/dev/null"
exit $fail
