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
| Where | Its own repository [redox-rpi/tinycc](https://github.com/redox-rpi/tinycc), a real fork of TinyCC `mob`, not a folder in this repository. The firmware build points at a fixed commit, like `vc4-toolchain` today. |
| Branches | `mob` upstream unchanged, `main` our fork, `vc4` the backend; `vc4` reaches `main` by PR, merged by the maintainer. Build clone: `~/rbc-build/tcc-vc4` on homenodellm. |
| Redox patches | The four patches of the `tcc` experiment move into the same fork later, as a separate step. |
| Licence | LGPL-2.1, as TinyCC upstream, so the backend could go upstream and `libtcc1` stays LGPL. The VC4 encoding is our own, checked against the GNU assembler and disassembler as black boxes; no tables or code from binutils (GPLv3+). |
| First step | A time-boxed spike: integer subset only (load/store, integer arithmetic, compares, branches, calls, stack frame), enough to compile `bootcode/fat.c`. No float, vector, inline asm or `.S`. |
| Measure | Size against `vc4-elf-gcc -Os`, and a host test. |

## Status (2026-11-11, evening)

Branch `vc4` of redox-rpi/tinycc, draft
[PR #1](https://github.com/redox-rpi/tinycc/pull/1) against `main`; about
1,270 new lines (`vc4-enc.h`, `vc4-gen.c`, `vc4-link.c`, tests). Native
`make test` still passes.

| Milestone | State |
|---|---|
| M0 | **done**: `vc4-tcc` builds; encoder test: 271 instruction forms, 0 failures against `vc4-elf-objdump` and `vc4-elf-as` |
| M1 | **mostly done**: arithmetic, locals, loops, compares, calls with more than 6 arguments, function pointers, globals, division compile to correct-looking code that links |
| M2 | **compiles and links**: `fat.c` builds with `vc4-tcc`; the object links into the real bootcode build (`MONITOR=1`) with `vc4-elf-ld`. Functional test not run yet (needs a simulator or the Pi) |

| Size | GCC 6.2.1 `-Os` | TCC (no optimisation) |
|---|---|---|
| `fat.c` text | 1,672 bytes | 3,454 bytes (2.07×) |
| `bootcode.bin`, `MONITOR=1` | 14,120 bytes | 15,902 bytes with TCC's `fat.c` |

Where the size goes: every function saves r6, r7 and lr and builds a frame
(12 bytes, GCC often none); arguments are spilled to the frame and every
use reloads them; globals are addressed with a 48-bit `mov` plus a load
(GCC: one PC-relative load); constants of 32 and up take 4 bytes. These
are the M7 targets, none needs a design change.

Findings on the way:

- `vc4-elf-as` encodes `ld rX,(rY-n)` (negative offset) as a PC-relative
  load, a binutils bug. GCC avoids negative offsets; TCC's encoder writes
  the correct 32-bit form (checked with objdump).
- Variadic calls (`printf`) use the normal convention in GCC's port
  (r0–r5, then the stack), so TCC's calls to GCC's `printf` are right.
- TCC's `.eh_frame` is rejected by `vc4-elf-ld`; switched off for VC4, as
  for ARM.
- TCC has no `stdint.h`; the spike uses newlib's from the vc4 toolchain.
  For the Pi build a small freestanding `stdint.h` belongs in the fork.

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

1. Functional test: may the spike build `resim` (vc4emul, from the same
   vc4-toolchain sources as the GCC we use) in its own directory on
   homenodellm? The alternative is a test on the Pi through the bootcode
   monitor (a row for the Pi test agent).
2. Clean-room check: while working out the field layouts the spike
   session also read parts of the binutils CGEN description
   (`cpu/vc4.cpu`); nothing was copied and every encoding is verified
   black-box. Enough for LGPL, or should the layouts be re-checked
   against Herman Hermitage's public notes?
3. Size goal: is "at most 1.5 to 2 times GCC's `-Os`" the bar, or only
   "it fits and works"? Today 2.07× without any optimisation.
4. Upstreaming the VC4 backend to TinyCC `mob`: possible with LGPL; not
   planned now.
5. `armstub8.S`: whether TCC's AArch64 assembler covers it (system
   registers, `eret`) is not checked; not part of this work package.
