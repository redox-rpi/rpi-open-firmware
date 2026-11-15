#!/bin/bash
# The mailbox trace (redox-rpi #443): the release build prints no line per
# mailbox request and no "alive" line, the debug build (MBOX_TRACE=1) and
# the monitor build do. Mailbox errors are printed in every build.
#
# 1. Builds bootcode.bin three times (release BOOT=1, debug release
#    BOOT=1 MBOX_TRACE=1, monitor MONITOR=1) and looks for the trace's
#    format strings in each image.
# 2. Runs mbox.c in the resim VC4 simulator (vc4emul), built with
#    MBOX_TRACE=0 and =1, with one request buffer (an answered and an
#    unanswered tag), a buffer that is too small, and the alive timer
#    (tests/mbox-sim-main.c), and checks what it prints on the serial line.
#
#   bootcode/tests/mbox-sim.sh <vc4-prefix> <vc4emul>
#   e.g. bootcode/tests/mbox-sim.sh ~/rbc-build/toolchains/prefix/bin/vc4-elf- \
#        ~/rbc-build/resim-fork/build/vc4emul/vc4emul
#
# vc4emul is redox-rpi/resim's simulator. KEEP=dir keeps the builds and the
# simulator output there.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
bc=$(cd "$here/.." && pwd)
fw=$(cd "$bc/.." && pwd)
prefix=${1:?usage: mbox-sim.sh <vc4-prefix> <vc4emul>}
emul=${2:?usage: mbox-sim.sh <vc4-prefix> <vc4emul>}
if [ -n "${KEEP:-}" ]; then t=$KEEP; mkdir -p "$t"; rm -rf "${t:?}"/*
else t=$(mktemp -d); trap 'rm -rf "$t"' EXIT; fi

fail=0
check() { if "$@"; then echo "ok   $name"; else echo "FAIL $name"; fail=1; fi; }
has() { grep -qF -- "$2" "$1"; }
hasnt() { ! grep -qF -- "$2" "$1"; }

# ---- 1. the real images ----
build() { # name make-vars...
	local d="$t/$1"; shift
	mkdir -p "$d"
	cp -r "$bc"/*.c "$bc"/*.h "$bc"/*.S "$bc"/*.ld "$bc"/Makefile "$d/"
	make -s -C "$d" FW_DIR="$fw" CROSS_COMPILE="$prefix" BUILD_ID=mbox-sim "$@" > "$d/make.log" 2>&1 ||
		{ cat "$d/make.log"; return 1; }
	strings -n 6 "$d/build/bootcode.bin" > "$d/strings"
}
build release BOOT=1
build debug BOOT=1 MBOX_TRACE=1
build monitor MONITOR=1

for b in release debug monitor; do
	s="$t/$b/strings"
	name="$b image: mailbox errors stay"
	check has "$s" "mbox: bad buffer"
	if [ $b = release ]; then
		name="release image: no request trace"
		check hasnt "$s" "mbox: requests after this one"
		name="release image: no alive line"
		check hasnt "$s" "mbox: alive"
		name="release image: banner"
		check grep -qx "redox-rpi bootcode mbox-sim (release, boots from the SD card)" "$s"
	else
		name="$b image: request trace"
		check has "$s" "mbox: requests after this one"
		name="$b image: alive line"
		check has "$s" "mbox: alive"
	fi
done
name="debug image: banner says mailbox trace"
check has "$t/debug/strings" "release, boots from the SD card, mailbox trace"
echo "sizes: release $(stat -c %s "$t/release/build/bootcode.bin")," \
	"debug $(stat -c %s "$t/debug/build/bootcode.bin") bytes"

# ---- 2. mbox.c in resim ----
# resim has no "switch" instruction: no jump tables
cflags=(-std=c11 -Os -ffreestanding -fno-builtin -fno-jump-tables
	-Wall -Wextra -Werror -Wno-multichar -D__VIDEOCORE4__ -DMONITOR=0
	-I"$bc" -isystem "$fw" -isystem "$fw/vc4_include")
for trace in 0 1; do
	d="$t/sim$trace"
	mkdir -p "$d"
	"${prefix}gcc" "${cflags[@]}" -DMBOX_TRACE=$trace -c "$here/mbox-sim-main.c" -o "$d/main.o"
	"${prefix}gcc" "${cflags[@]}" -c "$bc/printf.c" -o "$d/printf.o"
	"${prefix}gcc" -T vc4-sim.ld "$d/main.o" "$d/printf.o" -o "$d/sim.elf"
	"${prefix}objcopy" -O binary "$d/sim.elf" "$d/sim.bin"
	if ! timeout 120 "$emul" "$d/sim.bin" 0 0 > "$d/out" 2> "$d/err"; then
		echo "FAIL MBOX_TRACE=$trace: vc4emul stopped"; tail -20 "$d/out" "$d/err"; fail=1
		continue
	fi
	o="$d/out"
	name="sim $trace: ran to the end"
	check has "$o" "mbox-sim: end"
	name="sim $trace: request answered (code done, revision tag answered, EDID not)"
	check has "$o" "RESULT code 80000000 revision-tag 80000004 edid-tag 00000004"
	name="sim $trace: bad buffer error printed"
	check has "$o" "mbox: bad buffer 00800000 size 5, dropped"
	if [ $trace = 0 ]; then
		name="sim 0: no request line"
		check hasnt "$o" "mbox: 00010002"
		name="sim 0: no alive line"
		check hasnt "$o" "mbox: alive"
	else
		name="sim 1: request line with the unanswered tag"
		check grep -qx "mbox: 00010002 00030020?" "$o"
		name="sim 1: one alive line at 10 s"
		check [ "$(grep -c "^mbox: alive" "$o")" = 1 ]
		name="sim 1: alive line counts the request"
		check has "$o" "mbox: alive 10 s, 1 requests"
	fi
done
[ $fail = 0 ] && echo "all ok"
exit $fail
