# `start_x.elf`

The extended main firmware: everything in [`start.elf`](start.elf.md), plus
the camera and more codecs. `start_x=1` in `config.txt` picks it (on newer
systems, `camera_auto_detect=1` does too), together with `fixup_x.dat`.

[All Broadcom files](README.md)

**Not on redox-rpi's cards, and not analysed here.** `scripts/rpi-firmware.sh`
in redox-rpi copies only `start.elf` and `start_cd.elf`. This page lists what
is known from Raspberry Pi's documentation; figures from the file itself
would need the full release.

## What it adds over `start.elf`

- the camera pipeline: the CSI-2 receiver (Unicam) driven by the firmware,
  the ISP, and the old camera stack (`raspistill`, `raspivid`, MMAL camera
  component); the newer libcamera stack drives the camera from Linux and
  needs no `start_x`;
- more codec and image components reached over MMAL/OpenMAX;
- more memory for the VPU: it needs `gpu_mem` of at least 128 MB.

## redox-rpi and this file

Redox has no camera driver and uses no VPU codecs. The camera and codec
blocks are not documented by Broadcom.
