#!/bin/bash
# Load a file into the Pi's SDRAM through the bootcode serial monitor
# (test builds, MONITOR=1) and optionally run it.
#
# Usage: bootcode/rbc-load.sh [--enter] FILE [ADDR] [chain|go|none]
#   --enter  first wait for the monitor's "press Enter" line after a reset
#            and open the monitor
#   ADDR     default 0xc1000000 (SDRAM at 16 MB, uncached alias)
#   chain    start FILE as a bootcode.bin image (copied to 0x80000000)
#   go       call VPU code at ADDR and print its return value
#
# It only writes to the serial port. Replies are read from the log that
# the one serial reader on the host writes (cat /dev/ttyUSB0 >> LOG), so
# there is never a second reader.
#   TTY  default /dev/ttyUSB0
#   LOG  default ~/redox-rpi/logs/serial.log

set -euo pipefail

TTY="${TTY:-/dev/ttyUSB0}"
LOG="${LOG:-$HOME/redox-rpi/logs/serial.log}"

ENTER=0
if [ "${1:-}" = "--enter" ]; then ENTER=1; shift; fi
FILE="$1"
ADDR="${2:-0xc1000000}"
ACTION="${3:-none}"
LEN=$(stat -c %s "$FILE")

log_size() { stat -c %s "$LOG"; }

# wait_for OFFSET REGEX SECONDS: print the first matching log line after OFFSET
wait_for() {
	local off=$1 re=$2 secs=$3 line
	for ((i = 0; i < secs * 5; i++)); do
		line=$(tail -c +$((off + 1)) "$LOG" | tr -d '\r' | grep -a -m1 -E "$re" || true)
		if [ -n "$line" ]; then echo "$line"; return 0; fi
		sleep 0.2
	done
	echo "timeout waiting for: $re" >&2
	return 1
}

send() { printf '%s\r' "$1" > "$TTY"; }

if [ $ENTER = 1 ]; then
	off=$(log_size)
	wait_for "$off" 'rbc> monitor: press Enter' 60 >/dev/null
	off=$(log_size)
	printf '\r' > "$TTY"
	wait_for "$off" '^ok monitor' 5 >/dev/null
	echo "monitor open"
	sleep 0.2
fi

want=$(python3 -I -c 'import sys, zlib; print("%08X" % zlib.crc32(open(sys.argv[1], "rb").read()))' "$FILE")
# 115200 baud is about 11 500 characters a second, 2 hex digits per byte
secs=$((LEN * 2 / 11500 + 1))
echo "loading $LEN bytes to $ADDR, crc32 $want, about $secs s"

# the monitor answers each err only after its input has been quiet for
# 0.5 s, then prints the prompt, so a retry needs no reset
try_load() {
	local off r
	off=$(log_size)
	send "l $ADDR $(printf '%x' "$LEN") $want"
	r=$(wait_for "$off" '^(ok send|err)' 5) || return 1
	case "$r" in ok*) ;; *) echo "$r" >&2; return 1 ;; esac
	off=$(log_size)
	xxd -p "$FILE" > "$TTY"
	r=$(wait_for "$off" '^(ok crc32|err)' $((secs + 10))) || return 1
	case "$r" in "ok crc32 $want") ;; *) echo "$r" >&2; return 1 ;; esac
}

for try in 1 2 3; do
	if try_load; then
		echo "loaded $LEN bytes at $ADDR, crc32 $want"
		break
	fi
	if [ $try = 3 ]; then echo "load failed 3 times" >&2; exit 1; fi
	echo "load try $try failed, retrying" >&2
	# the err line and the prompt come together; give the monitor time to
	# finish any drain before the next command
	sleep 1.5
done

case "$ACTION" in
chain)
	off=$(log_size)
	send "chain $ADDR $(printf '%x' "$LEN")"
	wait_for "$off" '^(ok chain|err)' 5
	;;
go)
	off=$(log_size)
	send "g $ADDR"
	wait_for "$off" '^(ok ret|err)' 30
	;;
none) ;;
*) echo "unknown action $ACTION" >&2; exit 1 ;;
esac
