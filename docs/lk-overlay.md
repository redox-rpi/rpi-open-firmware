# librerpi/lk-overlay on the Pi 3B+

[librerpi/lk-overlay](https://github.com/librerpi/lk-overlay) (GPLv2+, last
commit 2026-06-20, 252 commits) is the newer rewrite by the same community.
Everything runs on the little kernel (LK), on the VPU and on the ARM.
Its boot chain:

1. **Stage 1** (`vc4-stage1`, our `bootcode.bin`). The SDRAM setup
   (`rpi-ddr2/autoram`) is newer than the 2019 code. Stage 1 loads stage 2
   as `/boot/lk.elf` from an ext2/ext4 partition: partition 2 of the SD card
   (`sdhostp1`), behind a FAT partition that holds only `bootcode.bin`.
   Builds for TFTP, USB and SPI exist too.
2. **Stage 2** (`vc4-stage2`, `lk.elf`). It stays running on the VPU, with a
   small **mailbox server** (`mailbox-property-server`). It starts the ARM
   in **64-bit mode** on BCM2837 (it sets `ARM_C0_AARCH64`) with a built-in
   ARM program. Optionally it uses Raspberry Pi's `armstub8`, which parks
   cores 1–3 in the spin table.
3. **ARM stage** (`rpi3-test`, aarch64 LK with `linux-bootloader`). It
   loads `/boot/Image-aarch64`, `/boot/rpi3.dtb`, `cmdline.txt` and
   `initrd` from the same ext partition and starts Linux. By default Linux
   gets only one core; the `docs/booting-arm` notes list ways to fix that
   (`armstub8`, PSCI in U-Boot or LK).

That covers our step 1 (64-bit, spin table through `armstub8`) and part of
step 3. The gaps for us:

- **Mailbox:** the server answers only a few requests: firmware revision,
  ARM memory, clocks list, throttled, reboot and display notices. Missing
  are board model and revision, MAC address, serial number, clock rates,
  temperature, power states and the framebuffer. `rpi-firmwared` and
  U-Boot ask for some of these, so they would need new tags or changes on
  our side.
- **Network:** the USB Ethernet driver (`rpi-usb-nic`) only knows the SMSC
  LAN9514 of the Pi 2/3B (chip ID `0xec000002`), not the **LAN7515/LAN7800
  of the 3B+**. So no network boot from the VPU on our Pi. Linux or Redox
  would still drive the LAN7800 themselves later.
- **Pi 3B+ itself:** the README marks the Pi 3 as unknown for Linux, SD,
  USB and Ethernet. Nobody has reported a 3B+.
- **U-Boot:** not in the chain yet. It would be loaded as
  `Image-aarch64`, but U-Boot asks the mailbox for the board revision and
  the memory size, and only the memory size is answered today.
