# rpi-open-firmware: redox-rpi experiment

This fork belongs to [redox-rpi](https://github.com/redox-rpi/redox-rpi),
a port of Redox OS to the Raspberry Pi 3B+. We use it to test how far
Kristina Brooks's open-source VPU firmware
([christinaa/rpi-open-firmware](https://github.com/christinaa/rpi-open-firmware))
gets on our Pi 3B+, and what is missing before it could start Redox
instead of Broadcom's closed firmware. The license stays upstream's: GPLv2+,
with BSD-licensed Broadcom headers (see [LICENSE](LICENSE)). Our changes are
on the `redox-rpi` branch; `master` is upstream unchanged.

## Results on the Pi 3B+ (BCM2837B0)

| Test | Date | Result |
| --- | --- | --- |
| [#224](https://github.com/redox-rpi/redox-rpi/blob/main/docs/test-plan.md) | 2026-11-05 | **The VPU firmware works:** serial output, 1 GB LPDDR2 set up and tested, USB power and PHY on, ARM clock set, ARM core 0 runs the 32-bit chainloader. The chainloader finds the SD card (64 MB, SDSC), then one read fails at the data clock, and reading the MBR crashes on a trap that GCC 13 inserts where the old code is missing a `return`. |
| #249 | 2026-11-05 | **Works up to the kernel hand-off** (same 64 MB card, both chainloader bugs fixed). The first SD read failed at `SH_CDIV 0x8` and worked at half the clock (`0x12`). MBR, `cmdline.txt`, `rpi.dtb` and `zImage` were read, and the test payload ran: 32-bit Supervisor mode, MIDR `0x410FD034` (Cortex-A53), core 0. A 32 GB SDHC card worked at the full data clock, with no retry, so only the old card needs the slower clock. |
| #251 | queued | Step 2: [librerpi/lk-overlay](https://github.com/librerpi/lk-overlay) built without Nix (`redox-rpi/build-lk-overlay.sh`). Does the VPU start the ARM in 64-bit mode and run a 64-bit test program? |

What it does not do yet, and what Redox would need (64-bit ARM start,
spin table for cores 1–3, loading U-Boot or `kernel8.img`, a replacement
for the firmware mailbox, USB/Ethernet): see the project's
[topic page](https://github.com/redox-rpi/redox-rpi/blob/main/docs/topics/open-firmware.md).

## Our changes

- [`redox-rpi/`](redox-rpi/README.md): toolchain build script (no root),
  build script, test payload, SD card image script.
- `arm_chainloader`: missing `return`s added, SD read errors reported, and
  retried with half the clock; the card size in the log no longer
  overflows for cards of 4 GB and more (not yet seen on the Pi).

## Build

```bash
git clone -b redox-rpi https://github.com/redox-rpi/rpi-open-firmware.git
cd rpi-open-firmware
redox-rpi/build-toolchain.sh          # once, about 40 minutes
redox-rpi/build.sh
redox-rpi/make-sdcard.sh bcm2710-rpi-3-b-plus.dtb openfw-3bp.img
```

Details in [redox-rpi/README.md](redox-rpi/README.md). Below is upstream's README.

---

# Minimal Raspberry Pi VPU firmware

`rpi-open-firmware` is a small firmware for the [RPi VPU](https://en.wikipedia.org/wiki/VideoCore), capable of initializing UART, VPU PLL (PLLC), and ARM itself. It's intended as a libre replacement for the stock `bootcode.bin` normally loaded from the SD card. A UART cable is highly recommended for the time being, though not strictly necessary. Additionally, there is a second-stage chainloader running on ARM capable of initializing eMMC, FAT, and the Linux kernel.

See [issue #31](https://github.com/christinaa/rpi-open-firmware/issues/31) for a compatibility table and related discourse.

Technical discussion occurs in `#raspberrypi-internals` on [Freenode](http://webchat.freenode.net/?channels=#raspberrypi-internals). Please come on IRC before randomly hacking on the codebase :-)

All Broadcom headers are licensed under 3-Clause BSD License while our code is under GPLv2+. See `LICENSE` for more information. Various external projects are mirrored under a mix of GPL-compatible licenses.

## Is this project dead or abandoned?

It's not dead and not abandoned in a conventional sense. 

However, it is on an indefinite hold. I wrote a detailed explanation [in issue #37](https://github.com/christinaa/rpi-open-firmware/issues/37) of its current situation. 

## Building

As a prerequisite, Julian Brown's [VC4 toolchain](https://github.com/itszor/vc4-toolchain) is necessary as well as the `arm-none-eabi-` toolchain (Debian package `gcc-arm-none-eabi`). You can tweak the VC4 toolchain path in `CROSS_COMPILE` in `Makefile` and the ARM path in `arm_chainloader/Makefile` if necessary. Contributors should not commit their personal paths. After configuration, run `buildall.sh`. The binary is at `build/bootcode.bin`, ready to be copied to an SD card.

### Building on macOS

macOS compilation is similar to GNU/Linux, save platform errata described here. Instructions to build the ARM toolchain are [here](https://launchpadlibrarian.net/287100910/How-to-build-toolchain.pdf). Due to symlinking by default, GCC must be installed manually, and, an older version of guile is necessary (homebrew packages `gcc-6` and `guile18`, respectively). Finally, set the environment variable `LIBRARY_PATH` to `/lib:/lib64` when running `buildall.sh. 

## Technical Details
The firmware is split into two parts, one running on the VC4 and the other on ARM. The VC4 part initializes PLLC and moves VPU over to it, and then brings up UART. It performs SDRAM initialization, mapping it to  `0xC0000000` (uncached alias). Next, ARM is initialized, and the embedded bootloader is mapped to ARM address `0x0`. `arm_chainloader` is then executed.

The ARM chainloader initializes the eMMC controller and accesses the FAT boot partition. From here, it chainloads the Linux kernel (other payloads likely do not work due to mailbox usage).

## Does it boot Linux?

Yes, with some conditions. You can boot a very minimal version of Linux without the firmware and get it to work with UART and eMMC. Support for USB, DMA, and Ethernet are in the works, which will be sufficient for certain headless systems. Still, many other peripherals require bringup, such as video. Additionally, drivers for power management need to written.

## Thanks To
 * **[Herman Hermitage](https://github.com/hermanhermitage)** for his VC4 documentation and for helping determine suitable ARM PLL configurations.
 * **[Julian Brown](https://github.com/itszor)** for reviewing the code and for his awesome VC4 toolchain.
 * **[Alyssa Rosenzweig](https://github.com/bobbybee)** for her contributions to the firmware especially in areas of Linux bringup and early ARM side initialization, as well as fixing mailbox support.
 * **[David Given](https://github.com/davidgiven)** for his initial LLVM project used as the base for the initial LLVM toolchain before moving to GCC.
 * **[Scott Mansell](https://github.com/phire)** for reviewing the code.
 * **[Broadcom](https://github.com/broadcom)** for their header release.
 * Various other people not mentioned here.
