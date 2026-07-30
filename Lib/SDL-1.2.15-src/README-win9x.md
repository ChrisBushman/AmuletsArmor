# SDL 1.2.15 source, vendored for the Windows 9x build

A trimmed copy of the official
[SDL 1.2.15 source release](https://www.libsdl.org/release/SDL-1.2.15.tar.gz)
(zlib license, see `COPYING`): `configure`/`configure.in`/`Makefile.in`/
`build-scripts/` (autoconf plumbing), `include/`, `src/`, and the
`*.in`/`*.pc.in` templates `config.status` needs to exist even though we
don't consume their output. Dropped: `docs/`, `test/`, other-platform IDE
projects (`VisualC/`, `Xcode*/`, `Borland*`), and every non-Windows
`README.*`.

**Case-sensitivity trap**: this tree was originally extracted on a
case-insensitive filesystem (macOS). Two of SDL's own files exist under
names that only differ by case from other things on the same case-
insensitive host (`sdl.pc.in`, all-lowercase, is the real file `configure`
expects -- an earlier `SDL.pc.in` copy silently "worked" locally by
resolving to the same inode, then failed the moment a case-sensitive
Linux host tried to build it). If you ever need to re-vendor this from a
fresh SDL release, verify filenames against `tar tzf` output (case-exact)
rather than trusting `cp`/`ls` on a case-insensitive host -- don't assume
this is the last one.

## Why source, not the prebuilt DLL

`Lib/SDL-1.2.15/lib/x86/SDL.dll` (prebuilt, MSVC-built) has a clean
Win95/98 import table by itself, but real Windows 95 hardware testing
still crashed *inside* it (`AASERVER caused an invalid page fault in
module SDL.DLL`) -- likely a genuine SDL-1.2-on-real-Win95 issue
independent of the mingw CRT problem that hit the other three Win9x
binaries. Rebuilding from source with the exact same toolchain/flags used
everywhere else removes that whole class of MSVC/mingw ABI mismatch as a
variable, and gets us a `libSDL.dll.a` import lib for free (no more
`gendef`/`dlltool` trick against a foreign-format `.lib`).

## The `_strtoui64` fix

Even with `-D__USE_MINGW_ANSI_STDIO=0` (needed for our own game code, see
`Build/Win9x/Makefile`), SDL's own build still produced a `SDL.dll` that
statically imported `MSVCRT.DLL:_strtoui64` -- same missing-export
problem, different source. Root-caused with a linker map file
(`-Wl,-Map=`) plus manual bisection: SDL's `configure` detects a real
`strtoull()` in mingw's headers and uses it directly instead of its own
portable `SDL_strtoull()` fallback (`src/stdlib/SDL_string.c`) -- but
mingw's `strtoull` is itself a thin forwarder (in `libmsvcrt.a`) to
`_strtoui64`, which doesn't exist on Windows 9x's real msvcrt.dll.

Fixed by forcing `configure`'s autoconf cache to report `strtoll`/
`strtoull`/`_strtoll`/`_strtoull` as unavailable
(`ac_cv_func_strtoll=no` etc., passed on the `./configure` command line --
see `Build/Win9x/Makefile`), so SDL compiles its own self-contained
`SDL_strtoll`/`SDL_strtoull` instead. Confirmed via `objdump -p` that the
resulting `SDL.dll` no longer references `_strtoi64`/`_strtoui64` at all.

## Building for Win9x

Driven by `Build/Win9x/Makefile` via a plain `./configure --host=
i686-w64-mingw32 --enable-shared --disable-static <the ac_cv_func_*
overrides above>` + `make`, with the same `CC`/`CFLAGS`
(`WINVER`/`_WIN32_WINNT`/`_WIN32_WINDOWS`/`__USE_MINGW_ANSI_STDIO=0`/
`-fno-stack-protector`) used for everything else in this port. `configure`
auto-detects the Windows-native backends (windib/directx video,
waveout/dsound audio, win32 threads/timer/joystick/cdrom) for a
`*-*-mingw*` host with no extra flags needed -- confirmed via
`ddraw.h`/`dsound.h`/`dinput.h` all being found (mingw-w64 ships them).
