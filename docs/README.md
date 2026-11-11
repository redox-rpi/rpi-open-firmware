# Documentation

| Page | What |
|---|---|
| [models.md](models.md) | The Pi models and their hardware: chip, cores, memory, USB, network, Wi-Fi, our test boards |
| [broadcom-firmware.md](broadcom-firmware.md) | What is in Broadcom's firmware and its variants, per model, with a picture |
| [boot-chain.md](boot-chain.md) | Broadcom's boot pieces, how each redox-rpi release starts, the steps to the end goal, with pictures |
| [own-firmware.md](own-firmware.md) | Our firmware ([redox-rpi#230](https://github.com/redox-rpi/redox-rpi/issues/230)): milestones, monitor, boot selector, ARM start, findings |
| [test-results.md](test-results.md) | Every Pi test of this repository |
| [rpi-open-firmware-2019.md](rpi-open-firmware-2019.md) | Upstream's firmware on our Pi 3B+, and the 3B+ risks |
| [lk-overlay.md](lk-overlay.md) | librerpi/lk-overlay on our Pi 3B+, with our three patches |
| [librerpi.md](librerpi.md) | Every librerpi repository (lk-overlay, their continued rpi-open-firmware with about 100 hardware notes, rpi-tools, RP1, JTAG) and what it is good for |
| [forks.md](forks.md) | Forks of every source we use: glennra's November 2026 Pi 3B Linux boot without Broadcom firmware (DSI display, ARM handoff), and what the rest hold |
| [vpu-compiler.md](vpu-compiler.md) | Discussion: fewer toolchain dependencies (TinyCC fork with a VC4 backend, the existing LLVM VideoCore4 backend, alternatives, sources for the instruction encoding) |
| [tcc-vc4.md](tcc-vc4.md) | Draft plan: TinyCC fork with a VC4 backend (decisions, milestones, host test, open questions) |
| [socdump.md](socdump.md) | Register dumps as reference values for the firmware |
| [other-models.md](other-models.md) | Pi 4 and Pi 5 later |
| [upstream-README.md](upstream-README.md) | Kristina Brooks's original README, with its credits |

Command reference for our firmware: [bootcode/README.md](https://github.com/redox-rpi/rpi-open-firmware/blob/bootcode/bootcode/README.md) on branch `bootcode`.
