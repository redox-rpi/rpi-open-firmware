# redox-rpi experiment: open VPU firmware on the Pi 3B+

This directory holds what the [redox-rpi](https://github.com/redox-rpi/redox-rpi)
project added to Kristina Brooks's
[rpi-open-firmware](https://github.com/christinaa/rpi-open-firmware) to find
out how far an open-source `bootcode.bin` gets on a **Raspberry Pi 3B+**
(BCM2837B0), and what is missing before it could start Redox OS instead of
the closed Broadcom firmware. The upstream code outside this directory is
unchanged apart from the chainloader fixes listed in the top-level [README](../README.md), which also has the results.

| File | What it does |
| --- | --- |
| `build-toolchain.sh` | Builds the VC4 toolchain (binutils/GCC 6.2.1/newlib from [itszor/vc4-toolchain](https://github.com/itszor/vc4-toolchain), pinned) and an `arm-none-eabi` GCC 13.3 for the chainloader, as a normal user, into `../toolchains/prefix` |
| `toolchain.sha256` | Checksums of the GNU source tarballs the script downloads |
| `build.sh` | Runs upstream `buildall.sh` with those toolchains and builds the test payload |
| `payload/` | A 32-bit test program the chainloader starts as `zImage`; it prints the CPU mode and IDs on the UART |
| `make-dtb.sh` | Makes `redox-3bp.dtb` for the bootcode monitor's `boot64`: Broadcom's 3B+ devicetree with redox-rpi's overlays (`disable-bt`, `mmc`) applied, since our VPU code does not apply overlays |
| `make-sdcard.sh` | Makes a small SD card image (FAT16: `bootcode.bin`, `rpi.dtb`, `cmdline.txt`, `zImage`) without root |
| `build-lk-overlay.sh` | Builds [librerpi/lk-overlay](https://github.com/librerpi/lk-overlay) (pinned `50c95c1`) without Nix: the 64-bit ARM stage, VPU stage 2 and stage 1 (`bootcode.bin`) |
| `payload64/` | A 64-bit test program, started by lk-overlay's ARM loader as `Image-aarch64`; it prints EL, MIDR, MPIDR, x0, SCTLR and CNTFRQ |
| `make-sdcard-lk.sh` | Card image in lk-overlay's layout: FAT with `bootcode.bin`, ext4 with `/boot/lk.elf`, `Image-aarch64`, `rpi3.dtb`, `cmdline.txt` |

## Build

On an x86_64 Linux host with gcc, g++, make, flex, bison, texinfo, curl,
git, mtools and sfdisk (tested on Ubuntu 26.04, host GCC 15):

```bash
git clone https://github.com/redox-rpi/rpi-open-firmware.git
cd rpi-open-firmware
redox-rpi/build-toolchain.sh          # once
redox-rpi/build.sh
redox-rpi/make-sdcard.sh bcm2710-rpi-3-b-plus.dtb openfw-3bp.img
```

The devicetree comes from the
[Raspberry Pi firmware repository](https://github.com/raspberrypi/firmware/tree/master/boot).
