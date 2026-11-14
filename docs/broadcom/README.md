# Broadcom's files, one page each

One page for every file of Broadcom's Raspberry Pi firmware that a redox-rpi
card carries, plus the variants it doesn't carry. Each page says what is
inside the file, as far as we can see without running it.

All numbers are from release `1.20260915` of
[raspberrypi/firmware](https://github.com/raspberrypi/firmware), the one
redox-rpi uses (`scripts/rpi-firmware.sh` in redox-rpi). They were read
with `readelf`, `strings`, `xxd` and `dtc` on 2026-11-14. The overview of
all closed pieces and which models use them is
[broadcom-firmware.md](../broadcom-firmware.md). The chip they run on:
[hardware/pi3b-plus.md](../hardware/pi3b-plus.md).

| File | Size | Runs on | Page |
|---|---|---|---|
| `bootcode.bin` | 52,624 B | VPU, from L2 cache | [bootcode.bin.md](bootcode.bin.md) |
| `start.elf` | 3,023,104 B | VPU, from SDRAM, stays running | [start.elf.md](start.elf.md) |
| `start_cd.elf` | 844,380 B | VPU, from SDRAM, stays running | [start_cd.elf.md](start_cd.elf.md) |
| `start_x.elf` | not on our cards | VPU | [start_x.elf.md](start_x.elf.md) |
| `start_db.elf` | not on our cards | VPU | [start_db.elf.md](start_db.elf.md) |
| `fixup.dat`, `fixup_cd.dat` (and `fixup_x`, `fixup_db`) | 7,381 B, 3,280 B | read by `bootcode.bin` | [fixup.dat.md](fixup.dat.md) |
| `armstub8` | built into `start*.elf` | ARM, EL3 | [armstub8.md](armstub8.md) |
| `bcm2710-rpi-3-b-plus.dtb` (and the 3B and Zero 2 W ones) | 35,252 B | read by `start*.elf`, used by the kernel | [bcm2710-rpi-3-b-plus.dtb.md](bcm2710-rpi-3-b-plus.dtb.md) |
| `overlays/disable-bt.dtbo` | 1,073 B | applied by `start*.elf` | [disable-bt.dtbo.md](disable-bt.dtbo.md) |
| `overlays/mmc.dtbo` | 1,221 B | applied by `start*.elf` | [mmc.dtbo.md](mmc.dtbo.md) |
| `LICENCE.broadcom` | 1,594 B | – | [LICENCE.broadcom.md](LICENCE.broadcom.md) |

The order they act in at power-on: Boot ROM → `bootcode.bin` →
`start*.elf` with `fixup*.dat` → devicetree and overlays → `armstub8` →
U-Boot (or the kernel). `start*.elf` keeps running on the VPU afterwards
and answers the ARM over the mailbox.

## How we look at them

- Only what the files show on their own: headers, section tables,
  embedded strings, data layouts. The licence allows using and passing
  them on unmodified; it says nothing about reading them.
- Findings from our internal disassembly of `bootcode.bin` are behaviour
  only (register order, decisions), never code. They are in
  [own-firmware.md](../own-firmware.md#findings-from-the-closed-bootcodebin)
  and in the `bootcode/` README.
- Where something is the usual community reading rather than something we
  checked, the page says so.
