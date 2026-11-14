# Other sources

Every outside project this repository uses or reads, in one list. Each
project's own repository explains it; this page only says what it is
for us and whether it is still moving. Status checked 2026-11-14 (last
push on GitHub).

**Watch:**

- **yes**: active and close to our work. Look at new commits about once a
  week.
- **release**: we pin a version. Look when a new release comes out.
- **no**: no change for a year or more, or only of historical interest.
  Look again only when a question needs it.

A source moves to **no** after 12 months without a change, and back to
**yes** when it moves again.

## Raspberry Pi

| Source | For us | Last change | Watch |
|---|---|---|---|
| [raspberrypi/firmware](https://github.com/raspberrypi/firmware) | Broadcom's closed files on our cards ([broadcom/](broadcom/README.md)); pinned `1.20260915` | 2026-11-12 | release |
| [raspberrypi/rpi-eeprom](https://github.com/raspberrypi/rpi-eeprom) | EEPROM bootloader of the Pi 400 and Pi 5 | 2026-11-10 | release |
| [raspberrypi/linux](https://github.com/raspberrypi/linux) | devicetrees and overlays, Linux drivers as reference for registers | 2026-11-13 | release (with the firmware) |
| [raspberrypi/arm-trusted-firmware](https://github.com/raspberrypi/arm-trusted-firmware) | TF-A with PSCI for the Pi 3, 4 and 5; BL31 inside the Pi 5 firmware | 2026-09-29 | yes |
| [raspberrypi/tools](https://github.com/raspberrypi/tools) | `armstubs/` (source of `armstub8`) | 2022-07-11 | no |

## Open firmware

| Source | For us | Last change | Watch |
|---|---|---|---|
| [glennra/lk-overlay](https://github.com/glennra/lk-overlay/tree/feature/rpi3-dsi-linux) (`feature/rpi3-dsi-linux`) and [glennra/rpi-open-firmware](https://github.com/glennra/rpi-open-firmware/tree/feature/arm-handoff-review) (`feature/arm-handoff-review`) | Glenn Ramsey: Pi 3B boots Linux without Broadcom firmware, DSI display, ARM hand-off; same goal as our M2/M4 | 2026-11-10 | yes |
| [librerpi/lk-overlay](https://github.com/librerpi/lk-overlay) | VPU firmware on LK; tested on our Pi ([lk-overlay.md](lk-overlay.md)) | 2026-08-20 | yes |
| [librerpi/lk](https://github.com/librerpi/lk) | their LK fork with the VC4 port, under lk-overlay | 2026-06-18 | yes |
| [librerpi/rpi-tools](https://github.com/librerpi/rpi-tools) | Linux-side register tools (`pll-inspector`, `pv_dumper`, EEPROM tools); idea for `socdump` | 2026-06-25 | yes |
| [librerpi/rpi-open-firmware](https://github.com/librerpi/rpi-open-firmware) | the continued 2019 firmware, and about 100 hardware notes in `docs/` (mailbox, SDRAM, HDMI, HVS, PMIC, OTP, VL805, …): read first before a milestone | 2025-08-24 | no |
| [christinaa/rpi-open-firmware](https://github.com/christinaa/rpi-open-firmware) | the 2019 original this repository forks ([upstream-README.md](upstream-README.md), [rpi-open-firmware-2019.md](rpi-open-firmware-2019.md)) | 2022-03-25 | no |
| [librerpi/pj](https://github.com/librerpi/pj) | a JTAG hat for the Pi | 2024-11-15 | no |
| [librerpi/rp1-lk](https://github.com/librerpi/rp1-lk), [librerpi/rp1-hacking](https://github.com/librerpi/rp1-hacking) | own code on the Pi 5's RP1 chip | 2024-01 | no |
| [librerpi/u-boot](https://github.com/librerpi/u-boot) | their U-Boot fork | 2022-09-24 | no |
| [librerpi/rpi-bootd](https://github.com/librerpi/rpi-bootd) | a boot server (USB, UART) | 2020-03-20 | no |
| [librerpi/vpu-x265](https://github.com/librerpi/vpu-x265) | example of VPU vector code | 2020-07-26 | no |
| [doriangc/lk-overlay](https://github.com/doriangc/lk-overlay) | an unfinished HDMI attempt on lk-overlay | 2023-07-07 | no |
| [hramrach/rpi-open-firmware](https://github.com/hramrach/rpi-open-firmware) | small JTAG and OTP notes | 2021-05-17 | no |

## VPU toolchain and simulator

Our own forks: [redox-rpi/tinycc](https://github.com/redox-rpi/tinycc),
[redox-rpi/resim](https://github.com/redox-rpi/resim) (its README lists
where each change comes from), [redox-rpi/vc4-toolchain](https://github.com/redox-rpi/vc4-toolchain).

| Source | For us | Last change | Watch |
|---|---|---|---|
| [itszor/vc4-toolchain](https://github.com/itszor/vc4-toolchain) | build scripts for VC4 GCC 6.2.1, binutils, newlib: what we build the firmware with | 2021-07-02 | no |
| [itszor/gcc-vc4](https://github.com/itszor/gcc-vc4) | the GCC port itself | 2023-01-03 | no |
| [itszor/binutils-vc4](https://github.com/itszor/binutils-vc4), [itszor/newlib-vc4](https://github.com/itszor/newlib-vc4) | assembler, linker, C library | 2017 | no |
| [mgottschlag/resim](https://github.com/mgottschlag/resim) | the original VPU simulator (MIT) | 2020-03-29 | no |
| [itszor/resim](https://github.com/itszor/resim), [cleverca22/resim](https://github.com/cleverca22/resim) | resim forks with more instructions; parts taken into ours | 2023-01, 2020-02 | no |
| [hermanhermitage/videocoreiv](https://github.com/hermanhermitage/videocoreiv) | the VPU instruction set, reverse engineered | 2019-02-20 | no |
| [hiro-0317/llvm-videocore](https://github.com/hiro-0317/llvm-videocore), [LEA0317/LLVM-VideoCore4](https://github.com/LEA0317/LLVM-VideoCore4) | an LLVM backend for the VPU ([vpu-compiler.md](vpu-compiler.md)) | 2024-11, 2023-10 | no |
| [phire/llvm](https://github.com/phire/llvm) | an older LLVM attempt | 2015-02-03 | no |
| [maazl/vc4asm](https://github.com/maazl/vc4asm) | QPU (3D unit) assembler | 2024-07-08 | no |
| [thubble/videocore-elf-dis](https://github.com/thubble/videocore-elf-dis) | VPU disassembler | 2012-11-27 | no |

## How to recheck

```bash
gh api repos/<owner>/<repo> --jq .pushed_at
```

for each row; for glennra also the branches (`gh api
repos/glennra/lk-overlay/branches`). Update the dates here and move rows
between **yes** and **no** by the rule above.
