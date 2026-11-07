# redox-rpi bootcode: our own open boot firmware for the Pi 3B+

Goal: replace **all** of Broadcom's closed boot pieces on the Raspberry Pi
3B+ (BCM2837B0) with our own open code: `bootcode.bin`, `start*.elf` with
`fixup*.dat`, and the `armstub8` built into the firmware. Only the Boot ROM
inside the chip stays. Plan and progress:
[redox-rpi/redox-rpi#230](https://github.com/redox-rpi/redox-rpi/issues/230).

It is a small, flat C program for the VideoCore IV processor (VPU). There is
no RTOS: one file per hardware block, strict warnings (`-Wall -Wextra
-Werror`), and every file says where its code comes from.

## Milestones

| | What | State |
|---|---|---|
| **M1** | Stage 1: VPU clock, UART, SDRAM, memory test, heartbeat | **passed** on the Pi 3B+, Pi test #265 (2026-11-07): Elpida 1 GB, DDR PLL 398.4 MHz, self test 1024 MB in 28 ms, heartbeat to 60 s |
| M1.5 | Serial monitor for test builds (`MONITOR=1`): peek/poke, dump, load code over the UART, run it, chain another `bootcode.bin` | built, Pi test queued |
| M2 | Read the SD card's FAT partition, load our 64-bit ARM stub, U-Boot and the devicetree, start the ARM in AArch64 with cores 1–3 in the spin table | |
| M3 | A small service that stays on the VPU and answers the mailbox (board, memory, MAC, clocks, temperature, power) for U-Boot, `rpi-firmwared`, `rpi-fbd` | |
| M4 | HDMI and a framebuffer (PLLH, HVS, pixel valve, HDMI) | |
| M5 | Network boot; what 3D needs (V3D power and clock, GPU memory) | |

For debugging only, stage 1 may later also start Broadcom's `start.elf`, to
swap one piece at a time.

## Build

Needs the VC4 toolchain from `../redox-rpi/build-toolchain.sh` (GCC 6.2.1
from itszor/vc4-toolchain, built once, about 40 minutes).

```bash
cd bootcode
make                          # build/bootcode.bin, build/bootcode.dis
./make-card.sh m1.img         # FAT16 card image with only bootcode.bin
```

Serial console on GPIO 14/15, 115200 8N1.

## Serial monitor (test builds only)

`make MONITOR=1` adds `monitor.c`. After the SDRAM test it prints
`rbc> monitor: press Enter within 5 s`. With no input it carries on with
the normal path, so an unattended boot still works. Enter opens the
monitor. **Never in a release:** anyone on the serial pins gets full
control of the chip.

Each command gets one reply line that starts with `ok` or `err`, then
the prompt `rbc> `. Numbers are hex.

| Command | What | Reply |
|---|---|---|
| `r A` | read the word at A | `ok A V` |
| `w A V` | write V to A | `ok` |
| `s A M`, `c A M` | set / clear bits M at A | `ok A V` |
| `d A N` | dump N bytes | lines, then `ok` |
| `l A N [C]` | load N bytes, sent as 2N hex digits (other characters skipped); with C the CRC-32 must match | `ok send …`, then `ok crc32 X`; `err timeout …` after 2 s without a digit, `err crc32 X want C` |
| `crc A N` | CRC-32 (zlib) of N bytes | `ok crc32 X` |
| `g A [r0 r1 r2 r3]` | call VPU code at A | `ok ret X` |
| `chain A N` | copy a `bootcode.bin` image to `0x80000000`, start it at `0x80000200` | `ok chain …`, no return |
| `boot` | leave the monitor | `ok boot` |

Before any `err`, the monitor drops input until the line has been quiet
for 0.5 s, so the rest of a broken upload is not read as commands. The
prompt follows, and the host can retry without a reset. `rbc-load.sh`
prints the expected time (about 5.7 KB of file per second at 115200
baud, so 100 KB takes about 18 s) and retries a failed load up to three
times.

Load into SDRAM through the uncached alias, for example `0xc1000000`
(16 MB). Other `0x8…` addresses would evict this code from the L2 cache.
`chain` copies a small trampoline to `0x80019000` first, so a chained
image can be up to `0x19000` bytes (100 KB).

From the host: `bootcode/rbc-load.sh [--enter] FILE [ADDR] [chain|go|none]`.
It writes only to `/dev/ttyUSB0` and reads the replies from the log of
the one serial reader (`LOG`, default `~/redox-rpi/logs/serial.log`), so
there is never a second reader. `--enter` waits for the monitor line after
a reset and opens the monitor. `go-test.S` (`make build/go-test.bin`) is
the smallest payload for `g`: `g c1000000 5 7` replies `ok ret 0000100C`.

`make-card.sh OUT.img BASE.img` puts our `bootcode.bin` into a copy of
another card image. The monitor test card is lk-overlay's card with our
`bootcode.bin`, so lk-overlay's stage 1 can be chained over serial and
still find its stage 2 on the card.

## Files and where they come from

| File | What | Source |
|---|---|---|
| `start.S` | entry at `0x80000200`, stack, exception stubs | entry address and 0x200 gap as rpi-open-firmware `firmware/start.s`; rest new |
| `linker.ld` | layout in the 128 KB L2 cache | new |
| `main.c` | stage 1 main, exception and panic output | new |
| `clock.c` | PLLC 1000 MHz, VPU clock, 1 MHz system timer | rpi-open-firmware `firmware/romstage.c`, `switch_vpu_to_pllc()`, values unchanged |
| `uart.c` | PL011 on GPIO 14/15 | rpi-open-firmware `firmware/romstage.c`, `uart_init()`, values unchanged |
| `timer.c`, `printf.c` | delay, small printf | new |
| `monitor.c`, `go-test.S`, `rbc-load.sh`, `libc.c` | serial monitor (test builds), its test payload and host script, `memset`/`memcpy` | new; cache flush registers from Broadcom's `vpu_l1_cache_ctrl.h` |
| `sdram.c` | LPDDR2 set-up and self test | rpi-open-firmware `firmware/sdram.c` (Julian Brown and the rpi-open-firmware authors); sequence, timings and magic values unchanged; self test extended |
| `hw.h` | common header | new; register names from Broadcom's headers |
| `../firmware/broadcom/` | VideoCore IV register definitions | released by Broadcom (BSD), used as they are |

rpi-open-firmware commit `82a6264` (GPLv2+). Its VPU code brought up our
Pi's 1 GB Elpida LPDDR2 in Pi test #224. lk-overlay (librerpi, `50c95c1`,
GPLv2+) has the same SDRAM sequence. This directory is GPLv2 or later as
well.

