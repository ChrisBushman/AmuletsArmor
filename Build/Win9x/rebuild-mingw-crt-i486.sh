#!/usr/bin/env bash
# Rebuilds mingw-w64-crt from source with -march=i486 -mtune=i486 and
# installs it over Ubuntu's packaged i686-w64-mingw32 runtime.
#
# Why: real Windows 95 hardware testing hit "invalid instruction" crashes
# at IDENTICAL bytes across every mingw-built binary tested (AA.exe,
# AAServer.exe, AALauncher.exe, AAScriptCompiler.exe -- binaries that
# share no application code at all), proving the fault was in mingw's own
# compiler-generated/runtime startup code, not ours. Root-caused via
# objdump to CMOV (opcode 0f 44), a Pentium Pro/P6-family instruction
# (1995+) absent on 486/Pentium/Pentium MMX hardware --
# i686-w64-mingw32-gcc defaults to -march=pentiumpro with no explicit
# -march at all.
#
# Passing -march=i486 to OUR OWN code (see WIN9X_VER_DEFINES in this
# repo's Build/Win9x/Makefile) fixes our object files, but not Ubuntu's
# prebuilt gcc-mingw-w64-i686 package: libmingw32.a's pseudo-reloc.o (the
# CRT's runtime pseudo-relocation support, called unconditionally by
# crt2.o/dllcrt2.o at process startup, before main() even runs) and
# several other libmingwex.a/libmingw32.a objects (printf/scanf float
# formatting, gdtoa, etc.) are prebuilt at the package's default
# -march=pentiumpro and never touched by our own -march flags. Confirmed
# via objdump: even a trivial MessageBoxA hello-world linked with our own
# -march=i486 still had CMOV in 17 different runtime functions pulled in
# from the prebuilt archives.
#
# The only real fix: rebuild mingw-w64-crt itself from the exact source
# Ubuntu's package is built from (fetched via `apt-get source mingw-w64`,
# so it matches the installed gcc-mingw-w64-i686/binutils-mingw-w64-i686
# versions exactly) with -march=i486 -mtune=i486, and install over the
# same /usr/i686-w64-mingw32 prefix the distro package already uses --
# every binary linked afterward picks it up automatically via the normal
# default link line, no per-object linker tricks needed. Verified: 0 CMOV
# and 0 MMX/SSE anywhere in a freshly relinked hello.exe, using nothing
# but a plain `gcc -o hello.exe hello.c`.
#
# Must run once (in CI, or in a local Docker dev loop) before any Win9x
# `make` in this repo -- and in the sibling AAServer/AAScriptCompiler/
# AALauncher repos, which each carry their own copy of this same script.
# Safe to re-run; takes a few minutes.
set -euo pipefail

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

# Enable source packages (needed for `apt-get source mingw-w64` below).
# Ubuntu 24.04's deb822 sources format ships with deb-src disabled by
# default; older one-line sources.list format ships it present but
# commented out. Try both; harmless if one doesn't match.
sudo sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/ubuntu.sources 2>/dev/null || true
sudo sed -i 's/^# deb-src/deb-src/' /etc/apt/sources.list 2>/dev/null || true

sudo apt-get update -qq
sudo apt-get build-dep -y -qq mingw-w64
sudo apt-get install -y -qq dpkg-dev autoconf automake libtool

cd "$WORKDIR"
apt-get source mingw-w64

cd mingw-w64-*/mingw-w64-crt
CC=i686-w64-mingw32-gcc CFLAGS="-O2 -march=i486 -mtune=i486" \
  ./configure --host=i686-w64-mingw32 --target=i686-w64-mingw32 \
    --prefix=/usr/i686-w64-mingw32 --disable-lib64 --enable-lib32
make -j"$(nproc)"
sudo make install

echo "mingw-w64-crt rebuilt and installed with -march=i486 -mtune=i486."
