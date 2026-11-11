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

