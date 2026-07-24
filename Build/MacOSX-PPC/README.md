# PowerPC / Mac OS X Tiger build

Native build path for PowerPC G3/G4 Macs running Mac OS X 10.4 "Tiger".
This is a **different** target from `Build/MacOSX/` (which is the modern
Intel/ARM CMake + Homebrew build). Tiger-era tools cannot run modern CMake
or Homebrew, so this path uses a plain Makefile instead.

## Toolchain

- **Hardware/OS:** real or emulated PowerPC G3/G4 Mac running Mac OS X 10.4.
- **Dev tools (preferred): [Tigerbrew](https://github.com/mistydemeo/tigerbrew)**,
  a Homebrew fork that still builds/bottles for Tiger/PPC. Confirmed working
  on a real PowerBook G4: installs a modern `gcc` (14.2.0 as of this
  writing, built as `powerpc-apple-darwin8`) that correctly accepts Apple's
  `-arch ppc` driver flag, plus `cmake`, `git`, and — critically — `sdl`
  (real SDL 1.2). This is a much better starting point than hunting down
  Xcode Tools 2.5 install media: modern GCC diagnostics, working C99 mode,
  none of the old-compiler quirks the rest of this doc used to warn about.
  `brew install sdl_net` for the one piece Tigerbrew doesn't ship by default.
- **Dev tools (fallback):** Xcode Tools 2.5 (ships `gcc-4.0`/`gcc-3.3`) if
  Tigerbrew isn't an option. Override `CC`/`CXX` accordingly
  (`make CC=gcc-4.0 CXX=g++-4.0`); everything else in this doc still applies.
- **Build driver:** the `Makefile` in this directory, run directly on the
  Tiger box (native build, not cross-compiled). `SDKROOT`/`-isysroot` is left
  empty by default since the Tiger box's own headers/SDK are already 10.4;
  it's there as a hook if this ever gets cross-compiled from a newer host.
- **Workflow:** you edit on your regular machine; `remote-build.sh` rsyncs
  the tree to the Tiger box over SSH and runs `make` there. No Xcode project,
  no GUI interaction needed.
- **SSH to an old Tiger `sshd`:** modern OpenSSH clients refuse Tiger's
  `ssh-rsa`/`ssh-dss`-only host keys and old KEX/cipher list by default.
  Add a `~/.ssh/config` `Host` entry with
  `HostKeyAlgorithms +ssh-rsa`, `PubkeyAcceptedAlgorithms +ssh-rsa`,
  `KexAlgorithms +diffie-hellman-group1-sha1,diffie-hellman-group14-sha1`,
  and `Ciphers +aes128-cbc,3des-cbc` rather than passing all of that on every
  invocation.

## Why not CMake

CMake 3.9.6 is available via Tigerbrew, but the repo's top-level
`CMakeLists.txt` requires 3.20+ (for the modern Intel/ARM/Windows configs,
which don't need to run on Tiger). Rather than lower that requirement for
everyone, this path stays a hand-written Makefile mirroring
`AA_CORE_SOURCES`/`AA_PLATFORM_SOURCES` from the top-level `CMakeLists.txt` —
short and stable enough to keep in sync by hand. Revisit if Tigerbrew ever
ships a newer CMake.

## Dependencies: SDL 1.2 / SDL_net, on the Tiger box

Do **not** reuse `Lib/SDL-1.2.15/` or `Lib/SDL_net-1.2.8/` from this repo —
those are Windows-only prebuilt `.lib`/`.dll` files, not source.

**Via Tigerbrew (preferred):**

```sh
brew install sdl sdl_net
```

**From source (fallback, e.g. no Tigerbrew):** build the classic Unix way
(not the Xcode-project/`.framework` way), since we launch from a
Terminal/SSH session rather than double-clicking a `.app`, so there's no
`SDLMain.m`/bundle bootstrapping to deal with:

```sh
# On a modern machine (Tiger's curl/openssl can't do modern TLS):
curl -LO https://www.libsdl.org/release/SDL-1.2.15.tar.gz
curl -LO https://www.libsdl.org/projects/SDL_net/release/SDL_net-1.2.8.tar.gz
scp SDL-1.2.15.tar.gz SDL_net-1.2.8.tar.gz tiger-box:~/src/

# On the Tiger box:
cd ~/src
tar xzf SDL-1.2.15.tar.gz && cd SDL-1.2.15
./configure --prefix=/usr/local && make -j2 && sudo make install
cd ../SDL_net-1.2.8
./configure --prefix=/usr/local && make -j2 && sudo make install
```

Either way this ends up with `/usr/local/bin/sdl-config`, `libSDL.dylib`, and
`libSDL_net.dylib`/headers, which the Makefile here picks up via
`sdl-config --cflags --libs`.

## Building

```sh
# on the Tiger box, from the repo root:
make -C Build/MacOSX-PPC

# or remotely, from your dev machine:
AA_PPC_HOST=user@tiger-box ./Build/MacOSX-PPC/remote-build.sh
```

Output binary: `Build/MacOSX-PPC/build/amulets-armor`. Run it from the `Exe/`
directory so relative asset paths resolve, same as the other ports:

```sh
cd Exe && ../Build/MacOSX-PPC/build/amulets-armor
```

## Scope of this step

This scaffolding gets the source **compiling and linking** for PPC/Tiger by
reusing the already-portable `TARGET_UNIX` code path (proven out by the
existing Intel/ARM macOS build) with `NO_ASSEMBLY=1` so the x86 inline-asm
fast paths in `3D_COLLI.C`/`COLORIZE.C`/`3D_VIEW.C` fall back to the plain C
versions.

**Endianness: audited and fixed, not yet validated on real hardware.** See
`ENDIAN_AUDIT.md` in this directory for the full inventory. Every on-disk
struct (resource files, map/BSP geometry, object types, map animation,
scripts, character saves, inventory, sound samples, trig tables) and every
network packet has byte-swap calls wired in now. All of it compiles and
behaves identically on the existing little-endian build (every swap is a
provable no-op there) — but none of it has actually run on big-endian
hardware yet, which is exactly what this Makefile is for. Expect the first
real bring-up here to surface swap bugs the little-endian testing couldn't
catch.

**Unaligned struct access** is a related open question: some on-disk structs
are byte-packed, and direct pointer-cast dereferences of misaligned
multi-byte fields can fault on real PPC even though x86 tolerates it
silently. Not yet audited independently of the endian work — watch for
`SIGBUS`/alignment traps specifically (as opposed to wrong-looking data,
which points at a missed byte-swap instead).

## Known gotchas to expect at bring-up

- If `sdl-config` isn't on `PATH`, add `/usr/local/bin` to `PATH` or pass
  `SDL_CONFIG=/usr/local/bin/sdl-config` to `make`.
- No AltiVec/G4-only assumptions are made anywhere in this build; G3 is a
  fully supported target, not just G4.
- If using Xcode Tools 2.5's `gcc-4.0` instead of Tigerbrew: it's stricter
  about some things MSVC/newer gcc let slide — expect a round of
  warning/error triage. `-std=gnu99` (already the default here) avoids most
  C99-strictness friction from this codebase's old-style declarations.
