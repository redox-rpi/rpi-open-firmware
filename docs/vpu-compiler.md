# VPU compiler: fewer dependencies (discussion)

State of the discussion of 2026-11-11 (maintainer and firmware session).
The firmware work (M2, M3) goes on with GCC regardless.

## Decided so far

- 2026-11-11: a time-boxed TCC spike (integer subset, target
  `bootcode/fat.c`) runs in its own session.
- The fork lives in its own repository,
  [redox-rpi/tinycc](https://github.com/redox-rpi/tinycc), not in this
  one: TCC also ships in the Redox images, and its upstream history and
  licence stay apart from the firmware. The firmware build will pin a
  commit of it.
- Licence: LGPL-2.1, as TinyCC upstream, so the backend could go upstream,
  and `libtcc1`, which TCC links into every program and so into our
  firmware, stays LGPL. We write the VC4 encoding ourselves from public
  documentation and check it against the VC4 GNU assembler's output; no
  tables or code are taken from binutils (GPLv3+).

## What we use today

| Part | Tool |
|---|---|
| VPU code (`bootcode/`, upstream's firmware) | VC4 GCC 6.2.1, GNU assembler and linker (binutils), newlib, all from [itszor/vc4-toolchain](https://github.com/itszor/vc4-toolchain) (around 2017) |
| ARM code (`armstub8`, test programs, upstream's chainloader) | ARM GCC (`aarch64-linux-gnu-`, `arm-none-eabi-`) |
| Build | `redox-rpi/build-toolchain.sh`: fixed sources and checksums, about 40 minutes, no root |

It works, but it is a large, old dependency that we don't control, it does
not run on the Pi, and old GCC versions get harder to build with new host
compilers. The aim: fewer dependencies, everything open, and ideally the
firmware built on the Pi itself.

## The idea: a TinyCC fork with a VC4 backend

[TinyCC](https://repo.or.cz/tinycc.git) (TCC, LGPL) is one small program
holding the C front end, the code generator, an assembler and a linker.

**For it:**

- **TCC already runs on Redox on the Pi** (redox-rpi experiment `tcc`,
  [#247](https://github.com/redox-rpi/redox-rpi/issues/247)). A VC4
  backend in that same TCC means the firmware can be built on the Pi,
  under Redox. No other option gets there as easily.
- One tool for both sides: the AArch64 backend for the ARM parts exists.
- Our firmware fits what TCC does well: plain C, no C++, a few files. Our
  own code is about 14 KB (the rest of the 66 KB monitor build is
  Broadcom's chained `bootcode.bin`), so even code 1.5 to 2 times larger
  than GCC's fits the 128 KB of L2 with room to spare.
- We would own it and could tune it to exactly what the VPU code needs.

**Against it, honestly:**

- TCC writes machine code directly, not assembler text. So the fork must
  encode the irregular VC4 instructions (16, 32, 48 and 80 bits long)
  itself, besides the code generator and the ELF relocations. A rough
  size: code generator 2,000–3,000 lines (like TCC's `arm64-gen.c`),
  assembler 1,000–1,500, relocations a few hundred.
- Weaker code: TCC does little register allocation. Fine for boot code;
  timing-critical loops (SDRAM training) need a check.
- The VPU's 16-lane vector unit stays assembler (GCC doesn't use it
  either).
- Not checked yet: whether TCC's AArch64 assembler handles all that
  `armstub8.S` needs (system registers, `eret`).

## Sources for the VC4 instruction encoding

| Source | Licence | Use |
|---|---|---|
| Opcode table in vc4-toolchain's binutils (`opcodes/vc4-*`) | GPLv3+ | the most complete machine-readable table; **not used** (it would make the fork GPLv3); the assembler serves only as a black-box check of our bytes |
| [hermanhermitage/videocoreiv](https://github.com/hermanhermitage/videocoreiv) | none stated, "non-commercial" disclaimer | the VideoCore IV Programmers Manual (wiki), `videocoreiv.arch`, `tinyasm`, an IDA plugin, small samples (`uart01`, `blinker01`); facts to read and check, not code to copy |
| [thubble's videocore-elf-dis](https://github.com/thubble/videocore-elf-dis) | see repo | a disassembler, to check our output |
| The internal disassembly of Broadcom's firmware | – | real instruction use, behaviour notes only |

## Alternatives

| Way | Effort | Dependencies | On the Pi / Redox | Notes |
|---|---|---|---|---|
| Freeze GCC 6.2.1 (container on homenodellm) | small | large, old | no | do it anyway, as fallback and reference |
| **TCC fork with a VC4 backend** | medium (weeks) | one program | **yes, TCC runs already** | own encoding needed (table above) |
| [QBE](https://c9x.me/compile/) + [cproc](https://sr.ht/~mcf/cproc/) + an assembler | medium | 3–4 tools | probably portable | QBE writes assembler text and has no assembler of its own, so it needs one (below); better code than TCC |
| Forward-port the VC4 port to current GCC/binutils | large | very large | no | only fixes the ageing |
| **LLVM VideoCore4 backend** (exists, see below) | medium to try, large to finish | huge (LLVM, clang, lld) | no | clang today; the only way to Rust on the VPU |

**Assemblers for the QBE way:**

- GNU as from vc4-toolchain: works, but keeps binutils as a dependency.
- [vasm](http://sun.hasenbraten.de/vasm/) has a VideoCore IV module
  (`vidcore`), but its licence is not free: redistribution only unmodified
  and non-commercial use; anything else needs the author's written consent
  (vasm manual, legal section). That is a poor base for a project that
  wants to replace everything closed. Earlier versions of this page
  proposed it; dropped.
- Our own, from the encoding sources above: the same work as the TCC
  assembler.

**Not for the VPU:** [maazl/vc4asm](https://github.com/maazl/vc4asm)
(GPLv3) assembles and disassembles code for the **QPUs**, the 3D shader
units of the VideoCore IV, not the VPU our firmware runs on ("It
assembles QPU code to binary, ELF or C source"). It has no linker. Useful
later: QPU shaders for M5 (3D) and graphics, and reading any QPU code in
Broadcom's firmware.

## LLVM: there is a VideoCore4 backend

The LLVM line has a longer history than this page first said:

| Repository | LLVM | Last change | Notes |
|---|---|---|---|
| David Given's backend, [announced on llvm-dev in 2014](https://lists.llvm.org/pipermail/llvm-dev/2014-May/073262.html) | 3.x | 2014 | "incomplete, but generates decent code in some cases" |
| [phire/llvm](https://github.com/phire/llvm) | 3.x | 2015 | separate early port |
| Kristina Brooks's `LLVM-VideoCore4` (fork of Given's, the original repo is gone; copy at [phanirajkiran/LLVM-VideoCore4](https://github.com/phanirajkiran/LLVM-VideoCore4)) | 3.x | 2016 | the rpi-open-firmware author's; she later moved to GCC |
| [LEA0317/LLVM-VideoCore4](https://github.com/LEA0317/LLVM-VideoCore4) | 8 to 14 | 2022–2023 | the backend carried forward release by release |
| [hiro-0317/llvm-videocore](https://github.com/hiro-0317/llvm-videocore) | **19.1.3** | 2024-11 | same author as LEA0317; `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD=VideoCore4` with clang and lld; tests compile C/C++ to VC4 assembler (`llvm_crc32`, `llvm_control1`, …); instruction encoding marked "WIP" in the history; no licence file of its own besides LLVM's |

**For it:** clang is a modern C front end (C11 and later, good
diagnostics), LLVM's code quality, lld as linker, and it is the only way
to Rust on the VPU: rustc needs an LLVM backend, plus a target
specification and `core` built for it.

**Against it:** a huge dependency (building LLVM with clang and lld takes
hours and many GB), it will not run on the Pi, one person maintains it,
and how far object output works (instruction encoding still "WIP") is
unknown. It is the opposite of "fewer dependencies", but a strong answer to
"modern compiler".

**Cheap check:** build hiro-0317's LLVM 19 on homenodellm (in the
background, an hour or more) and compile `bootcode/fat.c` to assembler and,
if the encoding allows, to an object. Compare with GCC. That tells us
within a day how complete the backend is.

## TCC spike result

State 2026-11-11, end of the first day. Details, milestones and open
questions: [tcc-vc4.md](tcc-vc4.md); code: branch `vc4` of
[redox-rpi/tinycc](https://github.com/redox-rpi/tinycc), draft
[PR #1](https://github.com/redox-rpi/tinycc/pull/1).

- **Builds:** every C file of `bootcode/` but `monitor.c` (inline asm)
  compiles with `vc4-tcc` and links into the bootcode build
  (`MONITOR=1`). Every instruction form agrees with
  `vc4-elf-as`/`vc4-elf-objdump` (271 forms, 0 failures).
- **Runs:** in the resim simulator, TCC's `fat.c` loads every file from
  FAT16 and FAT32 card images correctly, and an ABI test (variadic
  functions, many arguments, narrow types, switch, struct return) prints
  the same as the host build. On the Pi: queued as pi-tests row 346.
- **Size:** bootcode text 16,945 bytes against 9,534 with GCC 6.2.1 `-Os`
  (1.78×, no optimisation at all); `bootcode.bin` 21,880 against 14,420.
- **Open problems:** no `long long`, float, struct arguments or VLAs; no
  `stdint.h` of its own; no assembler (`.S` files, `monitor.c`'s one asm
  line); prologue and global addressing are large (M7).
- **Estimate for the rest:** an assembler for the three `.S` files (73
  lines) one to two weeks; TCC's own linker for VC4 about a week.
- **So far no blocker:** nothing points against the approach.

## Proposed next steps

The TCC spike has started as a work package of its own: plan and
decisions in [tcc-vc4.md](tcc-vc4.md).

1. **Freeze GCC** (container, or Nix's `pkgsCross.vc4` as librerpi does, see [librerpi.md](librerpi.md)), as fallback and reference.
2. **Check the LLVM backend** (above, about a day): if it is nearly
   complete, it changes the picture, at least for a modern compiler and
   Rust.
3. **A time-boxed TCC spike**, as a sub-project of its own, off the
   firmware's path: only the integer subset (load/store, arithmetic,
   branches, calls), enough to compile `bootcode/fat.c`. The host test
   for `fat.c` exists, so size and result can be compared with GCC's.
   If that goes well: the rest of `bootcode/`, then the `.S` files with
   TCC's assembler.
4. Decision by the maintainer: TCC (small, on the Pi), LLVM (modern, Rust), QBE, or GCC frozen. TCC and LLVM do not exclude each other: TCC for building on the Pi, LLVM for a modern compiler on the build host.
