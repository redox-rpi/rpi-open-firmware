# redox-rpi armstub8: our own 64-bit ARM start stub for the Pi 3B+

The first code the four Cortex-A53 cores run, from address 0 in EL3. It
replaces the `armstub8` that Broadcom builds into `start.elf`. It is part
of milestone M2 of our open boot firmware
([../bootcode/README.md](../bootcode/README.md),
[redox-rpi/redox-rpi#230](https://github.com/redox-rpi/redox-rpi/issues/230)).

What it does, in `armstub8.S` (3076 bytes since rof #40; 256 before):

- **In EL3:**
  - CNTFRQ_EL0 = 19.2 MHz and CNTVOFF_EL2 = 0.
  - The ARM local timer runs from the crystal, divided by 1.
  - SMPEN in CPUECTLR_EL1, and lower ELs may use the Cortex-A53 control
    registers.
  - No FP/SIMD traps.
  - SCR_EL3: non-secure, HVC on, SMC on (PSCI, see below), EL2 in AArch64.
  - VBAR_EL3 = the stub's EL3 vector table at `0x800`.
  - SCTLR_EL2 with MMU and caches off.
  - Then `eret` to EL2.
- **Core 0:** jumps to `kernel_entry32` (default `0x80000`) with
  x0 = `dtb_ptr32`, x1–x3 = 0, as the arm64 Linux boot protocol wants it.
- **Cores 1–3:** wait with `wfe` on their spin-table slot (`0xe0`, `0xe8`,
  `0xf0`) until the kernel writes an entry address there and sends `sev`.
  U-Boot and Linux find the slots through `cpu-release-addr` in the
  devicetree.

The last 16 bytes use the layout that Broadcom's `start.elf` patches:
magic `0x5afe570b` at `0xf0`, version at `0xf4`, the devicetree address at
`0xf8`, the kernel address at `0xfc`. Our VPU loader fills the same
fields, so either firmware can start this stub (`armstub=armstub8.bin` in
`config.txt` for Broadcom's). Core 3's slot overlaps the magic, so a slot
that still holds the magic counts as empty.

## PSCI: poweroff and reset (rof #40)

Redox (and Linux) stop and restart the Pi through PSCI. Nothing on the
Pi 3 or Zero 2 W implemented it, so `poweroff` ended in
`PSCI SYSTEM_OFF failed: Unavailable` and the board stayed on with the
picture still on the screen. The stub now answers the calls itself with
an SMC handler in EL3:

| Call | Answer |
|---|---|
| `PSCI_VERSION` | 1.0 |
| `PSCI_FEATURES` | 0 for the four calls in this table, `NOT_SUPPORTED` otherwise |
| `SYSTEM_OFF` | sets the halt partition (63) in `PM_RSTS`, then a full reset through the PM watchdog |
| `SYSTEM_RESET` | a full reset through the PM watchdog |
| anything else | `NOT_SUPPORTED` (cores 1–3 still start through the spin table) |

After `SYSTEM_OFF` our bootcode (and the boot selector) sees the halt
partition and stays off until GPIO3 (pin 5) goes to ground, as with
Linux and Broadcom's firmware. The chip reset also stops the HDMI
output, so the monitor goes to standby. It is a low-power halt, not a
cut of the 5 V: the Pi 3 and Zero 2 W have no PMIC that can switch
themselves off.

The kernel only uses PSCI when the devicetree has a `/psci` node
(`compatible = "arm,psci-1.0", "arm,psci-0.2"`, `method = "smc"`).
`boot64` adds it when the armstub carries the word `PSCI` at `0x100`, so
an older stub on the card (`armstub8-redox.bin`) gets no node and the
kernel falls back to the watchdog for reset, as before.

Power in the halt: the Shelly plug read 0.0 W on the 3B+ (Tests 496, 504;
2.9 W with Redox running), and a power bank's display showed about
0.1 W on the Zero 2 W (maintainer, 2026-11-18, a rough reading).

The stub must stay inside the first 4 KiB: the Broadcom devicetrees keep
`0x0–0x1000` from the kernel with `/memreserve/`.

**QEMU test:** `qemu-psci.py STUB KERNEL8 INITRAMFS DTB [--cmd poweroff|reset]`
boots a direct-boot Redox through the stub (no `-kernel`, all cores start
in EL3) with a `/psci` node, logs in on serial and runs the command.
Result 2026-11-18 (QEMU 10.2.1, the console466 kernel8.img and
initramfs): `PSCI 1.0 using smc conduit from device tree`, 4 CPUs online,
`kstop` and `kreset` each end in the watchdog reset (QEMU exits under
`-no-reboot`). With the old 256-byte stub the same run shows
`PSCI SYSTEM_OFF failed: Unavailable` and QEMU stays up. QEMU does not
keep `PM_RSTS` across the reset, so the halt itself is a Pi test.

## Build and test

```bash
cd armstub
make          # build/armstub8.bin, build/smp-test.bin (aarch64-none-elf GCC
              # from ../redox-rpi/build-toolchain.sh)
```

`smp-test.S` is a test kernel. Core 0 prints its EL, CNTFRQ and x0, then
releases cores 1–3 one after the other through the spin table. Each core
prints its MPIDR and EL. The last line is `armstub8 test: 4 cores up`.

In QEMU (no `-kernel`, so all four cores start at 0 in EL3 and run our
stub; `dtb_ptr32` set to `0x2000000`):

```bash
qemu-system-aarch64 -M raspi3b -nographic -monitor none -serial stdio \
  -device loader,file=stub-patched.bin,addr=0x0 \
  -device loader,file=build/smp-test.bin,addr=0x80000 \
  -device loader,file=bcm2710-rpi-3-b-plus.dtb,addr=0x2000000
```

Result (QEMU 10.2.1, 2026-11-09): all four cores drop from EL3 to EL2
(`-d int`: `Exception return from AArch64 EL3 to AArch64 EL2 PC 0x68`,
four times), CNTFRQ 19.2 MHz, x0 = `0x2000000`, `4 cores up`.

`qemu-redox.sh IMAGE U-BOOT.bin MAINLINE.dtb` boots a whole Redox SD
image the same way: stub, then our U-Boot, then the Redox bootloader and
kernel. With the stable image of 2026-11-09 it reached
`DRAM: 948 MiB`, `CPU activation complete: 4 CPUs online` (Redox starts
cores 1–3 through our spin table) and `devshd: listening`.

Not yet on the Pi: QEMU does not check the A53 control registers or the
ARM local timer. That needs a Pi test (Broadcom's firmware with
`armstub=` first, our VPU loader later).

## What a firmware must do besides the stub

These came out of the QEMU runs: with only the stub, U-Boot starts but
says `DRAM: 0 Bytes`, because the devicetree's `memory@0` node has size
0. Broadcom's firmware fills it in. Before it starts the ARM, our loader
has to:

- set `/memory@0` `reg` to the ARM's share of the RAM (948 MiB with
  `gpu_mem=16`);
- apply what `config.txt` overlays do (`disable-bt`: console on the PL011;
  `mmc`: SD card on the Arasan controller), or load a devicetree with them
  already merged;
- later: `/chosen` `bootargs`, the board serial number and the Ethernet
  MAC (`local-mac-address`).
