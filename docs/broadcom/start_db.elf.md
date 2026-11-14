# `start_db.elf`

The debug build of the main firmware: [`start.elf`](start.elf.md) with
assertions and more log output. `start_debug=1` in `config.txt` picks it,
together with `fixup_db.dat`. `bootcode.bin` knows both keys (`start_debug`,
`start_db`, `fixup_db`).

[All Broadcom files](README.md)

**Not on redox-rpi's cards, and not analysed here.** `scripts/rpi-firmware.sh`
copies only `start.elf` and `start_cd.elf`.

## What it is good for

- more messages in the VPU's log buffer, which Linux reads with
  `vcdbg log msg` (the `.init.vc_debug_sym` section tells the tool where
  the buffer is);
- assertions that stop the firmware early instead of failing later.

For redox-rpi the serial log from `uart_2ndstage=1` has been enough so
far.
