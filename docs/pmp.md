# Project management plan: rpi-open-firmware

How the open firmware sub-project is organised. It is a part of
[redox-rpi](https://github.com/redox-rpi/redox-rpi) today. Its rules are
the [redox-rpi PMP](https://github.com/redox-rpi/redox-rpi/blob/main/docs/process/pmp.md),
except where this plan says otherwise. The scope is small and the release
model is different. This plan is written so that the sub-project can later
become a project of its own (see [Becoming independent](#becoming-independent)).

This plan is owned by the Process session and changed by pull request.
The maintainer approves it.

Status: draft, 2026-11-19.

## Goal and scope

An open boot firmware for the Raspberry Pi. It replaces Broadcom's closed
`bootcode.bin`, `start.elf` and the ARM stub, and it boots a 64-bit
kernel: Redox first, any `kernel8.img` with an initramfs and a devicetree
in general.

In scope:
- the VPU firmware `bootcode.bin` (SDRAM, clocks, serial console, SD card,
  FAT, `config.txt`, mailbox, HDMI);
- the 64-bit ARM stub (`armstub8`) with PSCI (poweroff, reboot);
- the boards Pi 3B+ and Pi Zero 2 W (both BCM2837/RP3A0);
- a release card that shows the firmware with a minimal Redox.

Not in scope today: other boards (Pi 4, Pi 400, Pi 5 have other SoCs and
boot ROMs), the VC4 3D/video blocks, a general bootloader menu. The
toolchain (tinycc VC4 backend, resim, vc4-toolchain) is a separate work
package with its own owner (VC4 Tools); this project uses it.

## Roles

| Role | Who | Owns here |
|---|---|---|
| Maintainer | the maintainer | scope of each milestone, merges, release decision, tags |
| Owner | Open firmware session | design and code, the issues and the milestones' content, release candidates, the docs in this repository |
| Process | Process session | this plan |
| Project lead | coordinator session | status, `merge-next`, reporting to the maintainer |
| V&V | Pi test agent | Pi tests through the [pi-tests queue](https://github.com/redox-rpi/pi-tests/blob/main/queue.md), the release card tests, preparing person tests |
| Requirements | Requirements session | the `REQ-VPU-<n>` requirements and their trace |
| Quality | Quality session | the KPIs, above all the boot time, on the KPI dashboard |
| Toolchain | VC4 Tools session | the compiler and simulator the firmware is built and tested with |

## Releases: by scope, not by date

Redox-rpi releases every week and a release never waits for a feature.
**This project does the opposite: a release comes when its minimum scope
is done.** There is no target date and no time box.

- **One milestone per release** (`v0.1.0`, `v0.2.0`, ...). Its issues are
  the minimum requirements for that release. The owner proposes the
  content; the maintainer decides it. Moving an issue out of a milestone is
  the maintainer's decision.
- **Release candidate** (`-rc1`, `-rc2`, ...): the owner may build a
  candidate card at any time to see where the release stands, also before
  all issues are done. A candidate image is a test build; it gets no tag
  unless the maintainer approves one on the exact commit. Unlike
  redox-rpi, where Release and CI builds the candidates, the owner builds
  them here.
- **Release:** when all issues of the milestone are done (or moved by the
  maintainer) and the [release checklist](release-checklist.md) passes on
  both boards, the owner asks the maintainer. The maintainer approves the
  tag on the exact commit. The owner writes the release notes in the
  GitHub release: files, boards, limits, tests and boot times (see the
  checklist).
- **Review:** after each release the owner and the project lead write a
  short review on the milestone: what shipped, what moved, boot times.
- **Version numbers:** `v0.x.y`. A release with new milestone features
  raises the minor number, a release with only fixes the patch number.
  1.0 is a separate decision.
- **The release card** carries the firmware and a minimal Redox (no
  network, desktop or developer tools), one card for both boards. The
  Redox part comes from a redox-rpi build; it is not a redox-rpi release.

The plan for each release is its milestone and the
[project board](https://github.com/orgs/redox-rpi/projects/3).

## Quality rules that apply only here

- **No avoidable error lines** in the boot output of a release (REQ-VPU-9,
  [#61](https://github.com/redox-rpi/rpi-open-firmware/issues/61)). This
  rule is for firmware releases only, not for redox-rpi.
- **Boot time** from power-on to `redox login:` is the main indicator. It
  has no target; the aim is next to no boot time. How it is measured is in
  [boot-time.md](boot-time.md).
- Every release is tested from a cold start on both boards, with an HDMI
  monitor ([release checklist](release-checklist.md)).

## Work

- **Issues** live in this repository. Labels: `area:<part>` (boot, sdram,
  clock, power, mailbox, sd, build, hdmi, monitor, sim) and `needs:<what>`
  (person, pi, nothing). Requirements are issues named `REQ-VPU-<n>`.
- **Branches and pull requests** as in redox-rpi: code through a branch
  and a PR that names its issue; the default branch is `redox-rpi`. Only
  the maintainer merges code PRs here (the merge rights of the redox-rpi
  PMP give nobody else a right in this repository). Docs go straight to
  the default branch.
- **Pi tests** go through the pi-tests queue, like every other test on the
  shared lab Pis. The test Pi's boot files may be changed by a test if it
  restores them.
- **Docs:** what changes with the code stays here under `docs/`. Background
  reading is in the [wiki](https://github.com/redox-rpi/rpi-open-firmware/wiki);
  hardware reference is in the
  [redox-rpi wiki](https://github.com/redox-rpi/redox-rpi/wiki).

## Becoming independent

The maintainer expects to move this project out of redox-rpi sooner or
later. To keep that easy:

- Rules that differ from redox-rpi are written here, not in redox-rpi.
- The firmware does not depend on redox-rpi's build: it builds from this
  repository and the VC4 toolchain alone. Only the release card takes a
  minimal Redox image from redox-rpi.
- When it moves, this plan takes over the shared rules it links today
  (roles, merge rights, communication, change control from the redox-rpi
  PMP), and the open points below are decided.

Open points for that step: where tests run (own queue or still pi-tests),
own CI, the organisation or account it moves to, the licence notes of the
upstream code ([upstream-README.md](upstream-README.md)).

## Change control

Changes to this plan go through a pull request reviewed by the maintainer.
