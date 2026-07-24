/* SDLmain.lib (prebuilt against SDL 1.2.15's own old <stdio.h>) calls the
 * pre-UCRT CRT's ___iob_func to get at stdin/stdout/stderr. The Universal
 * CRT (VS2015+) dropped that symbol entirely, so linking SDLmain.lib
 * against a modern toolset leaves it unresolved even with
 * legacy_stdio_definitions.lib (which only covers _iob_func, not the
 * older triple-underscore name). Supply it ourselves. */
#include <stdio.h>

FILE _iob[3];

FILE * __cdecl __iob_func(void)
{
    _iob[0] = *stdin;
    _iob[1] = *stdout;
    _iob[2] = *stderr;
    return _iob;
}
