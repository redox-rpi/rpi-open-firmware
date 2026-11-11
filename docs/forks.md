# Forks of the sources we use

Survey of 2026-11-11: every fork, and forks of forks, of the repositories
named in [librerpi.md](librerpi.md) and [vpu-compiler.md](vpu-compiler.md).
For each fork, all of its branches were compared with the parent (GitHub API),
to find work that exists only there. Most forks are plain copies.

## The one that matters: glennra (Glenn Ramsey), November 2026

Started 2026-11-08, three days before this survey, and active right now.
He works on the same goal as our M2 and M4 on a **Pi 3B**, on top of
librerpi's code:

| Fork | Branch | What |
|---|---|---|
| [glennra/lk-overlay](https://github.com/glennra/lk-overlay/tree/feature/rpi3-dsi-linux) | `feature/rpi3-dsi-linux`, 23 commits, 2026-11-09/10 | lk-overlay's stage 1 boots stage 2 from FAT on the SD card; stage 2 sets up PLLD, I2C, a DSI pixel valve, the HVS channel, DSI clock and PHY, the TC358762 bridge and panel power for the official 7-inch display, builds display lists and scans out a framebuffer; then it copies an ARM chainloader, sets the ARM address map and PLLB, and hands off to it |
| [glennra/rpi-open-firmware](https://github.com/glennra/rpi-open-firmware/tree/feature/arm-handoff-review) | `feature/arm-handoff-review`, 8 commits, 2026-11-08/10 | the ARM chainloader: a handoff ABI (firmware data at ARM offset `0x20`, framebuffer words with magic `LFB1`), a `simple-framebuffer` node for Linux, SDHOST left Linux-safe, secondary cores parked for Linux SMP; `docs/arm-linux-handoff.md` |

His report (in that doc, not checked by us): a Pi 3B with the 7-inch DSI
display booted Linux with **all four cores online, simplefb console, USB
keyboard and a Buildroot login, with no Broadcom firmware**. Open points
he lists: the clean LK patch series is not finished, the last change is
untested on hardware, Pi 1/2 and repeated boots are unverified, and clock
and power ownership is not solved (`clk_ignore_unused pd_ignore_unused`).

What it means for us:

- **M4 (display):** his DSI path goes through the same HVS and pixel
  valves as HDMI; only the last stage (DSI PHY and bridge, not HDMI and
  PLLH) differs. The HVS and display-list code is the part we need.
- **M2:** his ARM handoff (memory map, PLLB, park secondary cores,
  devicetree fix-ups) is the same job as our `boot64` with `armstub8`.
  Worth comparing before Pi row 332.
- **The 3B+:** he tests a 3B (BCM2837 without "B0", LAN9514). Our 3B+ has
  the newer stepping, LAN7515 and the PMIC, so his results don't carry
  over as they are.
- Contact: the maintainer wrote to him on 2026-11-11 (his issues are off;
  his open PRs are librerpi/lk-overlay#8 and librerpi/rpi-open-firmware#3),
  inviting him to compare notes or contribute. He answered the same day
  (lk-overlay#8): he'd like to chat and asked for a channel. His goals
  were a custom animation on the VPU while the OS loads, and booting as
  fast as possible (still working on that); avoiding Broadcom's firmware
  was a side effect.

## Everything else

| Network | Forks | With own work | Verdict |
|---|---|---|---|
| christinaa/rpi-open-firmware (current repo, 2016) | 4 | only ours (redox-rpi, 18 commits on `redox-rpi` plus branches) | – |
| YanLinAung/rpi-open-firmware (the big network of the original, 112 forks) | 112 | [librerpi](https://github.com/librerpi/rpi-open-firmware) (216 commits beyond the original's last state), glennra (above), [hramrach](https://github.com/hramrach/rpi-open-firmware) (4 commits, 2021: JTAG flags, OTP row names, Nix build notes), TheMindVirus (README edits only); 5 forks are older copies of librerpi's | librerpi is the line to follow; the rest are copies of the original from 2016 to 2019 |
| librerpi/rpi-open-firmware | 21 | glennra, hramrach, TheMindVirus | as above |
| librerpi/lk-overlay | 10 | glennra (above); [doriangc](https://github.com/doriangc/lk-overlay) (3 commits, 2023: "initial HDMI support", last message "Still not working...") | doriangc: an HDMI attempt to read before M4 |
| librerpi/lk | 3 | none | – |
| librerpi/rpi-tools | 7 | none | – |
| itszor/vc4-toolchain | 19 | mfp20 (README and clean-up only) | – |
| itszor/gcc-vc4 (the GCC port itself) | 7 | none newer than GCC 6.2.1 (branch `vc4-gcc6`, last change 2023-01: macOS arm64 build fix); no fork moves it to a newer GCC | confirms: a newer GCC means porting it ourselves |
| itszor/binutils-vc4, newlib-vc4 | 3, 5 | none | – |
| hermanhermitage/videocoreiv | 110 | mm120 (2013: ld/st and mul/div tables), TheLastMutt (2013: 80-bit vector instructions in tinyasm, a FIR filter), maximus64 (2016: IDA 6.9 fix) | mm120's tables and TheLastMutt's vector encodings are useful for an assembler (vpu-compiler.md) |
| LLVM-VideoCore4 (phanirajkiran copy) | 7 | LEA0317 (LLVM 8 to 14), randoruf (clean-up only); hiro-0317 (LLVM 19) is a separate repo by the same author as LEA0317 | see [vpu-compiler.md](vpu-compiler.md) |
| phire/llvm | 6 | ryarnyah (2013, older) | – |
| maazl/vc4asm (QPU) | 17 | small fixes (Jay-Madden 2026, LoekHabets 2024, YggdrasiI 2017) | – |
| thubble/videocore-elf-dis | 1 | none | – |

Method: `scratchpad/forks.sh`-style walk with the GitHub API (forks of
forks included); for the big network every branch of every fork was
compared with the original's last commit (2019-02-04). Forks GitHub could
not compare (deleted or empty) are left out.
