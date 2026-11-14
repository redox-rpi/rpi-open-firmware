# `overlays/mmc.dtbo`

A devicetree overlay (1,221 bytes, open source from the Raspberry Pi Linux
tree). `dtoverlay=mmc` in `config.txt` moves the SD card from the SDHOST
controller to the Arasan SDHCI controller.

[All Broadcom files](README.md)

## Background

The BCM2837 has two SD controllers:

| Controller | Address (bus) | Devicetree | Normally drives |
|---|---|---|---|
| SDHOST (Broadcom's own) | `0x7e202000` | `mmc@7e202000`, label `sdhost` | the SD card |
| Arasan SDHCI ("EMMC") | `0x7e300000` | `mmcnr@7e300000` (`mmcnr`) and `mmc@7e300000` (`mmc`) | the Wi-Fi chip over SDIO |

The SD card sits on GPIO 48–53; the Wi-Fi chip on GPIO 34–39. Each
controller can be routed to either group through the GPIO alternate
functions. `bootcode.bin` and `start*.elf` use SDHOST.

## What is inside

| Fragment | Target | Change |
|---|---|---|
| 0 | `mmc` (Arasan) | `pinctrl-0 = <mmc_pins>`, `bus-width = 4`, `brcm,overclock-50 = 0`, `status = "okay"` |
| 1 | `gpio` | new node `mmc_pins`: GPIO 48–53 (`0x30`–`0x35`), function 7 (ALT3), pull none on CLK and pull-up on CMD and DAT0–3 |
| 2 | `sdhost` | `status = "disabled"` |
| 3 | `mmcnr` | `status = "disabled"`: the Wi-Fi chip loses its controller |

Parameter: `overclock_50` sets `brcm,overclock-50` (clock in MHz for
high-speed cards).

## Why redox-rpi uses it

Redox's SD driver supports the Arasan controller (SDHCI), not SDHOST
(redox-rpi `config/config.txt`, `docs/architecture.md`). With this overlay
the card is on the Arasan controller, and Wi-Fi is off.
