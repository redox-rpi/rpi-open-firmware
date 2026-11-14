# `fixup.dat` (and `fixup_cd.dat`, `fixup_x.dat`, `fixup_db.dat`)

A small data file that always comes with its `start*.elf`:
`start.elf` with `fixup.dat`, `start_cd.elf` with `fixup_cd.dat`,
`start_x.elf` with `fixup_x.dat`, `start_db.elf` with `fixup_db.dat`.
`bootcode.bin` reads it together with the ELF file. A mismatched pair
doesn't boot.

[All Broadcom files](README.md)

## Facts

| | `fixup.dat` | `fixup_cd.dat` |
|---|---|---|
| Size | 7,381 bytes | 3,280 bytes |
| sha256 | `7853f2b51e4489f8…` | `64e1d7100658b031…` |
| For | `start.elf` (3.0 MB) | `start_cd.elf` (0.8 MB) |

## What is inside

Raspberry Pi describes it only as the file that adjusts `start*.elf` to
the memory split. The format is not documented. What the bytes show:

- no header, no magic number, no readable text;
- a byte stream with long runs of `03` and `0F`, starting with
  `0F 13 03 0F 03 0F …`, broken by rare other values (`41`, `47`, `70`,
  `EC`, …);
- both files start the same way and differ from about byte `0x28` on.

This fits a compressed list of places in `start*.elf` to adjust: short
codes for "skip so many words" and "patch this one". The ELF is linked
for a 256 MB memory with the VPU at the top (`start.elf` near 236 MB,
`start_cd.elf` near 250 MB). On a 1 GB Pi the VPU's part sits just below
1 GB, so every absolute address in the code must be moved; and the
fixup also tells the firmware where the ARM's memory ends. That is the
community reading; we haven't decoded the format.

## Why it matters for us

Our own firmware doesn't load `start*.elf`, so it needs no fixup. The
boot selector chains Broadcom's `bootcode.bin`, which reads the pair as
usual.
