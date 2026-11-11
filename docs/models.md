# Pi models: hardware

The Raspberry Pi models this project looks at, and what is on each board.
Which firmware each one boots with: [broadcom-firmware.md](broadcom-firmware.md).
Later models (Pi 4, Pi 5): [other-models.md](other-models.md).

| | Pi Zero / Zero W | Pi Zero 2 W | Pi 3B+ | Pi 400 |
|---|---|---|---|---|
| SoC | BCM2835 | RP3A0 (BCM2710A1 and 512 MB in one package) | BCM2837B0 | BCM2711C0 |
| ARM cores | 1 × ARM1176, 32-bit (ARMv6) | 4 × Cortex-A53, 64-bit, 1 GHz | 4 × Cortex-A53, 64-bit, 1.4 GHz | 4 × Cortex-A72, 64-bit, 1.8 GHz |
| VPU / GPU | VideoCore IV | VideoCore IV | VideoCore IV | VideoCore VI |
| RAM | 512 MB | 512 MB | 1 GB LPDDR2 | 4 GB LPDDR4 |
| Crystal | 19.2 MHz | 19.2 MHz | 19.2 MHz | 54 MHz |
| Peripherals (ARM address) | `0x20000000` | `0x3F000000` | `0x3F000000` | `0xFE000000` (low peripheral mode) |
| Interrupts | BCM2835 interrupt controller | BCM2836 per-core block + BCM2835 controller | BCM2836 per-core block + BCM2835 controller | GIC-400 |
| Boot media | SD card, USB device mode | SD card, USB device mode | SD card, USB, network (in the Boot ROM) | SD card, USB, network (EEPROM bootloader) |
| SD controllers | SDHOST and Arasan (EMMC) | SDHOST and Arasan | SDHOST and Arasan | EMMC2 |
| USB | one OTG port (DWC2) | one OTG port (DWC2) | DWC2 behind a LAN7515 hub, 4 ports | VL805 over PCIe: 2 × USB 3, 1 × USB 2 |
| Ethernet | none | none | LAN7800 in the LAN7515, gigabit over USB 2 (about 300 Mbit/s) | BCM54213 PHY with the SoC's GENET MAC, gigabit |
| Wi-Fi / Bluetooth | Zero W: CYW43438 | Cypress/Infineon CYW4343x family | CYW43455 (2.4 and 5 GHz) | CYW43456 (2.4 and 5 GHz) |
| Display | mini-HDMI | mini-HDMI | HDMI | 2 × micro-HDMI |
| Power chip | none (discrete regulators) | none (discrete regulators) | MaxLinear MxL7704 | not checked |
| Keyboard | – | – | – | built in, through its own USB controller |
| In redox-rpi | not supported: 32-bit ARM, Redox needs 64-bit | supported | supported; the test Pi | bring-up (Pi test row 325) |

## Our test hardware

- **Pi 3B+:** the test Pi, on the build host homenodellm's network, with
  a serial console (USB adapter, `ttyUSB0`), a switchable power plug, an
  HDMI grabber, and the boot selector card
  ([own-firmware.md](own-firmware.md#boot-selector-the-test-pis-card)).
- **Pi Zero 2 W:** USB Ethernet HATs with an RTL8152, network boot.
- **Pi 400:** serial console on `ttyUSB1`, network boot from its EEPROM.

Notes:

- The Zero and Zero W have a 32-bit ARMv6 core. Our firmware work could
  still run on them (same VPU as the 3B+), but redox-rpi cannot.
- "Supported" means redox-rpi images boot on it today, all with
  Broadcom's firmware.
