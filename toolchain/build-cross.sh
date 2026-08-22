#!/usr/bin/env bash
# Builds and installs the i686-elf cross-compiler this kernel builds
# with, following the standard OSDev "GCC Cross-Compiler" recipe:
# https://wiki.osdev.org/GCC_Cross-Compiler
#
# Why: the kernel used to build with the host's own `gcc -m32
# -ffreestanding -nostdlib`, approximating a freestanding i686 target
# on a hosted compiler -- correct output today, but exposed to silent
# codegen/ABI drift on a future host gcc upgrade, and required a
# 32-bit-multilib-capable host to build at all. i686-elf is a real "no
# OS" target triplet, so the resulting compiler is freestanding and
# libc-less by construction, with no host-specific assumptions to
# drift out from under it.
#
# Prerequisites (install once, requires root):
#   sudo apt-get install -y build-essential bison flex libgmp-dev \
#       libmpc-dev libmpfr-dev texinfo
#
# Usage: ./build-cross.sh
# Installs to $PREFIX (default ~/opt/cross). Re-run any time to
# rebuild from scratch (e.g. after a version bump below) -- safe to
# run repeatedly, existing source/build dirs are removed and
# re-fetched each time so there's no stale-checkout ambiguity.
#
# After it finishes, add this to your shell profile:
#   export PATH="$HOME/opt/cross/bin:$PATH"

set -eu

BINUTILS_VERSION=2.42
GCC_VERSION=14.2.0
TARGET=i686-elf
PREFIX="${PREFIX:-$HOME/opt/cross}"
JOBS="${JOBS:-$(nproc)}"

# SHA256 of the exact tarballs this script downloads, pinned so a
# compromised mirror or a corrupted partial download gets caught
# before it's extracted and compiled into the toolchain that produces
# the kernel binary -- computed directly from a real download of these
# same versions, not copied from a webpage.
BINUTILS_SHA256=5d2a6c1d49686a557869caae08b6c2e83699775efd27505e01b2f4db1a024ffc
GCC_SHA256=7d376d445f93126dc545e2c0086d0f647c3094aae081cdb78f42ce2bc25e7293

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

echo "==> Building $TARGET cross-compiler (binutils $BINUTILS_VERSION, gcc $GCC_VERSION)"
echo "==> Install prefix: $PREFIX"
echo "==> Work directory: $WORKDIR (removed on exit)"

mkdir -p "$PREFIX"
export PATH="$PREFIX/bin:$PATH"

cd "$WORKDIR"

echo "==> Downloading sources"
wget -q "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.gz"
wget -q "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.gz"

echo "==> Verifying checksums"
echo "${BINUTILS_SHA256}  binutils-${BINUTILS_VERSION}.tar.gz" | sha256sum -c -
echo "${GCC_SHA256}  gcc-${GCC_VERSION}.tar.gz" | sha256sum -c -

echo "==> Extracting sources"
tar xf "binutils-${BINUTILS_VERSION}.tar.gz"
tar xf "gcc-${GCC_VERSION}.tar.gz"

echo "==> Building binutils"
mkdir build-binutils
cd build-binutils
../"binutils-${BINUTILS_VERSION}"/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot \
    --disable-nls \
    --disable-werror
make -j"$JOBS"
make install
cd "$WORKDIR"

echo "==> Building gcc (C only, no libc -- this target has no OS to host one on)"
mkdir build-gcc
cd build-gcc
../"gcc-${GCC_VERSION}"/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --disable-nls \
    --enable-languages=c \
    --without-headers
make -j"$JOBS" all-gcc
make -j"$JOBS" all-target-libgcc
make install-gcc
make install-target-libgcc
cd "$WORKDIR"

echo "==> Done. Installed to $PREFIX"
echo "==> Add this to your shell profile if not already there:"
echo "        export PATH=\"$PREFIX/bin:\$PATH\""
echo "==> Verify with: $TARGET-gcc --version"
