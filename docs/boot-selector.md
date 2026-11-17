# Boot selector

The boot selector is a `bootcode.bin` for a Raspberry Pi 3B+ (and other
BCM2837 boards) that lets you choose the boot path from the serial line,
with no card swap. Our test Pi has run it on its network boot card since
2026-11-10 (pi-tests [Test 313](https://github.com/redox-rpi/pi-tests/blob/main/reports/313.md)).

It is a **lab tool, not a release firmware**: its serial monitor gives
anyone on the serial pins full control of the chip.

## What it does

The Pi's Boot ROM loads `bootcode.bin` from the card. With the selector in
that place:

1. It prints its banner, for example
   `redox-rpi bootcode 56354b8 (M1, monitor, test build, boot selector)`.
2. **Halt check:** if the OS halted the Pi (see below), it stays off here.
3. It waits **5 seconds** for Enter on the serial line.
   - **No Enter:** it chains the Broadcom `bootcode.bin` built into it
     (`rbc> chain built-in bootcode.bin, 52624 bytes, crc32 53C9B136`). The
     Pi then boots exactly as with Broadcom's firmware: `start.elf`, U-Boot
     or a direct boot, network or card. The boot takes about 5 s longer.
   - **Enter:** our SDRAM set-up runs and the monitor opens (`rbc>`
     prompt). There you can read and write registers, load code over the
     serial line and run it, or chain another `bootcode.bin`, such as a
     new build of our firmware under test.
4. `boot` in the monitor, or 300 s without input, resets the chip. The
   selector then starts again and, with nobody pressing Enter, boots
   normally.

Broadcom's code is chained before our SDRAM set-up, because it does not
start once our set-up has run (pi-tests Tests 269, 313, 320).

## Halt after poweroff (new, rof #40)

Redox's `poweroff` (redox-rpi #418, PR #526) halts the Pi the way Linux
does: it writes partition 63 (`0x555`) into the `PM_RSTS` register and
resets through the watchdog. The firmware that starts next must notice
this and stay off.

From build `56354b8` on, the selector does so:
- It prints `halt: the OS halted the Pi (PM_RSTS …, partition 63);
  staying off, GPIO3 (pin 5) to ground wakes it`.
- It powers nothing up, runs the VPU from the 19.2 MHz crystal with PLLC
  off, and waits.
- GPIO3 (pin 5) held to ground for 50 ms clears the mark and starts the
  Pi again. Removing power does too.

The same check is in our release firmware ([PR #46](https://github.com/redox-rpi/rpi-open-firmware/pull/46)).

## Versions

| Build | Branch | What | Status |
|---|---|---|---|
| `56354b8` | `selector-halt` ([PR #52](https://github.com/redox-rpi/rpi-open-firmware/pull/52)) | 233c8c5 plus the halt check | **current**, on the test Pi since 2026-11-17 ([Test 495](https://github.com/redox-rpi/pi-tests/blob/main/reports/495.md)) |
| `233c8c5` | `bootcode` | `boot` and the 300 s timeout reset the chip | on the test Pi 2026-11-11 to 2026-11-17 (Test 320) |
| `b081bfd` | `bootcode` | chains before our SDRAM set-up | Test 313 step 3 |

The built-in Broadcom image is `bootcode.bin` from firmware 1.20260915,
52624 bytes, CRC-32 `53C9B136`, unchanged.

## Download

GitHub release [boot-selector-56354b8](https://github.com/redox-rpi/rpi-open-firmware/releases/tag/boot-selector-56354b8): `rpi-open-firmware-boot-selector-56354b8.bin`, 61376 bytes,
sha256 `4c89edbcec591f840e7cbf13b9aa0898de3d055c432a3d05c5d36ec94db28f99`
(also as `.sha256` next to it), CRC-32 `9de2a646`. In the lab network the same file is under `http://homenodellm:8080/sdcard/`.

Test 495 on the 3B+ checked:
- a normal boot to Redox;
- a cold start with Enter (monitor, then `boot`);
- a real Redox `poweroff`: halt line, then 0 bytes on serial and 0.0 W for 3 minutes.

## Putting it on a card

On the FAT boot partition:
1. Keep Broadcom's file as `bootcode-broadcom.bin`: that is the way back.
2. Keep the previous selector, for example as `bootcode-selector-233c8c5.bin`.
3. Copy the selector as `bootcode.bin`.

Everything else on the card stays as it is. To go back, copy one of the
kept files to `bootcode.bin` again. If the Pi no longer starts, that
needs someone at the card.

Serial console: 115200 baud on GPIO 14/15 (pins 8 and 10). There must be
only one reader on the host's serial port.

## Using the monitor from the host

[`bootcode/rbc-load.sh`](https://github.com/redox-rpi/rpi-open-firmware/blob/bootcode/bootcode/rbc-load.sh) sends a file through the monitor and can run it. It is on the branches `bootcode`, `release-0.1` and the ones built on them; it reaches the default branch with [PR #10](https://github.com/redox-rpi/rpi-open-firmware/pull/10).
- `rbc-load.sh --enter FILE` opens the monitor after a reset and loads FILE.
- `chain` starts FILE as a `bootcode.bin`.
- `chainmon` does the same for a monitor build and opens its monitor, so
  a newer selector can be tried without writing it to the card.

The script only writes to the serial port and reads the replies from the
log of the one serial reader. The commands (`r`, `w`, `d`, `l`, `crc`,
`g`, `chain`, `reset`, `boot`) are listed in
[bootcode/README.md](https://github.com/redox-rpi/rpi-open-firmware/blob/bootcode/bootcode/README.md)
and at the top of `bootcode/monitor.c`.

## Building

From the `bootcode/` directory of the branch, with the VC4 toolchain:

```
make CROSS_COMPILE=…/vc4-elf- MONITOR=1 CHAIN_IMAGE=path/to/broadcom-bootcode.bin
```

On `233c8c5`/`selector-halt` the include paths are older. Add
`INCLUDES="-I. -isystem .. -isystem ../vc4_include"` when building in a
current checkout. The result is `build/bootcode.bin`, about 61 KB; the
Boot ROM loads up to 128 KB.

## Limits

- Pi 3B+ tested. The Zero 2 W ran our firmware but not the selector.
- The monitor is open to anyone on the serial pins. Use the selector only
  on lab Pis.
- Waking from the halt with GPIO3 has not been tried on hardware yet.
- Chained from another monitor (`rbc-load.sh … chainmon`), the selector's Enter path hangs. Its SDRAM set-up cannot run over an SDRAM that is already up, which later firmware handles. A cold start is not affected.
