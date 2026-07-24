/* Included from main.c in place of a raw #include <Windows.h>. This
 * project forces 1-byte struct packing globally (StructMemberAlignment,
 * MSVC's equivalent of the codebase's GCC-only PACK attribute), which
 * breaks several of windows.h's own internal compile-time size checks
 * if it's parsed under that setting (visible as winnt.h "C_ASSERT:
 * redefinition; different subscripts" / "negative subscript" errors on
 * a modern Windows SDK) -- restore normal alignment just for windows.h
 * itself.
 *
 * Deliberately NOT force-included project-wide: most of this codebase
 * (e.g. SOUND.C's SOS.H-family headers) defines its own legacy
 * DOS-era HANDLE/BOOL/WORD/etc. typedefs and was never meant to see
 * the real Windows API types -- pulling windows.h into every
 * translation unit made those clash. main.c is the only file that
 * actually needs it. */
#ifndef AA_WINPACKFIX_H
#define AA_WINPACKFIX_H

#if defined(_WIN32) || defined(WIN32)
#pragma pack(push, 8)
#include <windows.h>
#pragma pack(pop)
#endif

#endif
