# `start.elf`

The main firmware: a whole operating system for the VPU. `bootcode.bin`
loads it, and it then boots the ARM. Unlike `bootcode.bin` it **keeps
running** on the VPU after the ARM has started. It answers the ARM's
mailbox requests and runs the display, the video codecs, the 3D memory
management and audio. This is the standard variant. redox-rpi's cards
carry it, but with `gpu_mem=16` the cut-down
[`start_cd.elf`](start_cd.elf.md) is the one that runs.

[All Broadcom files](README.md)

## Facts

| | |
|---|---|
| Size | 3,023,104 bytes (`1.20260915`) |
| Format | ELF32, executable, machine "Broadcom VideoCore III" (number 137, also used for VideoCore IV) |
| Entry | `0xcec00200` |
| Linked at | `0x0ec00100`–`0x0f2f12dc` (about 236–243 MB), for a 256 MB memory layout; `fixup.dat` moves it to where the memory split puts it |
| Build | `VC_BUILD_ID_VERSION a86983925695a7e63166327d7c002d64040ed31d (clean)`, Sep 14 2026 14:50:41, user `dom`, host `buildbot`, platform `raspberrypi_linux`, variant `start` |
| OS | ThreadX/SMP VideoCore G5.3.5.2 (SP1), Express Logic (runs on both VPU cores) |
| Compiler | not stated in the file; reportedly Synopsys MetaWare |
| sha256 | `32f990801e7765ba…` |

## Layout in memory

| Section | Size | What |
|---|---|---|
| boot area at `0xcec00100`, `0xcec00200`, `0xcec01e00` | 256 B + 6.5 KB + 512 B | entry code and data, in the uncached alias (`0xC0000000` + address) so it can run before the cache is set up |
| `.text` | 2.42 MB | code |
| `.rdata` | 255 KB | constants and strings |
| `.data`, `.sdata` | 200 KB | initialised variables |
| `.bss`, `.sbss` | 4.05 MB | zeroed variables, buffers, heap |
| `.stack`, `.stack2` | 2 KB each | one start stack per VPU core |
| `.tls`, `.guard1` | small | per-thread data, a stack guard |
| `.drivers`, `.drivers_base` | 240 B | table of the built-in drivers (60 pointers) |
| `.init.vchiq_services` | 36 B | VCHIQ services started at boot |
| `.init_mmal_ril_parameter_table` | 3.8 KB | parameters of the MMAL/OpenMAX components |
| `.init.vc_debug_sym` | 1.2 KB | symbol table for Broadcom's `vcdbg` debug tool (address, size, name of key variables such as the log buffers) |
| `.text.firmware_rev`, `.text.bootloader_state` | 16 B each | the firmware revision the mailbox reports, and the state handed over from `bootcode.bin` |

At run time it takes the top of the SDRAM: the `gpu_mem` part (76 MB on a
1 GB Pi by default). The ARM gets everything below it.

## What is inside

The file still holds the paths of 136 of its source files. They show the
structure of Broadcom's firmware source tree:

| Part of the tree | Files | What it is |
|---|---|---|
| `vcfw/rtos/threadx` | 5 | ThreadX and Broadcom's wrapper (`vcos`) |
| `vcfw/drivers/chip/vciv/2708/…` | 48 | drivers for every block of the chip: `arm`, `clock`, `sdram`, `power`, `gpio`, `i2c`, `spi`, `usb`, `sdhost`, `sdhost_arasan`, `hdmi`, `dsi`, `dpi`, `pixelvalve`, `transposer`, `pwm_audio`, `pcm`, `systimer`, `intctrl`, `otp_key`, `multicore_sync`, `arbiter`, `axi_mon`, `cache`, `resetctrl`, `tectl`, `vrf` (vector unit), and for the media blocks `v3d`, `vce`, `codec`, `cabac`, `h264pwr`, `mci`, `unicam` |
| `helpers/arm_loader` | 3 | loads `config.txt`, the kernel, the initramfs and the devicetree, and starts the ARM (`arm_loader.c`, `arm_ldconfig.c`, `arm_display.c`) |
| `applications/vmcs` | 5 | the services the ARM talks to: `gencmd` (the `vcgencmd` commands), `audioserv`, `vcwatchdog` |
| `interface/vchiq_arm` | 2 | VCHIQ, the message queue between ARM and VPU in shared memory |
| `interface/mmal` | 8 | MMAL, the media interface on top of VCHIQ |
| `middleware/openmaxil` | 19 | OpenMAX IL components: `camera`, `isp`, `video_decode`, `video_encode`, `video_render`, `image_encode`, `resize`, `egl_render`, `hvs`, … |
| `codecs/video`, `codecs/image` | 30 | hardware video decoding (H.263, H.264, MPEG-4, MPEG-2, VC-1), H.264 encoding, JPEG en/decoding |
| `middleware/khronos` | 3 | the VPU side of the old closed OpenGL ES / EGL / OpenWF stack |
| `middleware/dispmanx`, `hdmi`, `vec` | 4 | display: layer manager, HDMI (EDID, CEC), composite video (VEC) |
| `middleware/powerman` | 2 | clocks, voltages, temperature, throttling |
| `middleware/audioplus` | 2 | audio to the jack (PWM) and to HDMI |
| `middleware/ISP`, `helpers/cam_alloc` | 2 | camera image processing |
| `filesystem` | 3 | FAT access to the boot partition, and file access for the ARM (`filesys_vchi`) |
| `middleware/dlloader` | 1 | loads further VPU code at run time (`dlopen`) |

**`config.txt`:** this file knows all the keys of the official
documentation and many that aren't in it (about 1,500 key-like strings; not all are
keys), for example `arm_freq`, `over_voltage`,
`sdram_freq`, `hdmi_*`, `framebuffer_*`, `kernel`, `initramfs`,
`device_tree`, `dtoverlay`, `dtparam`, `enable_uart`, `boot_delay`,
`disable_splash`, `temp_limit`.

**Kernel names it looks for:** `kernel8.img`, `kernel8_rt.img`,
`kernel8-32.img`, `kernel7l.img`, `kernel7.img`, `kernel.img`; gzip
kernels are unpacked.

**Mailbox:** the property interface (channel 8) the ARM uses: board
model, revision, MAC address, serial number, memory split, clocks and
voltages, temperature, power domains, framebuffer, GPIO expander, boot
state. Which of these redox-rpi uses, and which our firmware answers
already: [own-firmware.md](../own-firmware.md),
[boot-chain.md](../boot-chain.md).

## What it does at boot

1. Starts ThreadX on both VPU cores and its drivers.
2. Reads `config.txt` (and `cmdline.txt`), sets clocks and voltages
   (on the 3B+ through the MxL7704 power chip over I2C).
3. Sets up HDMI and a first framebuffer, shows the rainbow splash
   (unless `disable_splash=1`).
4. Loads the devicetree, applies the overlays and `dtparam`s, and fills in
   memory size, MAC address, serial number and the command line.
5. Loads the kernel (for redox-rpi: U-Boot) and the initramfs, puts
   `armstub8` at address 0 and starts the four ARM cores.
6. Stays running as the mailbox and VCHIQ server.
