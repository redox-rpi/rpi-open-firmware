# Our own firmware (redox-rpi#230)

On 2026-11-07 the maintainer decided to replace **all** of Broadcom's
closed boot pieces (`bootcode.bin`, `start*.elf` with `fixup*.dat`,
`armstub8`) with our own code, instead of taking rpi-open-firmware or
lk-overlay as they are. Both start on the 3B+
([rpi-open-firmware-2019.md](rpi-open-firmware-2019.md),
[lk-overlay.md](lk-overlay.md)). But rpi-open-firmware is old C++ with
bugs we had to fix, and lk-overlay runs a whole RTOS (LK) in stage 1, has
dead code and builds with warnings allowed.

Ours is flat C for the VPU: one file per hardware block, `-Wall -Wextra
-Werror`, and every file names where its code comes from. Code: branch
`bootcode`, directory
[`bootcode/`](https://github.com/redox-rpi/rpi-open-firmware/tree/bootcode/bootcode),
with commands and files listed in its README. GPLv2+, because the SDRAM,
clock, UART and ARM sequences come from rpi-open-firmware.

## Milestones

| | What | State |
|---|---|---|
| **M1** | Stage 1: VPU clock, UART, SDRAM, memory test, heartbeat | **passed**, Pi test #265 |
| Monitor | Test builds only (`MONITOR=1`): registers, load over serial, run, chain | **passed**, #269 |
| Boot selector | Monitor with Broadcom's `bootcode.bin` built in, for the test Pi's card | **passed**, #313, #320 rerun |
| M2 | Start the ARM in AArch64; load our `armstub8`, U-Boot and the devicetree from the SD card's FAT partition; cores 1–3 in the spin table | ARM start written (PR #7, row 332); `armstub8` built (PR #4); SD and FAT not started |
| M3 | A small service on the VPU that answers the mailbox (board, memory, MAC, clocks, temperature, power) for U-Boot, `rpi-firmwared`, `rpi-fbd` and the CPU governor | not started |
| M4 | HDMI and a framebuffer (PLLH, HVS, pixel valve, HDMI) | not started |
| M5 | Network boot; what 3D needs (V3D power and clock, GPU memory) | not started |

## M1: stage 1

VPU clock, UART, SDRAM and a memory test of 32 regions over the whole
1 GB, in 5.5 KB. On the Pi (#265, 2026-11-07): Elpida 1 GB LPDDR2, DDR PLL
locked at 398.4 MHz, self test passed in 28 ms, SDRAM ready 52 ms after
our code starts, then a heartbeat every 10 s for 60 s with no panic. The
system timer already reads 2.68 s when our code starts, so the Boot ROM
takes that long before it runs `bootcode.bin`. Why is still open.

## Serial monitor (test builds only)

After the SDRAM test it waits 5 s for Enter; otherwise it goes on with the
normal boot. Commands: read, write, set and clear bits, dump, load code
over the UART as hex with a CRC-32 check, call VPU code, `chain` another
`bootcode.bin` image through a trampoline at `0x80019000`, `arm`,
`reset` and `boot`. Each reply starts with `ok` or `err`, so the Pi test
agent can script it with `bootcode/rbc-load.sh`, which only writes to the
serial port and reads the log of the one serial reader. New VPU stages
come over serial; no card swap.

**Never in a release:** anyone on the serial pins gets full control of
the chip.

Pi test #269 passed every command, loads from 8 bytes to 296 KB with no
retry, and lk-overlay's stage 2 loaded over serial and run.

## Boot selector (the test Pi's card)

The monitor with Broadcom's `bootcode.bin` (1.20260915) built in.

- **No Enter within 5 s:** it chains Broadcom's code **before** touching
  the SDRAM, and the Pi boots exactly as with Broadcom's card: 998 MiB,
  network boot, `redox login:`.
- **Enter:** our SDRAM set-up, then the monitor.
- **`boot`, or 300 s without input:** a watchdog reset, after which the
  selector boots without the monitor.

What we learned on the way:

- **Chaining after our SDRAM set-up fails.** With the SDRAM left running,
  Broadcom's code skips its own set-up and sees only 256 MB (U-Boot:
  `DRAM: 240 MiB`, #269). The cause, from the disassembly (behaviour only):
  Broadcom's SDRAM routine sets the size to 256 MB first, and skips its
  whole set-up if SD_CS bit 0 (RESTRT) reads 1. Our set-up ends by setting
  that bit; the Boot ROM leaves SD_CS at `0x794200`, with the bit clear.
  With the SDRAM taken down first (SD_CS = 0 or the ROM's value), it
  hangs (#313 with e0dbedb, #320 with b081bfd). So the selector chains
  before our set-up (b081bfd, #313 passed).
- **Leaving the monitor needs a full reset.** The first selector hung
  after `boot` and after the idle time (#320). The fix is the watchdog
  reset Linux's `bcm2835_wdt` uses: `PM_WDOG = 0x5a000000 | 10`, then
  `PM_RSTC = 0x5a000000 | (PM_RSTC & ~0x30) | 0x20`. Tested by hand
  (#322), then in the file (233c8c5, #320 rerun passed on 2026-11-11).

## M2: ARM start and `armstub8`

- **`armstub8`** (PR #4, branch `armstub8`): our 64-bit ARM start stub,
  256 bytes, written from the BSD-licensed
  [`armstub8.S`](https://github.com/raspberrypi/tools/blob/master/armstubs/armstub8.S).
  In EL3 it sets CNTFRQ (19.2 MHz), the ARM local timer, SMPEN and the
  A53 control-register access, SCR_EL3 and SCTLR_EL2, then drops to EL2.
  Core 0 jumps to the kernel; cores 1–3 wait on the spin table (`0xe0`,
  `0xe8`, `0xf0`). It keeps the layout `start.elf` patches, so both
  firmwares can start it. In QEMU: four cores, Redox reaches `4 CPUs
  online`. On the Pi, with Broadcom's firmware and `armstub=` in
  `config.txt`: 998 MiB, 4 CPUs, pi-test 6/6. The log can't prove that
  ours ran rather than the built-in one; `uart_2ndstage=1` should show it
  next time.
- **ARM start from our VPU code** (PR #7, branch `arm-start`): the
  monitor's `arm` maps the SDRAM and the peripherals for the ARM, sets
  the ARM clock from PLLB (about 470 MHz), powers the ARM domain and
  releases the bridge, with the AArch64 bit (`ARM_CONTROL0` bit 9) set.
  rpi-open-firmware defines that bit but never sets it. Pi test row 332:
  load `armstub8` and its test kernel over serial, run `arm`, expect
  `4 cores up`.
- **Next:** read the SD card (SDHOST) and the FAT partition on the VPU,
  load `armstub8`, U-Boot and the devicetree from it.

## Findings from the closed `bootcode.bin`

From an internal disassembly, behaviour only, not published; the README
in `bootcode/` lists each finding with its source.

- It uses SDHOST, not EMMC, for the SD card.
- About a third of its register references are USB (USB and network
  boot).
- It reads `config.txt` for `gpu_mem`, `total_mem`, `start_file` and
  more.
- It hands off to `start.elf` through a small routine copied into SDRAM
  after a cache flush.
- Its SDRAM set-up: see the boot selector above.

## What Redox needs from a firmware without `start.elf`

Today several redox-rpi parts ask the running `start.elf` over the
mailbox: U-Boot (board, memory, MAC), `rpi-boot-stub` (USB power),
`rpi-firmwared` (temperature, clocks), the CPU governor, `rpi-fbd`
(framebuffer) and `rpi-update` (tryboot). Without `start.elf` either our
VPU code answers (M3, M4) or the ARM side does these steps itself. The
full list: [boot-chain.md](boot-chain.md#the-broadcom-pieces-and-what-we-know-about-them).

**Video codecs:** the hardware decoders (H.264, MPEG-2/VC-1 with a
licence key) are driven by code inside `start.elf`/`start_x.elf`, reached
from the ARM through VCHIQ/MMAL. Reusing that code is not an option: the
Broadcom licence allows the files only unmodified, and the code depends
on the rest of the closed firmware (ThreadX, its memory manager, VCHIQ).
The block's registers are not documented. Without the closed firmware
the choices are software decoding on the ARM (FFmpeg with NEON, H.264 up
to about 720p on a Pi 3) or our own codec driver from scratch, a project
of its own. This is one reason to keep Broadcom's `start.elf` as an
option next to our firmware.
