# Other Pi models (later)

The goal is Redox on the Pi 4, Pi 5 and newer models too, so anything we
build here keeps the chip-specific parts in their own tables:

| | 3B+ / 3B / Zero 2 W | Pi 4 | Pi 5 |
|---|---|---|---|
| SoC | BCM2837(B0) | BCM2711 | BCM2712 + RP1 (I/O over PCIe) |
| Peripherals (ARM address) | `0x3F000000` | `0xFE000000` (low peripheral mode) | from the devicetree |
| Crystal | 19.2 MHz | 54 MHz | 54 MHz |
| First boot code | `bootcode.bin` on the SD card | SPI EEPROM | SPI EEPROM |
| Open firmware | rpi-open-firmware, lk-overlay (tested here) | lk-overlay has some Pi 4 work (not checked) | none known |
| Starting cores, interrupts | spin-table, BCM2836 local mailboxes (our kernel patches) | GIC-400 (GICv2); PSCI only with TF-A as `armstub8.bin` | GIC-400; PSCI from the BL31 that the firmware already contains |

- `socdump`: the register addresses come from the devicetree and the
  tables are per chip, so the Pi 4 and Pi 5 are new tables, not a rewrite.
- [raspberrypi/arm-trusted-firmware](https://github.com/raspberrypi/arm-trusted-firmware)
  (TF-A, ports for the Pi 3, 4 and 5) runs at EL3 before the OS and gives
  **PSCI**: start and stop cores, reset, power off. Upstream Redox starts
  cores only through PSCI and routes interrupts only through a GICv2, which
  is why the 3B+ needs our spin-table and IPI kernel patches
  ([status.md](https://github.com/redox-rpi/redox-rpi/blob/main/docs/status.md)). On the 3B+ TF-A would add little (PSCI
  power-off and core hotplug; there's still no GIC). On the Pi 4 and 5 it
  matters a lot: with PSCI and the GIC-400, upstream Redox's own path fits,
  so these boards may need fewer Pi-specific patches than the 3B+. TF-A's
  BL31 could also take the "cores 1–3" step of the open firmware ([rpi-open-firmware-2019.md](rpi-open-firmware-2019.md#next-what-redox-would-need-on-top),
  step 1) instead of our own stub.
- Open firmware on the Pi 4/5 means replacing the EEPROM boot stage, not
  a file on the card. That's a separate experiment, done only with a way
  to restore the EEPROM.

