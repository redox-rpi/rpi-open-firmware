# VPU compiler: fewer dependencies (idea)

The VPU code builds with VC4 GCC 6.2.1 from
[itszor/vc4-toolchain](https://github.com/itszor/vc4-toolchain): a GCC and
binutils port from around 2017, plus newlib. It works, but it is a large,
old dependency that we don't control, and old GCC versions get harder to
build with new host compilers. The aim is fewer dependencies and more of
the chain under our control. Not started; nothing waits for it.

- **Step 1, cheap:** freeze the toolchain we have. Fixed sources and
  checksums already exist (`redox-rpi/build-toolchain.sh`); add a fixed
  build environment (a container on homenodellm) so the 40-minute build
  keeps working when the host compiler changes.
- **Step 2, our own small compiler:** a VideoCore IV backend for
  [QBE](https://c9x.me/compile/), with [cproc](https://sr.ht/~mcf/cproc/)
  as the C front end. QBE writes assembler text, and
  [vasm](http://sun.hasenbraten.de/vasm/) already has a VideoCore IV
  module (with vlink as the linker), so the hard part, encoding the
  irregular VC4 instructions (16, 32, 48 and 80 bits long), is done. A QBE
  backend is about 2,000-3,000 lines (its arm64 backend is that size).
  Our firmware is flat C without C++, which is the case cproc and QBE
  handle. The code would be slower than GCC's, which hardly matters for
  boot code.
- **Why not TCC:** TCC has its own assembler and linker, so a VC4 port
  would have to write the instruction encoding and the relocations as
  well as the code generator. More work, for weaker code.
- **What no small compiler gives:** the VPU's 16-lane vector unit (GCC
  doesn't use it either; it is written in assembler) and Rust on the VPU,
  which would need an LLVM backend, a project of its own.
- **When:** step 1 whenever the toolchain build breaks or a new build
  host comes. Step 2 when GCC gets in the way (a compiler bug, or people
  failing to build it), or as a self-contained project for a contributor.

