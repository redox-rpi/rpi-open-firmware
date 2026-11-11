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
| M1.5 | Serial monitor for test builds (`MONITOR=1`): peek/poke, dump, load code over the UART, run it, chain another `bootcode.bin` | **passed**, Pi test #269 (2026-11-07): every command, lk-overlay's stage 2 loaded over serial (296 KB, 52 s) and run to our 64-bit payload, lk-overlay's stage 1 chained; chaining Broadcom's `bootcode.bin` network-boots, but with only 256 MB detected |
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
the normal path, so an unattended boot still works. Only Enter (CR or LF)
opens the monitor; other bytes, such as line noise at power-on, are
ignored. **Never in a release:** anyone on the serial pins gets full
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
| `chain A N [R]` | copy a `bootcode.bin` image to `0x80000000` and start it at `0x80000200`, SDRAM left running; `R` = 1 takes the SDRAM down first (experimental: Broadcom's `bootcode.bin` hangs after it, #313, #320) | `ok chain …, SDRAM running` (or `SDRAM reset`), no return |
| `arm [M]` | start the ARM cores at ARM address 0 (bus `0xc0000000`; load an `armstub8` there and a kernel at `0xc0080000` first). M = 0 starts them in AArch32, anything else or nothing in AArch64. The monitor keeps running. #230 M2 step 2, `arm.c`, not yet on the Pi |
| `sd` | set up the SD card through SDHOST (GPIO 48–53, 1-bit) | `ok sd NAME N MB SDHC\|SDSC, SH_CDIV X` |
| `sdr L A N` | read N sectors from sector L to A | `ok crc32 X` |
| `ls` | list the root directory of the card's first FAT partition (FAT16/32) | lines, then `ok` |
| `fl NAME A` | load a root-directory file to A (long names, any case) | `ok NAME N bytes crc32 X` |
| `boot64 [K [DTB]]` | load `armstub8-redox.bin` (or `armstub8.bin`) to ARM 0, the kernel K (default `kernel8.img`) to 0x80000 and the devicetree (default `bcm2710-rpi-3-b-plus.dtb`, `-` for none) to 0x2000000; set the memory size in the devicetree, `kernel_entry32` and `dtb_ptr32`; start the ARM in AArch64 | `ok boot64` |
| `reset` | full chip reset through the watchdog, as a reboot | `ok reset`, `rbc> watchdog reset`, no return |
| `boot` | leave the monitor (boot selector: chain the built-in image) | `ok boot` |

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

### Boot selector (test Pi's network boot card)

`make MONITOR=1 CHAIN_IMAGE=broadcom-bootcode.bin` builds Broadcom's
`bootcode.bin` into the monitor (`chain-image.S`, the file unchanged, about
60 KB in all; the Boot ROM loads up to 128 KB). As `bootcode.bin` on the
network boot card it lets the test agent pick the boot path from the
serial line alone, with no card swap:

- **Enter within 5 s:** the monitor stays open for firmware tests (load
  and run our own stages, read registers).
- **No Enter:** it prints `rbc> chain built-in bootcode.bin, N bytes,
  crc32 X` and chains Broadcom's `bootcode.bin` before our SDRAM set-up
  (`ok chain …, SDRAM untouched`), so Broadcom's code finds the SDRAM as
  the Boot ROM left it and network-boots U-Boot and Redox as before. The
  first version set up the SDRAM, took it down and then chained; Broadcom's
  code never got going after that (Pi test #313 step 0).
- **Enter:** our SDRAM set-up runs, then `ok monitor` and the prompt.
  `boot`, or 300 s without a character, resets the chip through the
  watchdog (`rbc> watchdog reset`): the selector starts again and, with
  nobody pressing Enter, boots normally. Chaining Broadcom's code after
  our SDRAM set-up does not help: with the SDRAM left running it skips
  its own set-up and sees only 256 MB (#269), with the SDRAM taken down it
  hangs (#313, #320).

The normal boot gets about 5 s longer. Later
our own VPU loader (M2) replaces the built-in Broadcom image.

## Files and where they come from

| File | What | Source |
|---|---|---|
| `start.S` | entry at `0x80000200`, stack, exception stubs | entry address and 0x200 gap as rpi-open-firmware `firmware/start.s`; rest new |
| `linker.ld` | layout in the 128 KB L2 cache | new |
| `main.c` | stage 1 main, exception and panic output | new |
| `clock.c` | PLLC 1000 MHz, VPU clock, 1 MHz system timer | rpi-open-firmware `firmware/romstage.c`, `switch_vpu_to_pllc()`, values unchanged |
| `uart.c` | PL011 on GPIO 14/15 | rpi-open-firmware `firmware/romstage.c`, `uart_init()`, values unchanged |
| `timer.c`, `printf.c` | delay, small printf | new |
| `sd.c` | SD card through SDHOST, read-only; ported from rpi-open-firmware's `sdhost_impl.cc`, with the slower-clock retry from Pi test #249 |
| `fat.c` | FAT16/32 root directory and file reads, new code from the FAT specification; host test in `tests/fat-test.sh` (FAT16 and FAT32 images made with mtools) |
| `fdt.c` | writes `/memory@0 reg` in the devicetree, as `start.elf` does; new code from the Devicetree Specification; host test in `tests/fdt-test.sh` |
| `arm.c` | ARM start for the monitor's `arm`: address map, ARM clock (PLLB), ARM power domain, bridge; ported from rpi-open-firmware's ARM control and power drivers |
| `chain-image.S` | places a `bootcode.bin` image in the boot selector build | new; the image is Broadcom's, unchanged |
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
