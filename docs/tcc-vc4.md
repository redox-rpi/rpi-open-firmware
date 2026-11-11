# TinyCC with a VC4 backend: plan

**Draft**, 2026-11-11. Work package of its own; background and the
alternatives in [vpu-compiler.md](vpu-compiler.md). The firmware work goes
on with GCC 6.2.1 regardless.

## Goal

One small compiler, TinyCC (TCC), that builds the VPU part of this
firmware (`bootcode/`), first on the build host and later on the Pi under
Redox, where TCC already runs (redox-rpi experiment `tcc`, #247).

## Decided (maintainer and firmware session, 2026-11-11)

| Topic | Decision |
|---|---|
| Where | Its own repository `redox-rpi/tinycc`, a real fork of TinyCC `mob`, not a folder in this repository. The firmware build points at a fixed commit, like `vc4-toolchain` today. |
| Until then | Branch `vc4` in a clone on homenodellm (`~/rbc-build/tcc-vc4`), based on `mob` 70adbb12, the same commit the Redox `tcc` recipe uses. |
| Who creates the repo | The maintainer, once the spike shows the approach works. |
| Redox patches | The four patches of the `tcc` experiment move into the same fork later, as a separate step. |
| Licence | TCC is LGPL-2.1. The VC4 encoder is written from the vc4-toolchain binutils description (`cpu/vc4.cpu`, GPLv3+), so the VC4 files are marked GPLv3+ and the fork as a whole becomes GPLv3+. Fine for the firmware (GPLv2+ sources, built by the tool, not linked with it). |
| First step | A time-boxed spike: integer subset only (load/store, integer arithmetic, compares, branches, calls, stack frame), enough to compile `bootcode/fat.c`. No float, vector, inline asm or `.S`. |
| Measure | Size against `vc4-elf-gcc -Os`, and a host test. |

## How the backend is built

- Target `TCC_TARGET_VC4`, files `vc4-gen.c` (code generator), `vc4-link.c`
  (relocations), later `vc4-asm.c` (assembler). The C67 target is the
  template for the plumbing (32-bit, no assembler), `riscv64-gen.c` and
  `arm-gen.c` for the code generator.
- Output: ELF objects (`EM_VIDEOCORE3`, 137, as GCC's) that link with
  `vc4-elf-ld` and the existing linker scripts. TCC's own linker comes
  later.
- ABI as GCC's VC4 port, so objects mix: arguments in r0–r5, more on the
  stack at the caller's `sp`; result in r0 (r0:r1 for 64 bit); structs
  returned through a hidden pointer in r0; r6–r23 saved by the callee;
  `sp` r25, `lr` r26.
- Frame: `stm r6-r7,lr,(--sp)`, r7 as frame pointer, r6 as scratch;
  allocator registers r0–r5.
- Instructions used first: 16-bit ALU (register and 5-bit immediate),
  48-bit `mov`/`add` with a 32-bit immediate, 32-bit loads and stores with
  a 12-bit offset, `cmp` plus conditional branch, `bl` with
  `R_VC4_PCREL27_MUL2`, `bl rN`, conditional `mov` for compare results,
  `div.ss`/`div.uu` (the VPU has a divider).
- Every instruction form is checked automatically: the fork's encoder
  test prints each form as assembler text and as bytes, `vc4-elf-as`
  assembles the text, and the bytes must match (`tests/vc4/`).

## Milestones

| # | What | Done when | Effort |
|---|---|---|---|
| M0 | Plan (this page), target plumbing, encoder with the encoder test | `vc4-tcc` builds; every emitted form matches `vc4-elf-as` | 1 day |
| M1 | Simple functions: arithmetic, locals, `if`/loops, calls, globals | small test files compile, link with `vc4-elf-ld`, disassemble cleanly | 2–3 days |
| M2 | **Spike target:** `bootcode/fat.c` | compiles, links into the bootcode build, size compared with GCC, host test passes | 2–3 days |
| | *Spike result and go/no-go* | section "TCC spike result" in [vpu-compiler.md](vpu-compiler.md) | |
| M3 | The rest of the C in `bootcode/` (sd, sdram, printf, monitor, main) | bootcode built with TCC (C) plus GNU as (`.S`) boots on the Pi | 1 week |
| M4 | Assembler: `vc4-asm.c` for `start.S`, `go-test.S` | no GNU as needed | 1–2 weeks |
| M5 | Linker: VC4 relocations in TCC's linker, the linker-script parts we need (`-Ttext`, sections) | no binutils needed | 1 week |
| M6 | On the Pi: the fork with the Redox patches as the `tcc` recipe, plus `vc4-tcc` | firmware built on the Pi under Redox | days |
| M7 | Code size and speed (peephole, short branches, better constants) | open-ended, only if size or timing need it | – |

64-bit integers (`long long`) need add/sub with carry and come with M3 if
the code needs them (`fat.c` does not).

## Host test

Two levels:

1. **Encoder test** (M0): every instruction form against `vc4-elf-as`.
2. **Functional test** (M2): `fat.c` built by TCC runs against the FAT16
   and FAT32 images of the existing host test (`fat-host`) and must load
   the same files as the host build. That needs a VC4 simulator:
   `resim`/`vc4emul` from the vc4-toolchain (it was used to test the VC4
   GCC port). Building it on homenodellm needs the maintainer's OK (open
   question 2). Without it: the same test on the real Pi through the
   bootcode monitor, or GCC/TCC object comparison only.

## Open

1. Repository `redox-rpi/tinycc`: created by the maintainer after the
   spike (decided), or earlier so the branch is backed up on GitHub?
2. May the spike build `resim` (vc4emul, from the same vc4-toolchain
   sources as the GCC we use) in its own directory on homenodellm for the
   functional test?
3. Size goal: is "at most 1.5 to 2 times GCC's `-Os`" the bar for M2, or
   only "it fits and works"?
4. Upstreaming the VC4 backend to TinyCC `mob`: not planned now; the
   GPLv3 encoding source would make that a question for upstream.
5. `armstub8.S`: whether TCC's AArch64 assembler covers it (system
   registers, `eret`) is not checked; not part of this work package.
