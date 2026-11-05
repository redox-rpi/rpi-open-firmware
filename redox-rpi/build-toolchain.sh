#!/bin/bash
# Build the two cross toolchains rpi-open-firmware needs, as a normal user
# (no root, no distro packages beyond a host gcc, make, flex, bison, texinfo):
#
#   vc4-elf-       Julian Brown's VideoCore IV port of binutils/GCC 6.2.1/newlib
#                  (github.com/itszor/vc4-toolchain), for bootcode.bin
#   arm-none-eabi- plain GNU binutils + GCC (C, C++, libgcc only) + newlib for
#                  the ARM chainloader (it uses newlib's headers)
#   aarch64-none-elf- binutils + GCC (C, C++, libgcc) for lk-overlay's
#                  64-bit ARM stage
#
# Usage: redox-rpi/build-toolchain.sh [WORKDIR]   (default: ../toolchains)
# Result: WORKDIR/prefix/bin on PATH, see redox-rpi/env.sh.
#
# Tested on Ubuntu 26.04 with host GCC 15 (homenodellm). The old GCC 6
# sources need an older C/C++ dialect, set in HOST_CFLAGS/HOST_CXXFLAGS.

set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${1:-$HERE/../toolchains}"
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
PREFIX="$WORK/prefix"
JOBS="${JOBS:-$(nproc)}"

VC4_TOOLCHAIN_REPO=https://github.com/itszor/vc4-toolchain.git
VC4_TOOLCHAIN_REV=2f57f09   # 2021-07-02, pins binutils-vc4, gcc-vc4, newlib

GNU=https://ftp.gnu.org/gnu
INFRA=https://gcc.gnu.org/pub/gcc/infrastructure
BINUTILS=binutils-2.42
GCC_ARM=gcc-13.3.0
GMP=gmp-6.1.0
MPFR=mpfr-3.1.4
MPC=mpc-1.0.3
NEWLIB=newlib-4.4.0.20231231

HOST_CFLAGS="-O2 -std=gnu11"
HOST_CXXFLAGS="-O2 -std=gnu++98"

export PATH="$PREFIX/bin:$PATH"

fetch() { # url
  local f; f="$WORK/src/$(basename "$1")"
  [ -f "$f" ] || { curl -sSfL -o "$f.part" "$1" && mv "$f.part" "$f"; }
}

mkdir -p "$WORK/src"
for t in $GMP.tar.bz2 $MPFR.tar.bz2 $MPC.tar.gz; do fetch "$INFRA/$t"; done
fetch "$GNU/binutils/$BINUTILS.tar.xz"
fetch "$GNU/gcc/$GCC_ARM/$GCC_ARM.tar.xz"
fetch "https://sourceware.org/pub/newlib/$NEWLIB.tar.gz"
(cd "$WORK/src" && sha256sum -c "$HERE/redox-rpi/toolchain.sha256")

unpack_prereqs() { # gcc source dir
  for p in $GMP $MPFR $MPC; do
    [ -d "$1/${p%%-*}" ] && continue
    tar -C "$WORK/src" -xf "$WORK/src/$p".tar.*
    ln -sfn "$WORK/src/$p" "$1/${p%%-*}"
  done
}

# --- vc4-elf ---------------------------------------------------------------
if [ ! -x "$PREFIX/bin/vc4-elf-g++" ]; then
  V="$WORK/vc4-toolchain"
  if [ ! -d "$V" ]; then
    git clone "$VC4_TOOLCHAIN_REPO" "$V"
    git -C "$V" checkout -q "$VC4_TOOLCHAIN_REV"
  fi
  git -C "$V" submodule update --init --depth 1 binutils-vc4 gcc-vc4 newlib
  unpack_prereqs "$V/gcc-vc4"

  common="--target=vc4-elf --prefix=$PREFIX --disable-nls --disable-werror"
  b() { rm -rf "$WORK/build-$1"; mkdir -p "$WORK/build-$1"; cd "$WORK/build-$1"; }

  b vc4-binutils
  CFLAGS="$HOST_CFLAGS" CXXFLAGS="$HOST_CXXFLAGS" "$V/binutils-vc4/configure" $common
  make -j"$JOBS" && make install

  # same options as vc4-toolchain/build-all.sh
  b vc4-gcc1
  CFLAGS="$HOST_CFLAGS" CXXFLAGS="$HOST_CXXFLAGS" "$V/gcc-vc4/configure" $common \
    --disable-ssp --enable-languages=c --without-headers --with-newlib \
    --disable-decimal-float --disable-threads --disable-libatomic \
    --disable-libitm --disable-libsanitizer --disable-libquadmath \
    --disable-lto --enable-sjlj-exceptions
  make -j"$JOBS" && make install

  b vc4-newlib
  "$V/newlib/configure" --target=vc4-elf --prefix="$PREFIX"
  make -j"$JOBS" && make install

  b vc4-gcc2
  CFLAGS="$HOST_CFLAGS" CXXFLAGS="$HOST_CXXFLAGS" "$V/gcc-vc4/configure" $common \
    --disable-ssp --enable-languages=c,c++ --disable-decimal-float \
    --enable-threads --disable-libatomic --disable-libsanitizer \
    --disable-libquadmath --disable-lto --enable-sjlj-exceptions --with-newlib
  make -j"$JOBS" && make install
