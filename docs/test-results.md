# Pi test results

Every Pi 3B+ test of this repository. The Pi test agent runs them from the
[redox-rpi/pi-tests](https://github.com/redox-rpi/pi-tests) queue; each number
links its report.

| Test | Date | What | Result | Notes |
|---|---|---|---|---|
| [224](https://github.com/redox-rpi/pi-tests/blob/main/reports/224.md) | 2026-11-05 | rpi-open-firmware (2019) | partial | VPU part works: serial, 1 GB SDRAM tested, USB power, ARM core 0 runs the 32-bit chainloader. It finds the SD card, then crashes on a trap GCC 13 puts where the old code lacks a `return`. |
| [249](https://github.com/redox-rpi/pi-tests/blob/main/reports/249.md) | 2026-11-05 | rpi-open-firmware, fixed | pass | Up to the kernel hand-off: MBR, `cmdline.txt`, devicetree and `zImage` read, test payload in 32-bit mode on core 0. The old 64 MB card needs half the SD clock; a 32 GB card works at full speed. |
| [251](https://github.com/redox-rpi/pi-tests/blob/main/reports/251.md) | 2026-11-05 | lk-overlay | fail | Stage 1 prints one line, then hangs on a USB register write (USB power domain still off). Patch `0001` skips it. |
| [253](https://github.com/redox-rpi/pi-tests/blob/main/reports/253.md) | 2026-11-05 | lk-overlay | partial | The VPU starts the ARM in AArch64; LK runs on several cores. Its loader has no SD driver (`mount failure`). |
| [254](https://github.com/redox-rpi/pi-tests/blob/main/reports/254.md) | 2026-11-07 | lk-overlay | partial | ARM side finds the card; larger reads fail because the SD clock divider is 0. Patch `0002` (PR #1). |
| [265](https://github.com/redox-rpi/pi-tests/blob/main/reports/265.md) | 2026-11-07 | our bootcode, M1 | pass | VPU clock, UART, Elpida 1 GB at 398.4 MHz, memory test in 28 ms, heartbeat for 60 s. |
| [268](https://github.com/redox-rpi/pi-tests/blob/main/reports/268.md) | 2026-11-07 | lk-overlay | partial | SD reads fixed; the devicetree does not fit LK's 16 KB buffer. Patch `0003`. |
| [269](https://github.com/redox-rpi/pi-tests/blob/main/reports/269.md) | 2026-11-07 | our monitor | pass | Every command. lk-overlay's stage 2 loaded over serial reaches our 64-bit payload. Chaining Broadcom's `bootcode.bin` boots, but with only 240 MiB. |
| [313](https://github.com/redox-rpi/pi-tests/blob/main/reports/313.md) | 2026-11-10 | boot selector, `armstub8` | pass after a fix | e0dbedb hung chaining Broadcom's code after an SDRAM reset; b081bfd (chain before our SDRAM set-up) passed during the visit: 998 MiB, login. Our `armstub8` via `config.txt`: 998 MiB, 4 CPUs, pi-test 6/6 (that ours ran is not proven yet). |
| [320](https://github.com/redox-rpi/pi-tests/blob/main/reports/320.md) | 2026-11-10 | boot selector b081bfd | fail (b, c) | Normal boot passes; `boot` and the idle timeout hang after the monitor's chain. |
| [322](https://github.com/redox-rpi/pi-tests/blob/main/reports/322.md) | 2026-11-10 | watchdog reset by hand | pass | Two writes (PM_WDOG, PM_RSTC) restart the chip within 1 s; normal boot, 998 MiB, login. |
| [320b](https://github.com/redox-rpi/pi-tests/blob/main/reports/320b.md) | 2026-11-11 | boot selector 233c8c5 | pass | `boot` and the 300 s idle both reset through the watchdog and boot normally; 998 MiB, login, pi-test 6/6. |
| [332](https://github.com/redox-rpi/pi-tests/blob/main/queue.md) | queued | ARM start from our VPU code | – | Load `armstub8` and its test kernel over serial, `arm`, expect `4 cores up` (PR #7). |
