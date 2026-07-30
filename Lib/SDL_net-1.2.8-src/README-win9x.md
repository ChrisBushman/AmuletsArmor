# SDL_net 1.2.8 source, vendored for the Windows 9x build

Just the library sources (`SDLnet.c`, `SDLnetTCP.c`, `SDLnetUDP.c`,
`SDLnetselect.c`, `SDLnetsys.h`, `SDL_net.h`) from the official
[SDL_net 1.2.8 release](https://www.libsdl.org/projects/SDL_net/release/SDL_net-1.2.8.tar.gz)
(zlib license, see `COPYING`) -- not the full tarball (autotools build
system, other-platform IDE projects, example programs are all dropped).

`Lib/SDL_net-1.2.8/` (sibling directory) has prebuilt headers/import
libs/DLLs for the *native Windows (VC2013)* build and is untouched.
This directory exists only because that prebuilt DLL can't be reused for
Windows 9x -- see the patch below -- and mingw needs to compile it from
source anyway to produce an import lib in its own format.

## The Windows 9x patch

`SDLnet.c`'s `SDLNet_GetLocalAddresses()` (Win32 branch) statically calls
`GetAdaptersInfo()`. Its own documented minimum supported client is
Windows 2000 Professional (see
[Microsoft's docs](https://learn.microsoft.com/en-us/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersinfo)),
so a static import of it makes the *entire DLL* fail to load on Windows
95/98/ME -- even though nothing in AmuletsArmor, AAServer, or AALauncher
ever calls this optional IP-enumeration convenience function. Windows
resolves a DLL's whole import table eagerly at load time, so "nobody
calls it" doesn't help; the import itself is the problem.

Patched to resolve `GetAdaptersInfo` dynamically via `LoadLibraryA` +
`GetProcAddress` instead, falling back to 0 addresses if `iphlpapi.dll`
or the symbol isn't present. See the comment at the patch site in
`SDLnet.c` for the full explanation.

## Building for Win9x

Built by `Build/Win9x/Makefile` (mingw, `i686-w64-mingw32-gcc`), linking
against an import lib generated from the vendored
`Lib/SDL-1.2.15/lib/x86/SDL.dll` (already Win9x-clean as shipped, no
patch needed there -- see the Win9x plan notes). Deliberately does not
link `-liphlpapi`: after the patch above, nothing in this source calls
`GetAdaptersInfo` directly anymore, so nothing should pull in a static
`IPHLPAPI.DLL` import at all.
