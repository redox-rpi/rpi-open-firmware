# `start_cd.elf`

The cut-down main firmware. It does everything [`start.elf`](start.elf.md)
does to boot the ARM and answer the mailbox, plus the display, but has no
video codecs, no camera, no 3D and no OpenMAX/MMAL. The firmware picks it
on its own when `gpu_mem=16`, or with `start_cd=1`. **This is the one
redox-rpi runs** on the 3B+ and Zero 2 W: Redox uses none of the media
parts, and the ARM gets 60 MB more memory.

[All Broadcom files](README.md)

## Facts

| | |
|---|---|
| Size | 844,380 bytes (`1.20260915`), 28 % of `start.elf` |
| Format | ELF32, executable, machine "Broadcom VideoCore III" (137) |
| Entry | `0xcfa00200` |
| Linked at | `0x0fa00100`–`0x0fb7fd00` (about 250–252 MB), for a 256 MB layout with 16 MB for the VPU; `fixup_cd.dat` moves it to the top of the real memory |
| Build | same source as `start.elf`: `VC_BUILD_ID_VERSION a86983925695a7e63166327d7c002d64040ed31d (clean)`, Sep 14 2026 14:50:56, variant `start_cd` |
| OS | ThreadX/SMP VideoCore G5.3.5.2 (SP1) |
| Partner file | `fixup_cd.dat` (3,280 bytes); see [fixup.dat.md](fixup.dat.md) |
| sha256 | `ba2e8d34524bcac1…` |

## Layout, compared with `start.elf`

| Section | `start_cd.elf` | `start.elf` |
|---|---|---|
| `.text` (code) | 622 KB | 2.42 MB |
| `.rdata` (constants) | 119 KB | 255 KB |
| `.data` + `.sdata` | 67 KB | 200 KB |
| `.bss` + `.sbss` (zeroed) | 708 KB | 4.05 MB |
| `.drivers` table | 51 pointers | 60 pointers |
| `.init.vchiq_services` | 12 B | 36 B |
| `.init_mmal_ril_parameter_table` | missing | 3.8 KB |
| `.heap`, `.nuheap` | present (empty markers) | missing |

The boot area (`0xcfa00100`, `0xcfa00200`, `0xcfa01e00`) has the same
sizes as in `start.elf`, so the hand-off from `bootcode.bin` is the same.

## What is inside

47 source file paths remain (136 in `start.elf`).

**Drivers that stay** (`vcfw/drivers/chip/vciv/2708/…`): `arbiter`, `arm`,
`axi_mon`, `cache`, `clock`, `dpi`, `dsi`, `gpio`, `hdmi`, `i2c`,
`intctrl`, `multicore_sync`, `otp_key`, `pixelvalve`, `power`,
`pwm_audio`, `resetctrl`, `sdhost`, `sdhost_arasan`, `sdram`, `spi`,
`systimer`, `tectl`, `transposer`, `usb`, `vrf`, and the SD card control.
Also `clock_2711`, `hdmi_2711`, `pixelvalve_2711`: the source tree is
shared with the Pi 4 firmware, and these files are built in even here.

**Drivers that are left out:** `v3d` (3D), `vce` and `codec`, `cabac`,
`h264pwr`, `mci` (video codecs), `unicam` (camera), `pcm` (I2S audio).

**Other parts that stay:** ThreadX and `vcos`, `arm_loader` (config,
kernel, devicetree, ARM start), `vmcs` (gencmd), `dispmanx`, `hdmi`,
`vec` (display), `powerman` (clocks, voltages, temperature), `sysman`,
`dmalib`, FAT access and `filesys_vchi`.

**Left out:** OpenMAX IL, MMAL, the codecs, JPEG, the ISP, the Khronos
(OpenGL ES/EGL) server, `audioserv`, `dlloader`, `cam_alloc`.

**`config.txt`:** about 560 key-like strings (1,500 in `start.elf`); all
boot, clock, voltage, HDMI and devicetree keys are there.

## What redox-rpi needs from it

U-Boot reads board, memory and MAC address; `rpi-boot-stub` turns on USB
power; `rpi-firmwared` reads temperature and sets the ARM clock;
`rpi-fbd` asks for a framebuffer; `rpi-update` uses tryboot. All of this
is in `start_cd.elf`. Which of these requests our own firmware answers:
[own-firmware.md](../own-firmware.md).

## Seen on the Pi

At 1.4 GHz, `start_cd.elf` sets the MxL7704's core rail (register `0x14`)
to `0xD9` = 1.35625 V and the SDRAM rail (`0x13`) to `0xC8` = 1.25 V, up
from the power-on 1.200 V each (pi-tests row 409).
