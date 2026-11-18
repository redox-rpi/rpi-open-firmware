# Boot time to login

The firmware's main indicator is the time **from power-on to `redox login:`**
on the release card. We want it to go down. The details and the next cuts
are in [#44](https://github.com/redox-rpi/rpi-open-firmware/issues/44).

## How it is measured

- **Start: power-on.** The Boot ROM prints nothing, so the time from
  power-on up to our firmware comes from the Boot ROM's timer. Our banner
  prints it: `from the Boot ROM: … timer N us`. After a power-on, this
  timer counts from the moment the chip came out of reset. With the
  Shelly plug, the switch-on time is a second check.
- **End: the `redox login:` prompt** on the serial line. The prompt has
  no newline. A capture tool that stamps whole lines gives it the time of
  the next line instead, which is about 2 s too late (see the correction
  in #44). The pi-tests capture writes text after 0.5 s without a newline,
  so it is right.
- **Value:** `timer` + (time of `redox login:` − time of our banner
  line). Measure on a cold start (power-on), not after a watchdog reset,
  and on the release card of the board (#56, #57).
- **Per board:** Pi 3B+ and Pi Zero 2 W, each on its own row.
- **Every firmware Pi test that boots to the login** notes the value in
  its report, so the trend comes from tests we run anyway.

## Values so far

| Date | Board, card | Firmware | Boot ROM | Firmware → login | **Power-on → login** | Test |
|---|---|---|---|---|---|---|
| 2026-11-17 | 3B+, minimal Redox (keyboard only), SD at 25 MHz | `release-0.1` + direct boot | about 0.31 s | 12.85 s | **about 13.2 s** | 470 |
| 2026-11-17 | 3B+, the same, SD at 50 MHz (PR #45) | + high speed | about 0.31 s | 10.96 s | **about 11.3 s** | 473 |
| 2026-11-18 | Zero 2 W, standard direct card (248 MB initramfs, not minimal) | `cfg-20261117c` | 1.01 s | 30.83 s | **about 31.8 s** | 492 |

The Zero's value is for the full standard card. Of it, 22.5 s goes to
reading the 248 MB initramfs. The minimal release card (#56) is the big
cut there.

## Where the time goes (3B+, Test 473)

| Part | Time |
|---|---|
| Boot ROM | 0.3 s |
| Firmware up to the kernel (SD card, kernel8.img, initramfs, devicetree) | 3.2 s, of which 2.0 s is the initramfs |
| Kernel, of which about 1.4 s is DEBUG lines on the 115200 baud serial line | 1.9 s |
| initfs services, of which `dwc2d` waits about 3 s for the LAN link ([redox-rpi#536](https://github.com/redox-rpi/redox-rpi/issues/536)) | 4.6 s |
| switchroot to the login | 1.1 s |

The next cuts, largest first, are in #44.
