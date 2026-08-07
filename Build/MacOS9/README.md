# Mac OS 9 / PowerPC platform (Metrowerks CodeWarrior 8)

Builds a **classic Mac OS 9** (not OS X) PowerPC/CFM application that runs on a
real PowerBook/Power Mac under Mac OS 9.x. Unlike every other platform here,
this one is **not** a gcc/Makefile build — classic Mac OS has no Unix
toolchain, so it uses **Metrowerks CodeWarrior 8** (the `AmuletsArmor.mcp`
project in this folder) and the classic Mac Toolbox / MSL runtime.

The game shares the SDL 1.2 code path with the Unix ports
(`TARGET_UNIX` + `WIN32` + `WIN_IPX` + `AA_REAL_SDL12`), linking a classic-Mac
build of SDL 1.2.15 (DrawSprocket video, Sound Manager audio) + SDL_net.

## Files in this folder

| File | Purpose |
|------|---------|
| `AmuletsArmor.mcp` | CodeWarrior 8 project (targets, access paths, link order). Its access paths are machine-local — retarget `Source/`, `Include/`, the SDL/SDL_net libs, and this folder when you open it. |
| `AA_OS9_Prefix.h` | CW prefix file (set in the project's C/C++ Language panel). Injects the platform defines, strips the `cdecl/__fastcall/...` keywords MSL lacks, sets pragmas (`ANSI_strict off`, `mpwc_relax on`, `require_prototypes off`), and `#include <MacHeaders.c>` (precompiled Toolbox). |
| `aa_os9_compat.c` | Add to the project. Small classic-Mac shims: `putenv`/`_exit` stubs, a `pascal SetDialogTimeout` no-op (so DialogsLib isn't needed), and an optional boot tracer (`AA_BootLog`, gated by `AA_BOOT_TRACE` — off by default). |
| `make-os9-image.sh` | Packages a build folder into an OS-9-mountable NDIF (Rdxx) HFS disk image (preserves the app resource fork). `make-os9-image.sh <src-folder> <output.img>`. |

## Prefix defines (also in `AA_OS9_Prefix.h`)

`TARGET_UNIX=1 WIN32=1 WIN_IPX=1 AA_REAL_SDL12=1 NO_ASSEMBLY=1
_CRT_SECURE_NO_WARNINGS=1 _MBCS=1 SCREEN_WIDTH=320 SCREEN_HEIGHT=200`
(deliberately **not** `NDEBUG` — the reference Unix builds don't define it, and
`RESOURCE.C` relies on the loose-file fallback that lives outside `#ifdef NDEBUG`).

## SDL / SDL_net (built with CodeWarrior)

The classic-Mac SDL libraries are built from the vendored sources in `Lib/`,
each with its own CodeWarrior project:

- **SDL 1.2.15** — `Lib/SDL-1.2.15-src/` (same version the Unix ports use) +
  `Lib/SDL-1.2.15-src/SDL.mcp`. Classic-Mac backend: DrawSprocket video, Sound
  Manager audio. Three small `macos/`-only source tweaks were needed for the CW
  build (all in Mac-only files, so no other platform is affected):
  `SDL_main.c` `#undef WIN32` (the AA project defines WIN32 project-wide, which
  `SDL_platform.h` would turn into `__WIN32__` and pull Windows headers);
  `SDL_systimer.c` a forward decl for CW strict prototypes; `SDL_MPWtimer.c`
  stubbed (SDL_systimer.c is the timer impl — avoids duplicate symbols).
- **SDL_net 1.2.5** — `Lib/SDL_net-1.2.5-src/` + `SDL_net.mcp`. (This is an
  **older** SDL_net than the Unix ports' `Lib/SDL_net-1.2.8-src`; 1.2.5 is what
  the classic-Mac CW project was set up against.)

Both produce CFM shared libraries (`libSDL`, `SDL_net`) that this project links.

## Link libraries

`libSDL` + `SDL_net` (the CFM shared libs above), `MSL_C++_PPC.Lib`,
`InterfaceLib`, `MathLib`, `DrawSprocketLib`, `InputSprocketLib`, Open Transport
(`OpenTransportLib`+`OpenTransportAppPPC.o`, `OpenTptInternetLib`+`OpenTptInetPPC.o`).
**Do not** link DialogsLib (its `MacDialogsLib` import doesn't exist on classic
OS 9 → "app is damaged"); the `SetDialogTimeout` shim in `aa_os9_compat.c`
covers the one symbol needed.

Bake a real memory partition into the linked app (`SIZE` resource, ~64 MB
preferred / 24 MB minimum) with `Rez -a` or ResEdit, or it won't launch.

## Classic-Mac-specific source fixes (all `#if defined(macintosh)`-guarded)

These were the real bring-up bugs; all are guarded so no other platform is
affected. None were endianness — the big-endian MIPS/OS-X-PPC ports were fine;
these are CodeWarrior- and classic-Mac-specific:

- **`PACK` is empty on CodeWarrior.** CW has no per-field packed attribute, so
  every on-disk/on-wire struct tagged `PACK` got PPC natural alignment (e.g.
  the 39-byte `.RES` index entry padded to 44) and misparsed — breaking *all*
  resource loading. Fixed by wrapping each such struct in
  `#pragma options align=packed` / `align=reset` (RESOURCE.C, PACKET.H,
  VIEWFILE.H, OBJTYPE.C, INVENTOR.*, SPELTYPE.H, EQUIP.H, IRESOURC.H, SYNCPACK.H).
- **Path separator.** Classic Mac uses `:`, and `/` is an ordinary filename
  char. Subdirectory loose files (`MAPDESC/DES…`, `AAMUSIC/*.MUS`) are
  translated to Mac relative paths (`:dir:file`) in `FILE.C` `FileOpen` and
  `INIFILE.C` `INIFileOpen`.
- **Text-mode `fopen` is wrong on classic Mac** (translates line endings —
  a no-op on Unix). Every gameplay text/data file is opened `"rb"`/`"wb"`
  (FORM.C forms, INIFILE saves, 3D_IO map, RESSCALE resolution.ini,
  CONTROL/ESCMENU control.txt, CLIENT teleport, MAINUI, TOSDATA, DOOR, OBJGEN).
  This also fixed character-name entry: the `.FRM` form loader read forms in
  text mode, garbling the `ENDOFTEXT` parse that focuses the first editable box.
- **INI line endings.** `INIFILE.C` reads lines char-by-char (breaking on
  `\r`, `\n`, or `\r\n`) so a CR-only `config.ini` (which OS 9 produces when the
  game rewrites it) parses correctly instead of faulting near EOF.
- **`FileRead` loops on short reads**; `3D_COLLI.C` renamed `MoveTo`→
  `MoveObjectTo` (QuickDraw collision); `GENERAL.H`/`PERFPROF.C` guard Unix-only
  headers behind `!macintosh`.

## Runtime data

Ship the full `Exe/` game data alongside the app **plus** two sets that the
default packaging omits: the compiled maps `L*.MAP` (the town→adventure
transition gates on `MapExist("l<n>.map")`, so quests won't start without them)
and `AAMUSIC/*.MUS` (background music). Because classic Mac stores resource
forks, the cleanest delivery is an **NDIF (`Rdxx`) HFS disk image**, not a raw
`.dmg`, so it mounts under OS 9's Disk Copy with the app's `cfrg`+`SIZE` fork
intact.
