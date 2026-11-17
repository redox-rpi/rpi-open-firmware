# Documentation

The documentation for reading is in the [rpi-open-firmware wiki](https://github.com/redox-rpi/rpi-open-firmware/wiki):
Broadcom's files, the boot chain, our own firmware, models, sources and
the other firmware projects. Hardware, one page per board, is in the
[redox-rpi wiki](https://github.com/redox-rpi/redox-rpi/wiki). Pi test
results are in [redox-rpi/pi-tests](https://github.com/redox-rpi/pi-tests/blob/main/queue.md#done).

What stays in the repository, because it changes with the code:

| Page | What |
|---|---|
| [bootcode/README.md](https://github.com/redox-rpi/rpi-open-firmware/blob/bootcode/bootcode/README.md) | Our VPU firmware: build, monitor commands, boot selector, files (branch `bootcode` until [PR #10](https://github.com/redox-rpi/rpi-open-firmware/pull/10) is merged) |
| [boot-selector.md](boot-selector.md) | The boot selector: a `bootcode.bin` for lab Pis that picks the boot path from the serial line; halt after poweroff; versions, install, build |
| [armstub/README.md](https://github.com/redox-rpi/rpi-open-firmware/blob/bootcode/armstub/README.md) | Our 64-bit ARM stub: build and test (branch `bootcode` too) |
| [redox-rpi/README.md](../redox-rpi/README.md) | Build scripts for the toolchains, lk-overlay and card images |
| [upstream-README.md](upstream-README.md) | Kristina Brooks's original README, with its credits and licence notes |
