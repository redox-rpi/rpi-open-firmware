# librerpi: everything they have, and what it is good for

[librerpi](https://github.com/librerpi) is the community that carried on
open Raspberry Pi firmware after Kristina Brooks's
[rpi-open-firmware](https://github.com/christinaa/rpi-open-firmware) stopped
in 2019. Its own fork of that firmware has grown much further than the 2019 code our
repository is based on. Survey of 2026-11-11; nothing taken over yet except
what [lk-overlay.md](lk-overlay.md) describes.

| Repository | Last change | What it is | Use for us |
|---|---|---|---|
| [lk-overlay](https://github.com/librerpi/lk-overlay) (GPLv2+) | 2026-08 | the newer VPU firmware on Travis Geiselbrecht's LK kernel; stage 1 and 2 | tested on our Pi (#253, #269), see [lk-overlay.md](lk-overlay.md); reference for M2–M4 |
| [lk](https://github.com/librerpi/lk) | 2026-06 | their fork of LK (MIT), with the VC4 port | under lk-overlay |
| [rpi-open-firmware](https://github.com/librerpi/rpi-open-firmware) (GPLv2+) | 2025-08 | **the continued 2019 firmware: 375 commits against 159 upstream.** New: OTP reading, a `start.elf`-style stage (`startelf.c`), first Pi 4 work (`startelf4.S`, `utils4.cc`), an `arm64` stub, netboot and NixOS builds, JTAG notes, a memory map | the better base for anything we take from "rpi-open-firmware"; compare before each milestone |
| the same, [`docs/`](https://github.com/librerpi/rpi-open-firmware/tree/master/docs) | 2025 | **about 100 hardware notes**, among them `mailbox.txt`, `sdram.txt`, `sdhost.txt`, `hdmi.md`, `hvs.md`, `pixelvalve.md`, `video-pipeline.md`, `pll-inspection.txt`, `otp.txt`, `pmic.txt`, `power-management.txt`, `vpu.txt`, `vpu-l1.txt`, `vpu-irq.txt`, `vpu-jtag.txt`, `rom.txt`, `rpi4-rom.txt`, `start4.txt`, `ddr4.txt`, `pcie.txt`, `vl805.txt`, `wifi.txt`, `bluetooth.txt`, `pwm_audio.txt`, `v3d.txt`, `qpu-compute.txt`, `bootcode_re.txt`, `boot-flow-and-secure-boot.md` | first place to read for M3 (mailbox), M4 (HDMI: HVS, pixel valves, PLLH), the Pi 400 (ROM, `start4`, DDR4, VL805) and audio |
| [rpi-tools](https://github.com/librerpi/rpi-tools) | 2026-06 | Linux-side tools: `pll-inspector`, `pv_dumper` (pixel valves), `map_peripherals`, `kms-test`, `ramdumper`, EEPROM and signing tools, `rom_sim.c` (Boot ROM simulator), EDID tools, a Pi 5 voltage script | overlaps with redox-rpi's `socdump` (#224); `rom_sim.c` and the EEPROM tools for the Pi 400 and Pi 5 |
| [rp1-lk](https://github.com/librerpi/rp1-lk), [rp1-hacking](https://github.com/librerpi/rp1-hacking) | 2024-01 | running own code on the Pi 5's **RP1** chip (LK port; hooking its firmware to start its second core) | the RP1 row of the Pi 5 in [broadcom-firmware.md](broadcom-firmware.md): open RP1 code is possible in principle |
| [pj](https://github.com/librerpi/pj) | 2024-11 | a JTAG hat for the Pi | debugging the VPU and ARM without the serial monitor |
| [rpi-bootd](https://github.com/librerpi/rpi-bootd) | 2020-03 | boot server: USB boot, kernels over UART, console, reset (in development) | ideas for our `rbc-load.sh` and the Pi test set-up |
| [u-boot](https://github.com/librerpi/u-boot) | 2022-09 | their U-Boot fork | compare if U-Boot fails on our firmware (mailbox) |
| [vpu-x265](https://github.com/librerpi/vpu-x265) | 2020-07 | an x265 DCT on the VPU | example of vector-unit code |

**Toolchain:** librerpi builds with Nix (`pkgs.pkgsCross.vc4`, plus
`arm-embedded` and `aarch64-embedded`). That is also a way to freeze our
toolchain ([vpu-compiler.md](vpu-compiler.md), step 1).

**What this changes:** our `bootcode/` names christinaa's 2019 code as its
source. Before M3 (mailbox) and M4 (HDMI) we read librerpi's notes and
code first, and name them as the source where we use them. A switch of
this repository's base from christinaa's to librerpi's fork is possible
(the same GPLv2+ licence) but not needed: our own firmware is new code.
