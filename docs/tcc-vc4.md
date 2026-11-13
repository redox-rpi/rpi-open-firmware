# TinyCC with a VC4 backend: plan

**Draft**, 2026-11-11. Work package of its own; background and the
alternatives in [vpu-compiler.md](vpu-compiler.md). The parts of the backend, one by one: [tcc-vc4-components.md](tcc-vc4-components.md). The firmware work goes
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

## Status (2026-11-11, late night)

Branch `vc4` of redox-rpi/tinycc, draft
[PR #1](https://github.com/redox-rpi/tinycc/pull/1) against `main`. Native
`make test` still passes.

| Milestone | State |
|---|---|
| M0 | **done**: `vc4-tcc` builds; encoder test: 271 instruction forms, 0 failures against `vc4-elf-objdump` and `vc4-elf-as` |
| M1 | **done for integers**: ABI test (`tests/vc4/abi-sim.sh`) prints the same on the host and as TCC code in resim: variadic functions (also called from GCC code), 8 arguments, signed/unsigned narrow types, div/mod, shifts, compares, switch, loops, recursion, struct return, function-pointer tables, data relocations |
| M2 | **done (simulator)**: `fat.c` runs correctly in resim on FAT16 and FAT32 card images (`tests/vc4/fat-sim.sh`) |
| M3 | **builds**: every C file of `bootcode/` but `monitor.c` (inline asm, waits for M4) compiles with TCC, and the bootcode links (`tests/vc4/bootcode-build.sh`). **Runs on the Pi** ([pi-tests report 347](https://github.com/redox-rpi/pi-tests/blob/main/reports/347.md)): chained from the boot selector, the builds with TCC's `fat.c` (F) and with all C but `monitor.c` from TCC (T) answered `sd`, `ls`, `fl` (up to the 3 MB START.ELF), `sdr` and `crc` exactly as the GCC build (G). TCC's `sdram.c` did not run there (the chained build keeps the running SDRAM) |
| M4 | **done, Pi test passed**: `vc4-asm.c` assembles `start.S`, `go-test.S`, `chain-image.S` and `monitor.c`'s inline asm; `start.S` and `go-test.S` give the same bytes as gas, and `tests/vc4/asm-test.sh` checks about 90 instructions and the data directives against gas. The whole `bootcode.bin` builds with vc4-tcc (link: vc4-elf-ld): 24,942 bytes against 14,568 (text 1.71×). **Pi: pass** ([pi-tests report 349](https://github.com/redox-rpi/pi-tests/blob/main/reports/349.md)): every reply as the GCC build, go-test from TCC's assembler ran, G2 chained from A through TCC's trampoline; `fl START.ELF` (3 MB) took 10 s against 4 s. On the way: TCC ignored `attribute((section))` on a prototype (the trampoline went to `.text`), fixed in `tccgen.c`; start.S's `.word` was 16 bits (fixed by the open-firmware session, fat-boot 8c704d0) |
| M5 | **done, Pi test passed** ([pi-tests report 358](https://github.com/redox-rpi/pi-tests/blob/main/reports/358.md): the TCC-linked image answered as GCC's and #349, and both chains worked): TCC's linker links the whole bootcode (`-nostdlib -static -Wl,-Ttext=0x80000000 -Wl,--section-alignment=4 -Wl,--oformat=binary`), 24,972 bytes, `.text.start` at 0x80000000. New in the fork: `-Wl,-Map=FILE` (sections and symbols with final addresses) and code sections first for VC4. TCC has no linker scripts, so the firmware needs a small change (proposed to the open-firmware session, patch in the coordinator's notes): `.tramp` linked where stored and copied to 0x80019000 before use (`chain_trampoline` is position independent), `__start_X`/`__stop_X` symbols; `bootcode-build.sh LINK=tcc` checks linker.ld's `ASSERT`s on the map. In resim the ABI and FAT tests also pass when built and linked by vc4-tcc alone (own `sim-crt.S`, `sim-libc.c`) |
| M7 (step 1) | **experiment, branch `vc4-opt`** (not PR #1). Where the gap comes from: in resim (now with an instruction count and `bitflip`, so GCC's `fat.c` runs too) loading the 3 MB test file takes GCC's `fat.c` 12.1 M instructions and TCC's 65.0 M; the byte copy loop dominates (GCC 3 instructions per byte with registers and `addcmpbne`, TCC 17 with every variable in the frame and three jumps per iteration). Cheap fixes: loops with the condition after the body (one jump per iteration), 16-bit frame accesses (r7 64 bytes below the frame top), ALU immediates and `bmask`/`bset`/`bclr`, globals by `ld (pc+sym)` as GCC, store-to-load forwarding. Result: text 24,856 → 22,362 bytes (1.54×), `bootcode.bin` 24,972 → 22,472, `fat.c` 65.0 M → 49.6 M instructions (4.1× GCC). The rest needs register allocation for locals, which TCC does not have (a larger change). `ldsb`: GCC uses it, so it is safe on the Pi; only resim lacks it, and with unsigned `char` it hardly occurs, so it stays out. **Pi: pass** ([pi-tests report 371](https://github.com/redox-rpi/pi-tests/blob/main/reports/371.md)): `fl START.ELF` (3 MB) GCC 3.69 s, TCC before 9.43 s (2.56×), `vc4-opt` 8.18 s (2.22×), 13 % faster; all replies and crc32 values as before, chain back to GCC ok. Less than resim's 24 % because the SD word loop in `sd.c` and the waits gain less than `fat.c`'s copy loop |
| M7 (step 2) | **experiment, branch `vc4-opt` 68e5831f** (not PR #1). Register variables: before a function is compiled, a scan of its tokens weights each name by use (×8 per loop level); the 8 heaviest locals and parameters (4-byte scalars, address never taken, not in inline asm) live in r8–r15, saved by widening the prologue's `stm r6-r7,lr` in place, so functions without them cost nothing. On top: `++`/`--` on them is one `add`, a store into one retargets the instruction that computed the value, `p[i]` for bytes becomes `ldb rd,(ra+rb)`, the loop bottom becomes GCC's `addcmpb<cc>`, and the frame adjust is 4 bytes instead of 6. Result: text 22,362 → 21,524 bytes (1.48× GCC), `bootcode.bin` 21,636, `fat.c` in resim 49.6 M → 18.7 M instructions (1.55× GCC; the copy loop 14 → 6 instructions per byte, GCC 3). Checks: enc-test 290 forms, abi, asm, FAT16/FAT32 in resim, the bootcode build linked by vc4-tcc, and a new `tests2-diff.sh` (TinyCC's tests2 programs in resim against the step-1 build: 70 the same, 59 of them also as the host's `.expect`). **Pi: pass** ([pi-tests report 383](https://github.com/redox-rpi/pi-tests/blob/main/reports/383.md)): `fl START.ELF` (3 MB) GCC 3.68 s, step 1 8.16 s, step 2 **4.18 s (1.14× GCC)**, twice as fast as step 1; all replies and crc32 values as before, go-test ok, chain back to GCC ok. Better than resim's 1.55× because the Pi time also includes SD waits that are the same for both |

| `bootcode/` text | GCC 6.2.1 `-Os` | TCC (no optimisation) | factor |
|---|---|---|---|
| arm.c | 1,074 | 1,843 | 1.72 |
| clock.c | 336 | 420 | 1.25 |
| fat.c | 1,692 | 3,482 | 2.06 |
| fdt.c | 644 | 1,266 | 1.97 |
| libc.c | 44 | 178 | 4.05 |
| main.c | 1,004 | 1,410 | 1.40 |
| printf.c | 630 | 1,488 | 2.36 |
| sd.c | 1,428 | 2,539 | 1.78 |
| sdram.c | 2,410 | 3,831 | 1.59 |
| timer.c | 24 | 78 | 3.25 |
| uart.c | 248 | 410 | 1.65 |
| **total** | **9,534** | **16,945** | **1.78** |

`bootcode.bin` (`fat-boot` 088068d, `MONITOR=1`): 14,420 bytes with GCC,
16,210 with TCC's `fat.c`, 21,880 with all of the above from TCC. The
chain limit is 100 KB; the L2 region 108 KB.

Pi row 347 (passed) chained three builds over serial from the boot selector's
monitor: G (all GCC, the reference), F (`fat.c` from TCC), T (all but
`monitor.c` from TCC). It runs `sd`, `ls`, `fl` and `sdr` in each; F and T
printed exactly what G printed. Row 346 before it stopped at G: bootcode 088068d hung re-initialising the running SDRAM, fixed in fat-boot 8f11674.

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
- resim's `asr` returns 1 for every negative value (`||` for `|`); fixed
  in our resim build, noted in README-vc4.md.
- resim has no `ldsb` (GCC uses the 32-bit form on the Pi), so vc4-tcc
  loads signed bytes with `ldb` and `shl`/`asr` for now. Going back to
  `ldsb` saves 2 bytes per load, once a Pi test or a patched resim checks it.
- `stm r6-r7,lr` puts lr at the top in resim (the order is not
  documented). Normal functions only pair `stm` with `ldm`, so the order
  does not matter there; variadic functions save r6, r7 and lr one by one.

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
| M5 | Linker: VC4 relocations in TCC's linker, the linker-script parts we need (`-Ttext`, sections) | no binutils needed | 1 week — **done 2026-11-12** (8e1c7814), Pi row 358 passed |
| M6 | On the Pi: the fork with the Redox patches as the `tcc` recipe, plus `vc4-tcc` | firmware built on the Pi under Redox | days |
| M7 | Code size and speed (peephole, short branches, better constants) | open-ended, only if size or timing need it | – — **first step 2026-11-12** (experiment branch `vc4-opt`, 6d7440eb): text 1.71× → 1.54× GCC, `fat.c` in resim 65 M → 50 M instructions (GCC 12 M); on the Pi `fl START.ELF` 9.43 s → 8.18 s (GCC 3.69 s), row 371; **step 2** (68e5831f, register variables): 1.48×, resim 18.7 M (1.55×), Pi 4.18 s vs GCC 3.68 s (1.14×), row 383 |

64-bit integers (`long long`) need add/sub with carry and come with M3 if
the code needs them (`fat.c` does not).

## Host test

Two levels:

1. **Encoder test** (M0): every instruction form against `vc4-elf-as`.
2. **Functional test** (M2, `tests/vc4/fat-sim.sh`): `fat.c` built by TCC
   runs in `resim`/`vc4emul` (the simulator the VC4 GCC port was tested
   with; built in `~/rbc-build/tcc-vc4-tools/resim` with the maintainer's
   OK, 2026-11-11). The memory image holds a test driver and a FAT16 or
   FAT32 card image like `bootcode/tests/fat-test.sh` makes; every file
   must load with the size and checksum of the original. resim lacks
   `switch` and `bitflip #imm`, so GCC's `fat.c` cannot run there (the
   driver brings its own `printf` for the same reason).

## Open

1. Pi test passed (row 347). Next on the Pi: TCC's `sdram.c` from a cold start, which needs it in the boot selector card's own `bootcode.bin` (a card visit), or a later chain path that resets the SDRAM.
   `monitor.c` stays GCC until TCC's VC4 assembler (M4) handles its
   `__asm__("b %0")`; or should `monitor.c` get that one line from a `.S`
   helper so all C goes through TCC sooner?
2. Clean-room check: while working out the field layouts the spike
   session also read parts of the binutils CGEN description
   (`cpu/vc4.cpu`); nothing was copied and every encoding is verified
   black-box. Enough for LGPL, or should the layouts be re-checked
   against Herman Hermitage's public notes?
3. Size goal: is "at most 1.5 to 2 times GCC's `-Os`" the bar, or only
   "it fits and works"? Today 1.78× over all of bootcode, without any
   optimisation.
4. Upstreaming the VC4 backend to TinyCC `mob`: possible with LGPL; not
   planned now.
5. `armstub8.S`: whether TCC's AArch64 assembler covers it (system
   registers, `eret`) is not checked; not part of this work package.
