#ifndef AA_DIRECT_SHIM_H
#define AA_DIRECT_SHIM_H

#if defined(__GNUC__) && !defined(TARGET_UNIX)
/* Win9x port (mingw/GCC targeting real Windows, not TARGET_UNIX): this
   project's own Include/ directory sits ahead of the compiler's built-in
   system include directories on the search path (needed to find the
   game's own headers), so a plain #include <direct.h> from anywhere in
   the codebase would otherwise always resolve to this file's
   TARGET_UNIX-only shim below instead of mingw's real direct.h (which
   already provides a genuine, correctly-signatured _mkdir).
   #include_next continues the search from the next directory after this
   one, reaching mingw's real header. Not attempted for MSVC: it doesn't
   support #include_next, and this file's behavior under MSVC predates
   this Win9x port -- left exactly as-is (the #else branch below) so the
   working VC2013 build stays untouched. */
#include_next <direct.h>
#else
#include <sys/stat.h>

static inline int _mkdir(const char *path)
{
    return mkdir(path, 0777);
}

#ifndef mkdir
#define mkdir(path) _mkdir(path)
#endif
#endif

#endif
