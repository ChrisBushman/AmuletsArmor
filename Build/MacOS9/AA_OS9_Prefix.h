/* AA_OS9_Prefix.h -- CodeWarrior target prefix file for the Mac OS 9 (PPC/CFM)
 * Amulets & Armor build.  Set this as the target's "Prefix File"
 * (C/C++ Language panel).  It force-includes the classic Mac Toolbox
 * headers, then injects the exact define set the known-good macOS/Unix
 * CMake build uses (CMakeLists.txt) -- MINUS AA_GENERIC_UNIX_MAIN, because
 * OS 9 (like macOS/APPLE) has a REAL SDL_main.c providing main()->SDL_main,
 * so it must NOT take the Linux "generic unix main" path.
 */
#include <MacHeaders.c>          /* classic Mac PPC Toolbox, precompiled */

/* The AA game code is C, but its sources use the uppercase .C extension,
   which CodeWarrior's MW C/C++ compiler treats as C++ (every other
   platform's build forces C -- e.g. CMake's set_source_files_properties
   LANGUAGE C). Compiling C as C++ produces thousands of spurious errors
   (illegal name overloading, char[]->unsigned char* conversion errors,
   math.h pulling <cmath>, etc). Force C here. The two genuine C++ units
   (direct.cpp, ipx_client.cpp) flip it back on at their own top. */
#pragma ANSI_strict off          /* allow // comments + non-int bitfields (AA + SDL use both) */

/* AA uses T_byte8* (unsigned char*) for all strings, but string literals are
   char* -- every other compiler treats char*<->unsigned char* as a warning;
   CW makes it a hard "illegal implicit conversion" error unless MPW-relaxed
   pointer rules are on. (= the "Relaxed Pointer Type Rules" panel checkbox.) */
#pragma mpwc_relax on
#pragma warn_implicitconv off

/* AA calls a few POSIX funcs (_exit, putenv, mkdir) whose classic-Mac MSL
   prototypes aren't in scope at every call site; they link fine. Don't make
   the missing prototype a hard error (the panel's "Require Function
   Prototypes" is on for the normal C-source hygiene). */
#pragma require_prototypes off

/* The C++ units (ipx_client.cpp, direct.cpp) use `bool`; the panel's
   "Enable bool Support" is off, so turn the keyword on for C++ TUs only. */
#ifdef __cplusplus
#pragma bool on
#endif

/* --- platform routing -------------------------------------------------- */
#define TARGET_UNIX   1          /* THE SDL-routing lever: video/audio/input/timing -> SDL,
                                    not the DOS 0xA0000 / mode-X / inp/outp paths */
#define WIN32         1          /* defined project-wide on all Apple/Unix builds too; makes
                                    TESTME.C use inert game_main(), enables shared WIN32 paths,
                                    drives OPTIONS.H's SDL-header WIN32-hide trick */
#define WIN_IPX       1          /* DITALK.C -> Win32/WINDTALK.C -> ipx_client (IPX-over-UDP
                                    via SDL_net), the cross-platform transport */
#define AA_REAL_SDL12 1          /* We link a genuine SDL 1.2 (bare <SDL_net.h>/<SDL.h> paths),
                                    like the G3/Panther, MacOSX-PPC and IRIX-O2 Makefile builds
                                    -- NOT the modern-macOS sdl12-compat+SDL2 (<SDL2/...>) combo.
                                    Routes ipx_client.cpp etc. to the bare include paths. */

/* --- build knobs (verbatim from CMake) --------------------------------- */
/* Release/Final semantics like every other platform's shipping build: turns
   AA's DebugRoutine/DebugEnd/DebugCheck into no-ops. Without it the debug
   call-stack (G_CallStack[60]) is live and overflows mid-startup -> DebugFail
   crash. CMake release defines NDEBUG automatically; CW's Final target does not. */
#define NDEBUG        1
#define NO_ASSEMBLY   1
#define _CRT_SECURE_NO_WARNINGS 1
#define _MBCS         1
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 200

/* --- strip MSVC calling-convention keywords ---------------------------- */
#define cdecl
#define _cdecl
#define __cdecl
#define __fastcall
#define __stdcall

/* NOTE: AA_GENERIC_UNIX_MAIN is deliberately NOT defined (see header above). */
