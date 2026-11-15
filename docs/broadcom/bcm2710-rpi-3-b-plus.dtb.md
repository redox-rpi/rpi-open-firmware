# `bcm2710-rpi-3-b-plus.dtb` (and the 3B and Zero 2 W ones)

The devicetree: a binary description of the board's hardware, from the
open Raspberry Pi Linux tree (GPL/BSD), not closed code. `start*.elf`
picks the file for the board, applies the overlays from `config.txt`,
fills in a few values and hands it to the kernel (for redox-rpi: to
U-Boot, which passes it on to Redox).

[All Broadcom files](README.md)

## Facts

| File | Size | Board |
|---|---|---|
| `bcm2710-rpi-3-b-plus.dtb` | 35,252 B | Raspberry Pi 3 Model B+ |
| `bcm2710-rpi-3-b.dtb` | 34,731 B | Raspberry Pi 3 Model B |
| `bcm2710-rpi-zero-2-w.dtb` | 33,708 B | Raspberry Pi Zero 2 W |

Format: flattened devicetree (FDT, magic `d00dfeed`). Readable with
`dtc -I dtb -O dts`. The 3B+ file: `model = "Raspberry Pi 3 Model B+"`,
`compatible = "raspberrypi,3-model-b-plus", "brcm,bcm2837"`, 171 nodes,
12 with `status = "okay"` and 33 `"disabled"` (turned on by overlays).

## What is inside (3B+)

Every block of the chip with its bus address (`0x7e…`), interrupts,
clocks and pins:

| Group | Nodes |
|---|---|
| CPU | `cpu@0`–`cpu@3` (`enable-method = "spin-table"`), `l2-cache0`, the ARM local interrupt controller `@40000000` |
| Interrupts, timers, DMA | `interrupt-controller@7e00b200`, `timer@7e003000`, `dma-controller@7e007000`, `watchdog@7e100000` (also power domains), `rng@7e104000` |
| Clocks | `cprman@7e101000`, `clk-osc` (19.2 MHz), `clk-usb` |
| Mailbox, firmware | `mailbox@7e00b880`, `mailbox@7e00b840`, `firmware` (`raspberrypi,bcm2835-firmware`: clocks, GPIO expander, `vcio`), `power` |
| GPIO, serial | `gpio@7e200000`, `serial@7e201000` (PL011), `aux@7e215000` with `serial@7e215040` (mini UART) and two SPI |
| SD card | `mmc@7e202000` (SDHOST), `mmcnr@7e300000` (Arasan, for the Wi-Fi chip), `mmc@7e300000` |
| Buses | `i2c@7e205000`, `i2c@7e804000`, `i2c@7e805000`, `i2c0mux`, `spi@7e204000`, `i2s@7e203000`, `pwm@7e20c000`, `smi@7e600000` |
| USB | `usb@7e980000` (DWC2), with the LAN7515 and its Ethernet as child nodes |
| Display | `hvs@7e400000`, `pixelvalve@7e206000`, `@7e207000`, `@7e807000`, `hdmi@7e902000`, `dsi@7e209000`, `dsi@7e700000`, `dpi@7e208000`, `vec@7e806000`, `txp@7e004000`, `firmwarekms@7e600000`, `fb` |
| 3D, camera | `v3d@7ec00000`, `gpu`, `csi@7e800000`, `csi@7e801000` |
| Other | `thermal@7e212000`, `cpu-thermal`, `axiperf`, `gpiomem`, `sound`, `nvmem`, `led-act`, `led-pwr`, `linux,cma` |

## What `start*.elf` fills in

`/memory` (the ARM's part of the SDRAM), `/chosen` (`bootargs` from
`cmdline.txt`, the initramfs range), the Ethernet MAC address, the board
serial number and revision, and `dtparam` values. A key in `config.txt`
`device_tree=` picks another file.

## Redox and the devicetree

The Redox kernel and drivers take their register addresses from it and
translate the bus addresses (`0x7e…`) through the `ranges` of `/soc`.
Which nodes they use: redox-rpi `docs/architecture.md`.