- **Build (2026-11-05):** officially with Nix, which homenodellm doesn't
  have. It also builds without Nix, with our VC4/ARM toolchains and the
  host's aarch64 GCC: `redox-rpi/build-lk-overlay.sh` in our fork, with
  lk-overlay pinned at `50c95c1`, takes about a minute. Three local changes
  are needed:
  - The libgcc path was a fixed `/nix/store` path; it now comes from the
    compiler.
  - The Pi 2 ARM program needs C++ headers we don't build, so it gets a
    placeholder. A BCM2837 never starts it.
  - An include directory so stage 2 finds the embedded ARM programs.

  Result: stage 1 is 50 KB, stage 2 is 0.9 MB, the 64-bit ARM stage is
  76 KB. Their VC4 GCC is 6.2.1-based too. Card image in their layout
  (`redox-rpi/make-sdcard-lk.sh`): FAT with `bootcode.bin`, ext4 with
  `/boot`. The ext4 is made without the newest e2fsprogs features, which
  LK's driver doesn't know. Our 64-bit test program runs in QEMU.
  - **Pi test #251 (2026-11-05):** lk-overlay's stage 1 starts on the 3B+
    and prints one line (`platform_early_init: b`, 1 ms after the start),
    then nothing more. The next step in the code writes to the USB
    controller (`USB_DCTL`). That write most likely hangs the bus because
    the USB power domain is still off after the boot ROM; rpi-open-firmware
    turns that domain on first. Patch `redox-rpi/lk-patches/0001` skips the
    write and adds markers.
  - **Pi test #253 (2026-11-05, 32 GB SDHC card): the VPU starts the ARM
    in 64-bit mode.** With the USB write skipped, stage 1 gets through
    everything: markers `b1`, `b2` and `c`, PLLs, 1 GB SDRAM at 398 MHz
    (memory test passed), the SD card in 4-bit mode at 25 MHz, and stage 2
    from ext4. Stage 2 starts LK on the VPU, sets up composite video and
    the USB PHY, then `detected a bcm2837`, `enabling aarch64`, ARM clock
    and bridge up, and then waits for mailbox requests. On the ARM, LK
    starts in 64-bit mode on several cores (`welcome to lk/MP`) and reads
    the devicetree that stage 2 put at `0x1000000`. Its loader then fails
    with `mount failure: -2` (`ERR_NOT_FOUND`): upstream's `rpi3-test` is
    built without an SD card driver (`CONFIG_SD_BOOT := 0`), so the
    partition doesn't exist on the ARM side. Two lines in stage 2 print
    impossible clock values (`PLLC_PER at 9088281640219705594MHz`), most
    likely 64-bit numbers in the VPU's `printf`; the clocks themselves work.
  - For #254, `rpi3-test` is built with `CONFIG_SD_BOOT=1`. Its SD driver
    is C++, so `build-toolchain.sh` now also builds an `aarch64-none-elf`
    GCC with C++ (the host's aarch64 compiler has only C, and installing
    more needs root).
  - **Pi test #254 (2026-11-07): the ARM side finds the card, but larger
    reads fail.** The VPU part runs as in #253. On the ARM, LK's SD driver
    finds the 32 GB card in 4-bit mode, reads the partition table, mounts
    the ext4 partition (no `mount failure`) and reads a 576-byte file. Then
    reading the 35 KB devicetree fails: 136 `transfer error on FIFO word N`
    lines, `Transfer error, status: 0x20`, and the read of sector 73520
    gives up after 4 s. The log shows `vpu clock is 0, cdiv 0`. Cause:
    only the VPU code in `platform.c` sets `vpu_clock`. On the ARM it stays
    0, so the driver writes `SH_CDIV = 0` and never switches to the data
    clock: the card runs at core/2, about 125 MHz, instead of 25 MHz.
    Commands survive that, multi-block data doesn't. Patch
    `redox-rpi/lk-patches/0002` uses the 250 MHz that stage 2 leaves
    ([rpi-open-firmware#1](https://github.com/redox-rpi/rpi-open-firmware/pull/1)).
    QEMU can't check it, because its raspi3b machine puts the card on the
    EMMC controller, not SDHOST.
  - **Pi test #268 (2026-11-07): SD reads fixed, the loader stops at the
    devicetree.** `vpu clock is 250, cdiv 10`, 25 MHz, and the 35 KB
    devicetree was read with no transfer errors (#254: 136). Then
    `ERROR: fdt_open_into() == -3` (`FDT_ERR_NOSPACE`): the loader opens
    the devicetree into a 16 KB buffer, and the Raspberry Pi devicetree for
    the 3B+ is 35 KB. Patch `0003` gives it 128 KB of the 1 MB mapping. The
    next test (#269) loads that stage 2 over our serial monitor (see
    below), so lk-overlay iterations need no card swap any more: stage 2
    is one block at `0xc4000000` (296 KB, about 60 s over serial).
  - **Pi test #269 (2026-11-07): lk-overlay reaches our 64-bit payload.**
    Stage 2 with patch `0003`, loaded over our serial monitor (296 KB in
    52 s, CRC-32 matched) and started with `g c4000c72`, ran through
    `welcome to lk/MP`, the SD card at 25 MHz, the devicetree with no
    `fdt_open_into` error, `passing control off to linux`, and our test
    payload: EL1, `MIDR 0x410FD034`, `MPIDR 0x80000000`, devicetree at
    `0x3000000`. The same came from chaining lk-overlay's stage 1 from the
    monitor: its SDRAM set-up ran a second time without trouble, then
    stage 2 came from the card. So lk-overlay can boot a 64-bit kernel on
    the 3B+ with three small patches (`0001` to `0003`).

So lk-overlay is the better base for 64-bit and the mailbox. We would add
board/revision/MAC tags for U-Boot and `rpi-firmwared`. For network boot,
U-Boot on the ARM side would bring up the LAN7800 rather than the VPU.
