# Linux Build

This is the native Linux build path for Amulets & Armor.

## Scope

- Uses CMake as the build system (same top-level `CMakeLists.txt` as macOS).
- Reuses the `TARGET_UNIX` code path already proven by the macOS
  Intel/ARM and PowerPC/Tiger ports.
- IPX networking is always enabled (IPX-over-UDP via SDL_net, same as
  every other non-DOS port).

## Prerequisites

Debian/Ubuntu:

```sh
sudo apt-get install build-essential cmake ninja-build pkg-config \
    libsdl1.2-dev libsdl2-net-dev
```

Notes:
- Modern Ubuntu (24.04+) ships `sdl12-compat` under the `libsdl1.2-dev`
  package name -- an SDL 1.2 API shim over SDL2, the same approach the
  macOS build uses via Homebrew's `sdl12-compat`.
- `libsdl2-net-dev` provides UDP networking for IPX-over-UDP multiplayer.

## Build Scripts

Scripts live in `Build/Linux/` and can be run from anywhere; they resolve
the repo root automatically. They mirror `Build/MacOSX/*` exactly.

| Script | Description |
|---|---|
| `configure.sh [debug\|asan\|release]` | CMake configure step |
| `make.sh [debug\|asan\|release\|clean]` | Configure (if needed) + build |
| `clean.sh` | Remove all `out/linux*` build directories |
| `run.sh [debug\|asan\|release]` | Run the game from the `Exe` directory |

### Quick start (debug build)

```sh
./Build/Linux/make.sh
./Build/Linux/run.sh
```

### Release build

```sh
./Build/Linux/make.sh release
./Build/Linux/run.sh release
```

### Clean

```sh
./Build/Linux/clean.sh
```

## Multiplayer

Pass the server IP as the first argument and an optional port as the second.

```sh
cd Exe
../out/linux/amulets-armor <server-ip> [<port>]
```

## Manual CMake commands

If you prefer to drive CMake directly:

```sh
# Configure
cmake -S . -B out/linux -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build out/linux --target amulets-armor -j

# Run (must be launched from Exe/ so asset paths resolve)
cd Exe
../out/linux/amulets-armor
```

## Why `AA_GENERIC_UNIX_MAIN`

Linux gets a build define macOS doesn't: `AA_GENERIC_UNIX_MAIN=1`. This
codebase's own `WIN32=1` define (needed by real `WIN32`-guarded branches
elsewhere in `Source/*.C`, unrelated to SDL) leaks into modern SDL headers'
own platform sniffing and makes them think this is a real Windows build,
which triggers SDL's `main` -> `SDL_main` rename. On real Windows/macOS a
platform-provided `SDLmain` supplies the actual `main()` that calls
`SDL_main()` -- on Linux there is no such shim, so the binary would link
with no `main()` at all. `AA_GENERIC_UNIX_MAIN` (the same mechanism
already used by the IRIX-O2 build, for the same underlying reason) makes
`main.c` hide `WIN32` from SDL's headers around the `#include <SDL.h>`
line and provide its own `main()` -> `SDL_main()` wrapper instead. See the
comments in `Build/Windows/VC2013/AA/main.c` for the full explanation.
