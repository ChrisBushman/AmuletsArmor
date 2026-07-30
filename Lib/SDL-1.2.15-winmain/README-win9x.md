# SDL 1.2.15's real WinMain() wrapper, vendored for the Windows 9x build

`SDL_win32_main.c` is copied unmodified from the official
[SDL 1.2.15 source release](https://www.libsdl.org/release/SDL-1.2.15.tar.gz)
(`src/main/win32/SDL_win32_main.c`, placed in the public domain by Sam
Lantinga per the file's own header).

## Why this is needed

`main.c` (`Build/Windows/VC2013/AA/main.c`) defines the game's real entry
point as `int SDL_main(int argc, char *argv[])`, matching every other
platform. On real Windows, something still has to provide the actual
process entry point (`WinMain`) that sets up `argc`/`argv` from the
Windows command line and calls `SDL_main()` -- normally `SDLmain.lib`
(vendored at `Lib/SDL-1.2.15/lib/x86/SDLmain.lib`), a tiny MSVC-format
static library shipped alongside SDL.lib/SDL.dll for exactly this.

That `.lib` is MSVC-format COFF, and while GNU binutils can sometimes
read plain MSVC static libraries, trusting a cross-toolchain link against
someone else's MSVC-object-file ABI is a real risk (CRT startup
assumptions, `WinMain`/`SDL_main` calling-convention details, etc.) for
very little benefit when the actual source is small, public domain, and
freely available. So `Build/Win9x/Makefile` compiles this file itself
with the same `i686-w64-mingw32-gcc` used for everything else, instead of
linking the vendored `SDLmain.lib`.
