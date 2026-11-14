# `overlays/disable-bt.dtbo`

A devicetree overlay (1,073 bytes, open source from the Raspberry Pi Linux
tree). `dtoverlay=disable-bt` in `config.txt` makes `start*.elf` apply it
to the board's devicetree before the kernel starts.

[All Broadcom files](README.md)

## Why it is needed

The BCM2837 has two UARTs: the full **PL011** (`serial@7e201000`, alias
`uart0`) and the small **mini UART** in the AUX block (`serial@7e215040`,
`uart1`), whose baud rate follows the VPU core clock. On boards with
Bluetooth, the PL011 is wired to the Bluetooth chip and the mini UART to
GPIO 14/15. Redox's console driver supports only the PL011, so the PL011
has to move to GPIO 14/15.

## What is inside

Six fragments; the targets are resolved by label (`__fixups__`) when the
overlay is applied:

| Fragment | Target | Change |
|---|---|---|
| 0 | `uart1` (mini UART) | `status = "disabled"` |
| 1 | `uart0` (PL011) | `pinctrl-0 = <uart0_pins>`, `status = "okay"` |
| 2 | `bt` (the Bluetooth node under the PL011) | `status = "disabled"` |
| 3 | `uart0_pins` | pins, function and pull emptied: the PL011 no longer takes the Bluetooth pins (GPIO 32/33) |
| 4 | `bt_pins` | emptied the same way |
| 5 | `/aliases` | `serial0` points to the PL011, `serial1` to the mini UART |

Result: the PL011 drives GPIO 14 (TXD) and 15 (RXD) on pins 8 and 10 of
the header; Bluetooth is off. Linux's `hciuart` service must not run, but
Redox has none.

`start*.elf` also reads `enable_uart=1`, which with this overlay simply
turns on the console at 115200 baud.
