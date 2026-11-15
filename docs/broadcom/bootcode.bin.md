# `bootcode.bin`

The first file the Pi loads. The Boot ROM in the chip finds it on the
first FAT partition of the SD card, copies it into the VPU's 128 KB L2
cache and runs it. Its job is to bring up the SDRAM and load the main
firmware, `start*.elf`. All models with VideoCore IV (Zero, Zero 2 W,
3B, 3B+) use the same file. The Pi 4, 400 and 5 have an EEPROM bootloader
instead.

[All Broadcom files](README.md)

## Facts

| | |
|---|---|
| Size | 52,624 bytes (`1.20260915`) |
| Format | raw VPU code, no header, no ELF, no checksum |
| Load address | `0x80000000` (L2 cache, VPU bus address) |
| Entry | `0x80000200` |
| First `0x200` bytes | all zero; the ROM jumps to offset `0x200`, where the code starts |
| End | zeros, no signature (the 3B+'s ROM doesn't check one) |
| Limit | the ROM loads at most 128 KiB |
| sha256 | `4245ec23b58158dc…` |

## What is inside

Almost no readable text; the strings that are there show what it does.

**Files it looks for** (as FAT 8.3 directory names, so it reads the
directory without long names):

| Name in the file | File | Why |
|---|---|---|
| `CONFIG  TXT` | `config.txt` | early settings, see below |
| `START   ELF` | `start.elf` (or the variant `config.txt` names) | the main firmware |
| `RECOVERYELF` | `recovery.elf` | a recovery image, if present |
| `TIMEOUT    ` | `timeout` | an old switch for USB boot: wait longer for the USB device |
| `UART       ` | `uart` | turn on its own serial log |

**`config.txt` keys it reads** (everything else is left to `start*.elf`):

| Key | What it does here |
|---|---|
| `gpu_mem`, `gpu_mem_256`, `gpu_mem_512`, `gpu_mem_1024` | memory for the VPU; decides where `start*.elf` goes |
| `total_mem`, `require_total_mem` | limit or check the memory size |
| `start_file`, `fixup_file` | load another firmware pair |
| `start_x`, `start_cd`, `start_db`, `start_debug` | pick the variant: `start_x.elf`/`fixup_x.dat`, `start_cd.elf`/`fixup_cd.dat`, `start_db.elf`/`fixup_db.dat` |
| `fixup_rc` | another fixup file name (not documented) |
| `boot_partition`, `tryboot_a_b` | boot from another partition; A/B boot with `tryboot` |
| `boot_ramdisk` | load a whole boot image (`boot.img`) instead of single files |
| `bootcode_delay` | wait before reading the card (slow cards) |
| `uart_2ndstage` | serial log from `bootcode.bin` and `start.elf` |
| `legacy_mapping`, `force_pvt` | not documented |

**Network boot:** a DHCP vendor class string
`PXEClient:Arch:00000:UNDI:002001` and `dhcp_reply`, `eth_addr`. It talks
to the LAN7515 on the board over USB, does DHCP and loads the files over
TFTP. This is how the test Pi boots from homenodellm.

**USB boot:** `get_config_descriptor`, `interface_desc`, `ep_in`, `ep_out`,
`max_lun`, `init_msd` (USB mass storage), `multi_tt` (USB hub). About a
third of its register references are USB (from our disassembly).

## What it does, in order

From our disassembly (behaviour only) and our own firmware, which does the
same steps:

1. Sets up clocks: the crystal at 19.2 MHz, PLLs for the VPU and the
   SDRAM.
2. Brings up the 1 GB LPDDR2 SDRAM. It first sets the size to 256 MB and
   skips its whole SDRAM set-up if `SD_CS` bit 0 (RESTRT) is already set,
   which is why chaining it after our own set-up fails.
3. Reads the SD card through **SDHOST** (not the Arasan EMMC controller),
   finds the FAT partition and `config.txt`.
4. If there is no card: USB, then network boot (the 3B+ has both turned
   on in its OTP from the factory).
5. Loads `start*.elf` into the top of the SDRAM, adjusted with
   `fixup*.dat` to the memory split.
6. Flushes the cache, copies a small hand-off routine into SDRAM and jumps
   into `start*.elf`.

## Related

Our own open `bootcode.bin` does steps 1–3 and 5–6:
[own-firmware.md](../own-firmware.md). The boot selector on the test Pi's
card contains Broadcom's `bootcode.bin` unchanged and chains it when
nobody presses Enter.
