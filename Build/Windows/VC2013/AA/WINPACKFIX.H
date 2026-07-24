/* Forced-included (see AA.vcxproj's ForcedIncludeFiles) into every
 * translation unit in this project, before anything else -- including
 * before the project's own -Zp1/StructMemberAlignment=1Byte setting
 * (MSVC's equivalent of the codebase's GCC-only PACK attribute) has a
 * chance to affect anything.
 *
 * <windows.h> is pulled in both directly (main.c) and transitively via
 * SDL's own Windows headers, and only gets fully parsed the *first*
 * time it's hit in a given translation unit (its own #ifndef include
 * guard skips every subsequent #include <windows.h>). If that first
 * parse happens under 1-byte packing, several of its internal
 * compile-time size assertions (visible as winnt.h "C_ASSERT:
 * redefinition; different subscripts" / "negative subscript" errors)
 * break, because plenty of SDK structs were never meant to be packed
 * this tightly. Forcing the *first* parse to happen right here, under
 * normal 8-byte alignment, sidesteps that: every later #include
 * <windows.h> elsewhere in the same translation unit is then just a
 * no-op against the same already-parsed, already-correct definitions.
 */
#ifndef AA_WINPACKFIX_H
#define AA_WINPACKFIX_H

#if defined(_WIN32) || defined(WIN32)
#pragma pack(push, 8)
#include <windows.h>
#pragma pack(pop)
#endif

#endif
