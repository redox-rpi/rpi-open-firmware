# Open firmware for the Raspberry Pi

An independent sub-project of [redox-rpi](https://github.com/redox-rpi/redox-rpi),
the port of Redox OS to the Raspberry Pi. The goal: replace **everything
closed that comes from Broadcom** on the Pi, as far as that is possible,
with open code. That means:

- the boot firmware on the VPU: `bootcode.bin`, `start*.elf` with
  `fixup*.dat`;
- the ARM start stub built into it;
- the jobs `start.elf` keeps doing while the OS runs (mailbox, display);
- the firmware blobs other chips need, such as Wi-Fi and Bluetooth;
- on newer boards, the EEPROM bootloader.

Only the Boot ROM inside the chip stays. The models we look at (Zero W,
Zero 2 W, 3B+, Pi 400, Pi 5) are described in [Models](https://github.com/redox-rpi/rpi-open-firmware/wiki/Models) in the wiki;
work starts on the Pi 3B+. Plan and progress:
[#18](https://github.com/redox-rpi/rpi-open-firmware/issues/18). Every
closed piece, per model, and whether it can be replaced:
[Broadcom](https://github.com/redox-rpi/rpi-open-firmware/wiki/Broadcom#every-closed-piece-and-whether-it-can-be-replaced) in the wiki.

The firmware is not tied to Redox: it starts U-Boot, Linux or Redox the
same way, so it has its own docs, Pi tests and pace. What redox-rpi uses
from it, and how, is on redox-rpi's
[open firmware page](https://github.com/redox-rpi/redox-rpi/blob/main/docs/topics/open-firmware.md).

## Where it comes from

GitHub shows this repository as a fork of Kristina Brooks's
[rpi-open-firmware](https://github.com/christinaa/rpi-open-firmware), because
it started there. That project has been on hold since 2019 (its
[issue #37](https://github.com/christinaa/rpi-open-firmware/issues/37)).
Our scope is much wider than upstream's, and we take from several sources.
Every file names the ones it uses:

| Source | Licence | What we take |
|---|---|---|
| [christinaa/rpi-open-firmware](https://github.com/christinaa/rpi-open-firmware) (2019) | GPLv2+ | PLL, SDRAM, SDHOST and ARM start sequences; its 2016 code is in [`upstream/`](upstream/), with our fixes |
| [librerpi/lk-overlay](https://github.com/librerpi/lk-overlay), the newer rewrite by the same community | GPLv2+ | tested on our Pi ([lk-overlay](https://github.com/redox-rpi/rpi-open-firmware/wiki/lk-overlay)); reference for later steps, no code taken yet |
| [librerpi/rpi-open-firmware](https://github.com/librerpi/rpi-open-firmware), the 2019 firmware carried on to 2025 (375 commits), with about 100 hardware notes | GPLv2+ | first reference for the mailbox, HDMI and the Pi 400; all outside sources and their status in [Sources](https://github.com/redox-rpi/rpi-open-firmware/wiki/Sources) |
| Broadcom's register headers (`hardware.h`, `sdhost.h`, …) | BSD 3-clause | register names and bits |
| [raspberrypi/tools `armstub8.S`](https://github.com/raspberrypi/tools/tree/master/armstubs) | BSD 3-clause | what the stub must do (ours is new code) |
| Linux drivers and devicetrees | GPLv2 | register behaviour, the devicetree we load |
| Specifications: Microsoft FAT (fatgen103), SD (via NetBSD `sdmmcreg.h`, ISC), Devicetree v0.4, Arm Architecture Reference Manual | – | `fat.c`, `sd.c`, `fdt.c`, `armstub8` |
| Herman Hermitage's VideoCore IV notes, Julian Brown's VC4 GCC | – | the VPU itself, the compiler |
| An internal disassembly of Broadcom's firmware | – | behaviour notes only, no code; the disassembly is not published |

Our own firmware (`bootcode/`) is new, small C code. Upstream's original
README, with its credits, is in [docs/upstream-README.md](docs/upstream-README.md).

## State

| Step | What | State |
|---|---|---|
| M1 | Our `bootcode.bin`: VPU clock, UART, 1 GB SDRAM, memory test | passed on the Pi (#265) |
| Monitor | Test builds only: read/write registers, load code over serial, run it, chain another `bootcode.bin` | passed (#269) |
| Boot selector | The test Pi's card: Enter on the serial line opens our monitor, otherwise Broadcom's `bootcode.bin` (built in) boots as before | passed (#313, #320 rerun); in daily use on the test Pi |
| `armstub8` | Our build of the 64-bit ARM start stub | booted Redox on the Pi with Broadcom's firmware; not yet proven that it was ours that ran |
| M2 | Our VPU code starts the ARM in AArch64, then loads stub, kernel and devicetree from the SD card | ARM start (PR #7), SD card and FAT (PR #8), `boot64` (PR #9): written and host-tested, Pi test row 332 queued |
| M3 | Mailbox service on the VPU (memory, MAC, clocks, temperature) | not started |
| M4 | HDMI and framebuffer | not started |
| M5 | Network boot, 3D | not started |

Today every image of redox-rpi still boots with Broadcom's firmware. How
each release starts, and what is still Broadcom's after each step:
[Boot chain](https://github.com/redox-rpi/rpi-open-firmware/wiki/Boot-chain).

## Documentation

The pages for reading are in the [wiki](https://github.com/redox-rpi/rpi-open-firmware/wiki):

- [Models](https://github.com/redox-rpi/rpi-open-firmware/wiki/Models): the Pi models and their hardware.
- [Broadcom](https://github.com/redox-rpi/rpi-open-firmware/wiki/Broadcom): Broadcom's firmware per model (Zero W,
  Zero 2 W, 3B+, Pi 400, Pi 5), every closed piece, and one page per file
  (`bootcode.bin`, `start*.elf`, `fixup*.dat`, `armstub8`, devicetree,
  overlays, licence) with what is inside it.
- [Boot chain](https://github.com/redox-rpi/rpi-open-firmware/wiki/Boot-chain): Broadcom's boot pieces, how each redox-rpi
  release starts, the steps to the end goal (with pictures).
- [Own firmware](https://github.com/redox-rpi/rpi-open-firmware/wiki/Own-firmware): our firmware (#18): design,
  milestones, monitor and boot selector, findings.
- [rpi-open-firmware (2019)](https://github.com/redox-rpi/rpi-open-firmware/wiki/rpi-open-firmware-2019): the upstream
  firmware on our Pi, and the 3B+ risks.
- [lk-overlay](https://github.com/redox-rpi/rpi-open-firmware/wiki/lk-overlay): librerpi/lk-overlay on our Pi.
- [Sources](https://github.com/redox-rpi/rpi-open-firmware/wiki/Sources): every outside source (librerpi, glennra,
  toolchain, simulator), its last change, and whether we watch it.
- [VPU compiler](https://github.com/redox-rpi/rpi-open-firmware/wiki/VPU-compiler), [socdump](https://github.com/redox-rpi/rpi-open-firmware/wiki/Socdump): ideas and later work.
- [Hardware](https://github.com/redox-rpi/redox-rpi/wiki) (redox-rpi wiki):
  one page per model (3B+, Pi 400, Pi 5/CM5).

Pi test results: redox-rpi/pi-tests [queue](https://github.com/redox-rpi/pi-tests/blob/main/queue.md#done)
and [reports](https://github.com/redox-rpi/pi-tests/tree/main/reports).
Pages that change with the code stay in the repository: the READMEs in
`bootcode/`, `armstub/` and `redox-rpi/` (build, flash, test), and
[docs/upstream-README.md](docs/upstream-README.md).

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
