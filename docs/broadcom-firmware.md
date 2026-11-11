# Broadcom's firmware: what is in it, per Pi model

What the closed Raspberry Pi firmware consists of, which variant each
model uses, and what each piece does. Sizes are from the release
redox-rpi uses, [raspberrypi/firmware](https://github.com/raspberrypi/firmware)
`1.20260915`. Which of these pieces our own firmware replaces:
[own-firmware.md](own-firmware.md); how redox-rpi's releases start:
[boot-chain.md](boot-chain.md).

![Boot chain per Pi model: Zero W, Zero 2 W, 3B+ and Pi 400](images/broadcom-per-model.svg)

## The models

| | Pi Zero / Zero W | Pi Zero 2 W | Pi 3B+ | Pi 400 |
|---|---|---|---|---|
| Chip | BCM2835 | RP3A0 (BCM2710A1 + 512 MB in one package) | BCM2837B0 | BCM2711C0 |
| ARM | 1 × ARM1176, 32-bit (ARMv6) | 4 × Cortex-A53, 64-bit | 4 × Cortex-A53, 64-bit | 4 × Cortex-A72, 64-bit |
| VPU | VideoCore IV | VideoCore IV | VideoCore IV | VideoCore VI |
| RAM | 512 MB | 512 MB | 1 GB | 4 GB |
| First boot code after the ROM | `bootcode.bin` on the SD card | `bootcode.bin` on the SD card | `bootcode.bin` on the SD card, or over USB or the network (in the ROM) | bootloader in the SPI EEPROM on the board |
| Main firmware | `start*.elf` + `fixup*.dat` | `start*.elf` + `fixup*.dat` | `start*.elf` + `fixup*.dat` | `start4*.elf` + `fixup4*.dat` |
| ARM start stub | none needed (one core) | `armstub8` built into `start.elf` | `armstub8` built into `start.elf` | `armstub8-gic` built into `start4.elf` (sets up the GIC-400) |
| Devicetree | `bcm2708-rpi-zero.dtb`, `bcm2708-rpi-zero-w.dtb` | `bcm2710-rpi-zero-2-w.dtb` | `bcm2710-rpi-3-b-plus.dtb` | `bcm2711-rpi-400.dtb` |
| Other blobs | Wi-Fi/Bluetooth chip firmware (Zero W) | Wi-Fi/Bluetooth chip firmware | Wi-Fi/Bluetooth chip firmware | Wi-Fi/Bluetooth chip firmware; USB controller (VL805) firmware loaded by the EEPROM bootloader |
| In redox-rpi | not supported (32-bit ARM; Redox needs 64-bit) | supported | supported, the test Pi | bring-up (Pi test row 325) |

The Wi-Fi and Bluetooth firmware belongs to the Cypress/Infineon radio
chip and is loaded by the operating system's driver, not by the boot
firmware. redox-rpi has no Wi-Fi driver yet.

## The pieces

### Boot ROM (all models)

Inside the chip, cannot be changed or replaced, and cannot be broken.

- **Zero, Zero 2 W, 3B+:** looks for `bootcode.bin` on the SD card's
  first FAT partition. The 3B+ also tries USB and the network when there
  is none (set at the factory). The ROM loads at most 128 KiB into the
  VPU's L2 cache at `0x80000000` and jumps to `0x80000200`.
- **Pi 4 and 400:** reads the bootloader from the SPI EEPROM. If the
  EEPROM is broken, it looks for `recovery.bin` on the SD card to
  rewrite it.

### `bootcode.bin` (not on the Pi 4, 400 and 5)

52 KB, runs in the VPU's L2 cache. Sets up the clocks and the SDRAM, then
loads `start*.elf` and `fixup*.dat`. It also does USB and network boot
(about a third of its register accesses are USB), and reads `config.txt`
for a few early settings (`gpu_mem`, `total_mem`, `start_file`). One file
for all VideoCore IV models.

On the Pi 4 and 400 the **EEPROM bootloader** has this job instead. It
also knows network, USB and NVMe boot, and has its own settings
(`rpi-eeprom-config`, for example `BOOT_ORDER`).

### `start*.elf` and `fixup*.dat` (the main firmware)

The real firmware, about 0.8 to 3 MB. It runs on the VPU on top of the
ThreadX real-time OS and **keeps running** after the ARM has started.

What it does at boot:

- reads `config.txt` and `cmdline.txt`;
- sets the clocks and voltages;
- splits the memory between the ARM and the GPU (`gpu_mem`);
- sets up HDMI and a framebuffer;
- loads the devicetree and its overlays, fills in memory size, MAC
  address and serial number;
- loads the kernel (or U-Boot) and the stub, and starts the ARM.

After boot it answers the ARM over the **mailbox**: temperature, clocks,
throttling, framebuffer, power domains, board data. It also runs the
video codecs, the camera pipeline and the 3D memory management, reached
over VCHIQ.

`fixup*.dat` is the relocation data that adjusts the matching
`start*.elf` to the memory split. It always comes in a pair with its
`start*.elf`.

**Variants:** `config.txt` picks one (`start_x=1`, `start_debug=1`), and
`gpu_mem=16` picks the cut-down one automatically.

| VideoCore IV (Zero, Zero 2 W, 3B+) | VideoCore VI (Pi 4, 400) | What | Size (IV) |
|---|---|---|---|
| `start.elf` + `fixup.dat` | `start4.elf` + `fixup4.dat` | standard | 3.0 MB |
| `start_x.elf` + `fixup_x.dat` | `start4x.elf` + `fixup4x.dat` | plus camera and more codecs | |
| `start_cd.elf` + `fixup_cd.dat` | `start4cd.elf` + `fixup4cd.dat` | cut-down: no 3D, codecs or camera, for `gpu_mem=16`; **redox-rpi uses this one** on the 3B+ and Zero 2 W | 0.8 MB |
| `start_db.elf` + `fixup_db.dat` | `start4db.elf` + `fixup4db.dat` | debug output | |

The Pi 400 image of redox-rpi uses `start4.elf` (2.3 MB) for now.

### ARM start stubs (`armstub*`)

A few hundred bytes of ARM code that run first on the ARM, in the most
privileged mode. They set the timer frequency and the SMP bit, drop to
the level the kernel expects, and park cores 1–3 in a spin table until
the kernel releases them. Source: `armstubs/` in
[raspberrypi/tools](https://github.com/raspberrypi/tools/tree/master/armstubs)
(BSD).

| Stub | For | Model |
|---|---|---|
| none | single core, the firmware starts the kernel directly | Zero, Zero W |
| `armstub7` | 32-bit kernel on a multi-core chip | Zero 2 W, 3B+ (redox-rpi does not use it) |
| `armstub8` | 64-bit kernel, `arm_64bit=1` | Zero 2 W, 3B+ |
| `armstub8-gic` | 64-bit kernel, plus GIC-400 interrupt controller set-up | Pi 4, 400 |

They are built into `start*.elf`. `armstub=<file>` in `config.txt` loads
a different one from the card. Our own `armstub8` is one of these
([own-firmware.md](own-firmware.md#m2-arm-start-and-armstub8)).

### Devicetree and overlays

Open source, from the Raspberry Pi Linux tree, one `.dtb` per model (see
the table above). Overlays (`overlays/*.dtbo`) change it at boot from
`config.txt`. For example, redox-rpi uses `disable-bt` (the PL011 UART
for the serial console) and `mmc` (the SD card on the Arasan
controller). The firmware adds the memory size, MAC address and serial
number before it starts the kernel.

### `LICENCE.broadcom`

The terms under which the closed files may be passed on. It has to come
with every card or archive that contains them.

## Which files redox-rpi puts on a card

| Model | Files from Broadcom's release |
|---|---|
| 3B+, Zero 2 W | `bootcode.bin`, `start.elf`, `fixup.dat`, `start_cd.elf`, `fixup_cd.dat`, `LICENCE.broadcom`, `bcm2710-rpi-3-b.dtb`, `bcm2710-rpi-3-b-plus.dtb`, `bcm2710-rpi-zero-2-w.dtb`, `overlays/disable-bt.dtbo`, `overlays/mmc.dtbo` (redox-rpi `scripts/rpi-firmware.sh`) |
| Pi 400 (bring-up) | `start4.elf`, `fixup4.dat`, `LICENCE.broadcom`, `bcm2711-rpi-400.dtb` (and the 4B and CM4 devicetrees), overlays; the bootloader is in the board's EEPROM |
