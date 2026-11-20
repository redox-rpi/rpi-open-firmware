# Firmware release checklist

The checks for every rpi-open-firmware release. The release scope is set
by the milestones (v0.1.0, v0.2.0). The maintainer decides when a release
is due and approves the tag; nothing here starts a release on its own.

## Before a candidate

- [ ] The milestone's issues are done or explicitly moved.
- [ ] The release build (`make BOOT=1 HDMI=1`) has no warnings.
- [ ] The host tests pass: `bootcode/tests/fdt-test.sh`, `config-test.sh`,
      `fat-test.sh`, `armstub-header.sh --check`.

## Release card, on both boards

One card image for the Pi 3B+ and the Pi Zero 2 W (#57) with the minimal
Redox (#56). It is tested on each board from a cold start (power-on), with the
serial line captured and an HDMI monitor attached.

- [ ] **Boot to login** on the 3B+ and on the Zero 2 W.
- [ ] **HDMI:** the boot screen, then the Redox console (person check).
- [ ] **Boot time** from power-on to `redox login:`, per board, measured as
      in [boot-time.md](boot-time.md) and added to its table.
- [ ] **No avoidable error lines** (REQ-VPU-9, #61, #59). On the serial log
      from our banner up to `redox login:`, run:

      ```bash
      sed -n '/redox-rpi bootcode/,/redox login:/p' serial.log \
        | grep -Ei '\b(err|error|warn|warning|failed|not secure)\b'
      ```

      Pass: no match, except for lines on the allow-list in the pi-tests
      release check. The allow-list only holds lines that come from the
      test set-up. Each entry names the line, the reason and an open issue.
      Record the matches (or "none") in the report, per board.
- [ ] **Poweroff** (if #40 is in the release): `poweroff` halts the board,
      the monitor goes to standby, and GPIO3 wakes it.

## Release

- [ ] The release notes list the files (`bootcode.bin`, the card image),
      the boards, the limits, the tests above and the boot times.
- [ ] The maintainer approves the tag on the exact commit.