fi

# --- arm-none-eabi -----------------------------------------------------------
if [ ! -x "$PREFIX/bin/arm-none-eabi-g++" ]; then
  cd "$WORK/src"
  [ -d $BINUTILS ] || tar xf $BINUTILS.tar.xz
  [ -d $GCC_ARM ] || tar xf $GCC_ARM.tar.xz
  unpack_prereqs "$WORK/src/$GCC_ARM"

  rm -rf "$WORK/build-arm-binutils"; mkdir "$WORK/build-arm-binutils"; cd "$WORK/build-arm-binutils"
  "$WORK/src/$BINUTILS/configure" --target=arm-none-eabi --prefix="$PREFIX" \
    --disable-nls --disable-werror
  make -j"$JOBS" && make install

  rm -rf "$WORK/build-arm-gcc"; mkdir "$WORK/build-arm-gcc"; cd "$WORK/build-arm-gcc"
  CFLAGS="-O2 -std=gnu17" "$WORK/src/$GCC_ARM/configure" --target=arm-none-eabi --prefix="$PREFIX" \
    --disable-nls --enable-languages=c,c++ --without-headers --with-newlib \
    --disable-libssp --disable-shared --disable-threads --disable-lto \
    --with-multilib-list=rmprofile,aprofile
  make -j"$JOBS" all-gcc all-target-libgcc
  make install-gcc install-target-libgcc
fi

# --- aarch64-none-elf (for lk-overlay's 64-bit ARM stage, which has C++) ----
if [ ! -x "$PREFIX/bin/aarch64-none-elf-g++" ]; then
  cd "$WORK/src"
  [ -d $BINUTILS ] || tar xf $BINUTILS.tar.xz
  [ -d $GCC_ARM ] || tar xf $GCC_ARM.tar.xz
  unpack_prereqs "$WORK/src/$GCC_ARM"

  rm -rf "$WORK/build-a64-binutils"; mkdir "$WORK/build-a64-binutils"; cd "$WORK/build-a64-binutils"
  "$WORK/src/$BINUTILS/configure" --target=aarch64-none-elf --prefix="$PREFIX" \
    --disable-nls --disable-werror
  make -j"$JOBS" && make install

  rm -rf "$WORK/build-a64-gcc"; mkdir "$WORK/build-a64-gcc"; cd "$WORK/build-a64-gcc"
  CFLAGS="-O2 -std=gnu17" "$WORK/src/$GCC_ARM/configure" --target=aarch64-none-elf --prefix="$PREFIX" \
    --disable-nls --enable-languages=c,c++ --without-headers --with-newlib \
    --disable-libssp --disable-shared --disable-threads --disable-lto
  make -j"$JOBS" all-gcc all-target-libgcc
  make install-gcc install-target-libgcc
fi

if [ ! -f "$PREFIX/arm-none-eabi/include/string.h" ]; then
  cd "$WORK/src"
  [ -d $NEWLIB ] || tar xf $NEWLIB.tar.gz
  rm -rf "$WORK/build-arm-newlib"; mkdir "$WORK/build-arm-newlib"; cd "$WORK/build-arm-newlib"
  "$WORK/src/$NEWLIB/configure" --target=arm-none-eabi --prefix="$PREFIX" \
    --disable-nls --disable-newlib-supplied-syscalls
  make -j"$JOBS" && make install
fi

vc4-elf-gcc --version | head -1
arm-none-eabi-gcc --version | head -1
aarch64-none-elf-gcc --version | head -1
