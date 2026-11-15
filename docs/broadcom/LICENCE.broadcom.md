# `LICENCE.broadcom`

The licence of the closed files: `bootcode.bin`, `start*.elf`,
`fixup*.dat`. 1,594 bytes of plain text, copyright Broadcom (2006) and
Raspberry Pi (Trading) Ltd (2015). It must travel with every card image
or archive that contains these files; redox-rpi's cards carry it.

[All Broadcom files](README.md)

## What it allows and asks

A BSD-style licence for binaries only:

- **Redistribution** in binary form is allowed **only unmodified**.
- **Use** only for developing for, running or using a Raspberry Pi
  (or a device made through element14's Raspberry Pi customisation
  service).
- Copies must reproduce the copyright notice and the disclaimer.
- Broadcom's name may not be used to promote derived products.
- No warranty, no liability.

## What it doesn't say

- Nothing about reverse engineering, disassembly or reading the files.
- Nothing about patents or about the hardware's documentation.
- No source code is offered.

## What follows for this project

- The files may go on redox-rpi's cards and into its releases as they
  are, with this file.
- They may not be patched, not even a byte (which is one reason the boot
  selector chains Broadcom's `bootcode.bin` unchanged rather than
  modifying it).
- They may be used only with a Raspberry Pi.
- Our own firmware is separate code under its own (open) licence and is
  not covered by this file.
