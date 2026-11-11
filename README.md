# Open firmware for the Raspberry Pi

Part of [redox-rpi](https://github.com/redox-rpi/redox-rpi), the port of
Redox OS to the Raspberry Pi. The goal: replace **everything closed that
comes from Broadcom** on the Pi, as far as that is possible, with open
code. That means:

- the boot firmware on the VPU: `bootcode.bin`, `start*.elf` with
  `fixup*.dat`;
- the ARM start stub built into it;
- the jobs `start.elf` keeps doing while the OS runs (mailbox, display);
- the firmware blobs other chips need, such as Wi-Fi and Bluetooth;
- on newer boards, the EEPROM bootloader.

Only the Boot ROM inside the chip stays. We start with the Pi 3B+
(BCM2837B0). Plan and progress:
[redox-rpi#230](https://github.com/redox-rpi/redox-rpi/issues/230). Every
closed piece, per model, and whether it can be replaced:
[docs/broadcom-firmware.md](docs/broadcom-firmware.md#every-closed-piece-and-whether-it-can-be-replaced).

The repository started as a fork of Kristina Brooks's
[rpi-open-firmware](https://github.com/christinaa/rpi-open-firmware) (2019),
and that code is still here. Our own firmware is a new, small C program
that takes its hardware sequences from it and names the source in every
file.

## State

| Step | What | State |
|---|---|---|
| M1 | Our `bootcode.bin`: VPU clock, UART, 1 GB SDRAM, memory test | passed on the Pi (#265) |
| Monitor | Test builds only: read/write registers, load code over serial, run it, chain another `bootcode.bin` | passed (#269) |
| Boot selector | The test Pi's card: Enter on the serial line opens our monitor, otherwise Broadcom's `bootcode.bin` (built in) boots as before | passed (#313, #320 rerun); in daily use on the test Pi |
| `armstub8` | Our build of the 64-bit ARM start stub | booted Redox on the Pi with Broadcom's firmware; not yet proven that it was ours that ran |
| M2 | Our VPU code starts the ARM in AArch64, then loads stub, U-Boot and devicetree from the SD card | ARM start written (PR #7), Pi test row 332 queued; SD/FAT not started |
| M3 | Mailbox service on the VPU (memory, MAC, clocks, temperature) | not started |
| M4 | HDMI and framebuffer | not started |
| M5 | Network boot, 3D | not started |

Today every image of redox-rpi still boots with Broadcom's firmware. How
each release starts, and what is still Broadcom's after each step:
[docs/boot-chain.md](docs/boot-chain.md).

## Documentation

All in [docs/](docs/README.md):

- [broadcom-firmware.md](docs/broadcom-firmware.md): what is in Broadcom's
  firmware (`bootcode.bin`, `start*.elf`, `armstub*` and their variants),
  per model: Zero W, Zero 2 W, 3B+, Pi 400.
- [boot-chain.md](docs/boot-chain.md): Broadcom's boot pieces, how each
  redox-rpi release starts, the steps to the end goal (with pictures).
- [own-firmware.md](docs/own-firmware.md): our firmware (#230): design,
  milestones, monitor and boot selector, findings.
- [test-results.md](docs/test-results.md): every Pi test of this
  repository in one table.
- [rpi-open-firmware-2019.md](docs/rpi-open-firmware-2019.md): the
  upstream firmware on our Pi, and the 3B+ risks.
- [lk-overlay.md](docs/lk-overlay.md): librerpi/lk-overlay on our Pi.
- [vpu-compiler.md](docs/vpu-compiler.md), [socdump.md](docs/socdump.md),
  [other-models.md](docs/other-models.md): ideas and later work.

## Branches and directories

| Where | What |
|---|---|
| `redox-rpi` (default branch) | upstream's firmware with our fixes, the docs, and `redox-rpi/` (toolchain and build scripts, lk-overlay build and patches, test payloads, card images) |
| `bootcode` | our own firmware in `bootcode/`; work branches are stacked on it (`arm-start`, `armstub8`) |
| `master` | upstream, unchanged |

## Build

```bash
git clone -b redox-rpi https://github.com/redox-rpi/rpi-open-firmware.git
cd rpi-open-firmware
redox-rpi/build-toolchain.sh   # VC4 GCC 6.2.1 and ARM GCC, once, about 40 minutes, no root
```

Our firmware (branch `bootcode`): `cd bootcode && make`, or
`make MONITOR=1` for a test build with the serial monitor
([bootcode/README.md](https://github.com/redox-rpi/rpi-open-firmware/blob/bootcode/bootcode/README.md)).
Upstream's firmware: `redox-rpi/build.sh` ([redox-rpi/README.md](redox-rpi/README.md)).
Serial console on GPIO 14/15, 115200 8N1.

## Licence and sources

GPLv2 or later, as upstream. Broadcom's register headers are BSD
(3-clause). See [LICENSE](LICENSE). Every file of our firmware names the
code it is taken from. Facts from Broadcom's documents, Linux and an
internal disassembly of the closed firmware (behaviour only, no code
copied) are listed with their source; the disassembly itself is not
published.

Upstream's original README, with its credits, is kept in
[docs/upstream-README.md](docs/upstream-README.md).
