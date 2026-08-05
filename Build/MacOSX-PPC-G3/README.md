# PowerPC G3 / Mac OS X 10.3 "Panther" platform

A **separate** platform from `Build/MacOSX-PPC/` (which targets a G4 Tiger box
with Tigerbrew's gcc-14 + SDL). This one produces binaries that run on a
**PowerBook G3** (PowerPC 750, Mac OS X 10.3.9), and on G4/G5 too.

Why separate rather than a tweak of the Tiger build: the Tigerbrew gcc-14 C++
runtime (libstdc++/libgcc_s) is entangled with Tiger's 10.4 ABI — `$UNIX2003`
conformance variants, Tiger-only stdio globals, xlocale, and AltiVec — none of
which exist / can run on a G3. Instead this platform uses a **Panther-native
toolchain** so binaries link Panther's *own* system libstdc++/libSystem:

- **Compiler:** Apple's `gcc-4.0` / `g++-4.0` (Xcode Tools 2.5), not Tigerbrew gcc.
- **SDK:** `-isysroot /Developer/SDKs/MacOSX10.3.9.sdk -mmacosx-version-min=10.3`.
- **Codegen:** `-mcpu=750 -mno-altivec` (generic G3, no AltiVec/VRSAVE).

Result: `cpusubtype ppc750`, 0 AltiVec, links `/usr/lib/libSystem.B.dylib`
(v71, Panther) + the system Panther libstdc++ — no 10.4 symbols, nothing to
bundle for the C++ runtime. See `Makefile` for the exact flags.

## Prerequisite: build the G3-native SDL 1.2.15 + SDL_net 1.2.8

The Tiger build uses Tigerbrew's SDL (G4-tuned, AltiVec). The G3 build instead
uses SDL/SDL_net built from the **vendored source** (`Lib/SDL-1.2.15-src`,
`Lib/SDL_net-1.2.8-src`) with the same toolchain, installed once into
`~/aa-g3/prefix` (override with `GTET`-style `G3_PREFIX`). Run this **on the
Tiger box** (it has the 10.3.9 SDK and Apple gcc-4.0):

```sh
SDK=/Developer/SDKs/MacOSX10.3.9.sdk ; PREFIX=$HOME/aa-g3/prefix
F="-isysroot $SDK -mmacosx-version-min=10.3 -mcpu=750 -mno-altivec -O2"
LF="-isysroot $SDK -mmacosx-version-min=10.3 -arch ppc"

# SDL 1.2.15 (Quartz video; AltiVec explicitly off)
cd Lib/SDL-1.2.15-src && make distclean 2>/dev/null
CC=/usr/bin/gcc-4.0 OBJC=/usr/bin/gcc-4.0 ./configure --prefix=$PREFIX \
    --disable-static --enable-shared --disable-altivec --disable-video-x11 --without-x \
    CFLAGS="$F" OBJCFLAGS="$F" LDFLAGS="$LF"
gmake -j2 && gmake install    # the trailing sdl.m4 install error is harmless

# SDL_net 1.2.8 (just 4 .c files; link against the SDL just built)
cd ../SDL_net-1.2.8-src
for f in SDLnet SDLnetTCP SDLnetUDP SDLnetselect; do
    /usr/bin/gcc-4.0 $F $($PREFIX/bin/sdl-config --cflags) -c $f.c -o $f.o
done
/usr/bin/gcc-4.0 -dynamiclib -isysroot $SDK -mmacosx-version-min=10.3 -arch ppc \
    -install_name @executable_path/lib/libSDL_net-1.2.0.dylib \
    -compatibility_version 1.0.0 -current_version 1.8.0 \
    -o $PREFIX/lib/libSDL_net-1.2.0.dylib SDLnet.o SDLnetTCP.o SDLnetUDP.o SDLnetselect.o \
    -L$PREFIX/lib -lSDL -Wl,-framework,Cocoa
cp SDL_net.h $PREFIX/include/SDL/
ln -sf libSDL_net-1.2.0.dylib $PREFIX/lib/libSDL_net.dylib   # so -lSDL_net resolves
```

### Critical: `@executable_path` install names

The bundle (AmuletsArmor-Bundle's `merge.py --platform ppc-g3`) puts every
binary and a shared `lib/` under `Contents/MacOS/`, and there is **no launcher
wrapper that sets `DYLD_LIBRARY_PATH`** for AAServer (unlike the client, which
AALauncher launches with it). So the bundled libs must be found relative to the
binary. Set the SDL/SDL_net install names to `@executable_path/lib/` **before
building the apps** (the app link records whatever id the dylib carries):

```sh
P=$PREFIX/lib
install_name_tool -id @executable_path/lib/libSDL-1.2.0.dylib     $P/libSDL-1.2.0.dylib
install_name_tool -id @executable_path/lib/libSDL_net-1.2.0.dylib $P/libSDL_net-1.2.0.dylib
install_name_tool -change $P/libSDL-1.2.0.dylib \
    @executable_path/lib/libSDL-1.2.0.dylib $P/libSDL_net-1.2.0.dylib
```

Then `amulets-armor` and `AAServer` link `@executable_path/lib/libSDL*.dylib`,
which resolves to `Contents/MacOS/lib/` in the bundle for any launch method
(AALauncher, `run.sh`, Terminal, double-click). Verify with `otool -L`.

## Building

```sh
AA_PPC_HOST=aa-tiger ./Build/MacOSX-PPC-G3/remote-build.sh
```

Output: `Build/MacOSX-PPC-G3/build/amulets-armor` — `otool -hv` shows `ppc750`,
`otool -tvV | grep -c '…AltiVec…'` is 0, `otool -L` shows Panther system libs +
`@executable_path/lib/libSDL*`.
