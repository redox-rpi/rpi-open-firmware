# `armstub8` (built into `start*.elf`)

Not a file on the card: a 256-byte piece of ARM code inside `start.elf` and
`start_cd.elf`. The firmware writes it to ARM address 0 before it releases
the ARM cores, and it is the first ARM code that runs. `armstub=<file>` in
`config.txt` replaces it with one from the card.

[All Broadcom files](README.md)

## What it does

From Raspberry Pi's open source,
[`armstub8.S`](https://github.com/raspberrypi/tools/blob/master/armstubs/armstub8.S)
(BSD licence), in EL3 on all four Cortex-A53 cores:

1. sets `CNTFRQ_EL0` to 19.2 MHz and the ARM local timer, so the generic
   timer works;
2. sets the SMPEN bit (`CPUECTLR_EL1`) for cache coherency between the
   cores, and allows lower levels to reach the A53's control registers;
3. sets `SCR_EL3` (next level 64-bit, non-secure) and `SCTLR_EL2`;
4. drops to EL2;
5. core 0 jumps to the kernel at the address the firmware wrote into the
   stub (the firmware's default or `kernel_address`), with the
   devicetree's address in `x0`;
6. cores 1–3 wait in a loop on the spin table at `0xe0`, `0xe8`, `0xf0`
   until the kernel writes a start address there.

`start*.elf` patches two words near the end of the stub before starting
it: the devicetree address (`dtb_ptr32`, `0xf8`) and the kernel address
(`kernel_entry32`, `0xfc`).

## Variants

`armstub7` (32-bit kernels on the 3B+), `armstub8` (64-bit, our case),
`armstub8-gic` (Pi 4 and 400, also sets up the GIC-400). A single-core
Zero needs none.

## Ours

Our own `armstub8` (PR #4, branch `armstub8`) is built from the same
source and keeps the layout `start.elf` patches, so both firmwares can
use it; see [own-firmware.md](../own-firmware.md#m2-arm-start-and-armstub8).
