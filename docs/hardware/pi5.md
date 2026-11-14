# Raspberry Pi 5 and Compute Module 5

The Pi 5 and the CM5 share the BCM2712 SoC and the RP1 I/O chip. Nothing
has run on either in this project yet, so this page is from Raspberry
Pi's public documents (the Pi 5 product brief, the
[RP1 peripherals](https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf)
document, the CM5 datasheet) and the Linux devicetree; nothing here is
measured. The maintainer has a CM5 on the CM5 IO board. Overview of all
models: [models.md](../models.md).

## Board

| | Pi 5 | CM5 (on the CM5 IO board) |
|---|---|---|
| SoC | BCM2712 (16 nm) | BCM2712 |
| ARM | 4 × Cortex-A76 (ARMv8.2-A), 2.4 GHz; 512 KB L2 per core, 2 MB shared L3 | same |
| VPU / GPU | VideoCore VII, V3D 7.1 | same |
| RAM | 2, 4, 8 or 16 GB LPDDR4X-4267 | 2, 4, 8 or 16 GB |
| Storage | microSD (SDR104), PCIe 2.0 x1 connector for NVMe | optional eMMC on the module; microSD on the IO board for the Lite version; M.2 M-key slot on the IO board |
| I/O chip | RP1, on PCIe 2.0 x4 | RP1 on the module |
| USB | 2 × USB 3, 2 × USB 2 (in RP1) | 2 × USB 3 on the IO board |
| Ethernet | gigabit, MAC in RP1, BCM54213 PHY | gigabit on the module |
| Wi-Fi / Bluetooth | CYW43455, 2.4 and 5 GHz, Bluetooth 5.0 | on the Wi-Fi variants |
| Display | 2 × micro-HDMI, 4Kp60 | 2 × HDMI (full size) on the IO board |
| Camera / display | 2 × 4-lane MIPI connectors (each CSI-2 or DSI) | on the IO board |
| Power chip | Renesas DA9091 ("Gilmour"), with an RTC | on the module |
| Extra | power button, RTC battery connector, fan connector, 3-pin debug UART | power button, fan connector on the IO board |
| Crystal | 54 MHz | 54 MHz |
| Supply | 5 V, 5 A over USB-C (USB PD) | USB-C to the IO board |

## Two chips

- **BCM2712** holds the CPU, memory controller, VideoCore VII, HDMI, the
  SD card controller, the HEVC decoder, PCIe, and its own debug UART.
- **RP1** is Raspberry Pi's own chip. It holds almost all the I/O: USB 3
  (two xHCI controllers), Ethernet MAC, GPIO and the 40-pin header's
  UARTs, SPI, I2C, I2S and PWM, MIPI CSI-2 and DSI, DPI, the composite
  video output and an ADC. It sits behind PCIe 2.0 x4, so nothing on the
  header works until the PCIe link and RP1 are set up.

## Address maps

- The BCM2712 uses 40-bit physical addresses; its peripherals sit far
  above the RAM (around `0x10_0000_0000` and up), at addresses the
  devicetree gives. There is no `0x3F…`/`0xFE…` style fixed base.
- RP1's registers are reached through a PCIe BAR. Inside RP1, each
  peripheral has its own offset (RP1 peripherals document); the
  devicetree maps them under the `rp1` node.

## Boot

1. **Boot ROM**, then the **EEPROM bootloader**. On the Pi 5 the EEPROM
   holds the main firmware too: there is no `start*.elf` or `fixup*.dat`
   on the card, only `config.txt`, the devicetree, overlays and the
   kernel.
2. Boot sources: SD card, USB mass storage, NVMe, network (TFTP), and
   HTTP boot; the CM5 also boots from eMMC. The order is in the EEPROM
   (`BOOT_ORDER`, `rpi-eeprom-config`).
3. The firmware contains **TF-A's BL31** at EL3, so the kernel gets
   **PSCI** (start and stop cores, reset, power off). No spin table.
4. The CM5's eMMC is flashed over USB with `rpiboot`, with the IO board's
   boot jumper (nRPIBOOT) set.

## ARM

- **Interrupts:** GIC-400 (GICv2), as on the Pi 4.
- **Timer:** the generic timer at 54 MHz.
- **Starting cores:** PSCI from BL31.
- Cortex-A76 has more ARMv8.2 features than the A53/A72 (for example
  the LSE atomic instructions).

## Serial console

- The **3-pin debug UART** connector (JST-SH, between the HDMI ports on
  the Pi 5) is a UART in the BCM2712 itself, so it works without RP1. In
  Linux it is `ttyAMA10`. 3.3 V, 115200 8N1. Raspberry Pi sells a debug
  probe for it.
- GPIO 14/15 on the header are a PL011 in **RP1**, reachable only after
  the PCIe link and RP1 are up.
- `BOOT_UART=1` in the EEPROM settings makes the bootloader log on the
  debug UART.

## Media

The Pi 5 has a hardware **HEVC** decoder only; H.264 and the other codecs
of the VideoCore IV are gone (software decoding on the A76). The ISP for
the cameras is the new "PiSP" (back end in the BCM2712, front end in RP1).

## Close relatives

- **CM5 vs Pi 5:** same SoC and RP1, different board: optional eMMC, a
  board-to-board connector, IO-board-specific peripherals. Devicetree:
  `bcm2712-rpi-cm5-cm5io.dtb` (CM5 on the IO board); Pi 5:
  `bcm2712-rpi-5-b.dtb`.
- **Pi 500:** a Pi 5 in a keyboard, as the Pi 400 is to the Pi 4.
- Later BCM2712 revisions (D0) drop parts of the chip that the Pi 5 does
  not use; the devicetree name changes (`bcm2712d0-…`).
