# Upstream's original firmware (2016–2019)

This directory holds the code of Kristina Brooks's
[rpi-open-firmware](https://github.com/christinaa/rpi-open-firmware), the
project this repository was forked from. Until 2026-11-17 it sat at the top
of the repository.

**It is not our firmware and it is not maintained.** Our firmware is in
`bootcode/` (VPU) and `armstub/` (ARM start stub). Some of our files are
derived from this code and name the file they took from, for example
`bootcode/sdram.c` from `sdram.c` and `bootcode/clock.c` from
`romstage.c`. Those references point here.

| File | What it is |
|---|---|
| `start.s`, `romstage.c`, `sdram.c`, `trap.c`, `linker.lds` | VPU first stage: PLLC, UART, SDRAM, exceptions |
| `BCM2708PlatformStartup.cc`, `drivers/`, `lib/` | VPU drivers and runtime (C++), ARM start |
| `arm_chainloader/`, `arm_monitor.c` | 32-bit ARM chainloader that loads `zImage` from the SD card |
| `Makefile`, `buildall.sh` | Build of the above; `../redox-rpi/build.sh` runs it with our toolchains |
| `tools/wslstage.py`, `style.sh` | Upstream's helper scripts |

Broadcom's register headers (`../hardware.h`, `../broadcom/`,
`../vc4_include/`) stay at the top, because `bootcode/` uses them too. The
Makefiles here look for them one level up. Licence and credits:
[`../LICENSE`](../LICENSE), [`../AUTHORS.md`](../AUTHORS.md), and
upstream's README in [`../docs/upstream-README.md`](../docs/upstream-README.md).
