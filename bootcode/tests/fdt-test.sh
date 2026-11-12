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
head -c 4096 /dev/urandom > "$t/junk"
name="non-devicetree refused"
check bash -c "! '$t/fdt-host' '$t/junk' '$t/x' 1 >/dev/null"
exit $fail
