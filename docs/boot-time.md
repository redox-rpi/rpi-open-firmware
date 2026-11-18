# Boot time to login

The firmware's main indicator is the time **from power-on to `redox login:`**
on the release card. We want it to go down, as close to no boot time as we
can. The details and the next cuts
are in [#44](https://github.com/redox-rpi/rpi-open-firmware/issues/44).

## Aim

There is no fixed target. The aim is to show that a system tuned from the
first instruction up boots with **next to no boot time**. Our estimate
from earlier discussions is that 2–3 s to the login should be possible.
We own every stage from the VPU's first instruction to the login, and
this project is meant to demonstrate that.

A rough budget for 2–3 s on the 3B+, compared with today (Test 473):

| Stage | Today | Possible | How |
|---|---|---|---|
| Boot ROM | 0.3 s | 0.3 s | fixed, in the chip |
| Firmware up to the kernel | 3.2 s | 0.3–0.5 s | a small initramfs (#56) at 50 MHz, `kernel8.img` at the same speed as the initramfs (it reads at 2.6 MB/s now, unexplained), HDMI set up while the SD card reads |
| Kernel | 1.9 s | 0.2–0.3 s | no DEBUG lines on the 115200 baud serial line (about 1.4 s now) |
| Services up to the login | 5.7 s | 1–1.5 s | `dwc2d` stops waiting for the LAN link ([redox-rpi#536](https://github.com/redox-rpi/redox-rpi/issues/536), about 3 s); start only what the login needs, the rest in the background after it |
| **Sum** | **11.3 s** | **about 2–2.5 s** | |

Most of the services and kernel time is in redox-rpi, not in the firmware.
The firmware's part is the first 0.5 s and the minimal card.

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
