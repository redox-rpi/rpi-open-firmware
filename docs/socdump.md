# Register dumps: socdump

A read-only Redox tool, similar to the Linux tools `pll-inspector` and
`pv_dumper` in [librerpi/rpi-tools](https://github.com/librerpi/rpi-tools)
(GPL-2.0, from the lk-overlay authors; we port the idea, not the code).

- **Why:** the closed firmware leaves its clock, PLL and HDMI settings in
  the registers. A dump under the closed firmware gives reference values
  for the open firmware and for HDMI (`vc4d`, [graphics.md](https://github.com/redox-rpi/redox-rpi/blob/main/docs/topics/graphics.md)),
  so the HDMI PHY and PLLH don't have to be guessed. A second dump under
  the open firmware shows what differs.
- **Where:** a new crate `tools/socdump`, not part of `rpi-firmwared`. That
  driver runs all the time and only does the mailbox; `socdump` is run by
  hand, maps the register pages with `physmap`, and only reads.
- **Steps:**
  1. Clock manager, PLLs A–D and H, GPIO pin functions on the BCM2837.
     Check the results against `/scheme/rpi-firmware/clocks`.
  2. Pixel valves, HVS, HDMI. It only reads a block whose clock is on.
  3. Reference dumps at 1080p and 720p on the 3B+ (a Pi test row).
  4. `socdump diff`.
- The rest of the other tools: `edid` (making EDIDs) helps with HDMI mode
  tests. The EEPROM and signing tools are for the Pi 4/5 boot EEPROM.
  `webusbboot` needs a USB device port, which the 3B+ doesn't have.
  [librerpi/vpu-x265](https://github.com/librerpi/vpu-x265) (a 2020 VPU
  vector-unit experiment, no license) doesn't matter for booting.


## State (2026-11-11)

- Step 1 is merged ([redox-rpi#227](https://github.com/redox-rpi/redox-rpi/pull/227)):
  PLLH reads 1485 MHz and `pllh_pix` 148.5 MHz at 1080p60. Dump in
  [tools/socdump/reference](https://github.com/redox-rpi/redox-rpi/tree/main/tools/socdump/reference).
- Steps 2 and 4 are in [redox-rpi#362](https://github.com/redox-rpi/redox-rpi/pull/362):
  `socdump display` (HVS channels and planes, pixel valves 0–2, HDMI core
  and HD blocks with PHY and timing) and `socdump diff A B`.
- Step 3: the 1080p dump under Broadcom's firmware (Pi test row 337,
  2026-11-12) is in PR #362 as
  `tools/socdump/reference/pi3bp-firmware-1080p-display.{txt,regs}`.
  Values for M4: pixel valve 2 control 0x00177005, h 1920/88/44/148, v
  1080/4/5/36; HDMI scheduler 0x000cb02b, PHY `ctl_0` 0x8c000000; PLLH
  1485 MHz, `pllh_pix` /10 = 148.5 MHz; HVS channel 1 scales the 1824x984
  framebuffer to 1920x1035 at y=22. The firmware's dynamic clocking moves
  PLLA/B/C and some dividers between dumps, so a `diff` always shows those.
  The 720p dump needs `hdmi_mode` in the SD card's `config.txt`, so it
  waits for a card visit.
- For M4 (HDMI without `start.elf`): the `.regs` files hold every value
  Broadcom's firmware programs for HDMI (PLLH, pixel valve 2, HDMI core
  and PHY, HVS). `socdump regs` under our firmware and `socdump diff`
  against them show what is still missing.