## What the closed firmware does (internal disassembly)

To learn what our code still lacks, we disassemble Broadcom's
`bootcode.bin` with `vc4-elf-objdump` from the same toolchain. We note
**behaviour only**: which hardware it touches, register order and values.
No code is copied, and the disassembly itself is not published. Each fact
names the file it comes from.

Source: `bootcode.bin` from Raspberry Pi firmware `1.20260915`, 52 624
bytes, sha256 `4245ec23b58158dc…`.

- **Layout:** the first 0x200 bytes are zero; code starts at `0x80000200`.
  That matches what the Boot ROM does and what we do.
- **Hardware it touches** (bus addresses it loads): SDRAM controller
  (`0x7ee00000`) and the DDR PHY (`0x7ee06000`, `0x7ee07000`); cache
  controls (`0x7ee01000`, `0x7ee02000`); power manager (`0x7e100000`),
  clock manager (`0x7e101000`), PLLs (`0x7e102000`); system timer; GPIO;
  UART; SDHOST (`0x7e202000`); OTP (`0x7e20f000`); and a lot of USB (dwc2 at
  `0x7e980000`, about a third of all register references), for USB and
  network boot. It does **not** use the EMMC controller (`0x7e300000`): the
  SD card is read through SDHOST.
- **What it reads:** `config.txt` (the names `gpu_mem`, `total_mem`,
  `start_file`, `fixup_file`, `start_x`, `force_pvt` appear as text), the
  8.3 names `CONFIG.TXT`, `START.ELF`, `RECOVERY.ELF`, `TIMEOUT`, `UART`; it
  knows exFAT; for network boot it sends the DHCP class
  `PXEClient:Arch:00000:UNDI:002001`.
- **Hand-off to `start.elf`:** it copies a small routine (about 512 bytes)
  out of itself into SDRAM, flushes the caches through the cache control
  registers above, and jumps to the ELF entry of `start.elf`. Still open:
  which values it passes in registers or memory. That decides whether our
  stage 1 can start Broadcom's `start.elf` (debug option above).

More to do in the disassembly, in this order: the SDRAM sequence compared
with ours (timings, the B0 chip's slew), the PLL values it sets, how it
picks `start_cd.elf` for `gpu_mem=16`, and the hand-off values.
