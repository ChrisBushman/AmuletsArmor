#ifndef _WINDIRECT_H_
#define _WINDIRECT_H_
/* Was DIRECT.H (renamed): collided with Include/direct.h, an unrelated
   Windows-CRT _mkdir compatibility shim -- same base name, different guard
   macro, different purpose. On a case-sensitive filesystem (e.g. IRIX),
   "direct.h" (lowercase, as this file's own #include sites all quoted it)
   doesn't same-directory-match this file (DIRECT.H, uppercase), so it
   silently fell through to Include/direct.h instead of erroring, leaving
   this file's own declarations (DirectMouseSet et al) invisible below. */

#ifdef __cplusplus
extern "C" {
#endif

#include "MOUSEMOD.H"
#include "GENERAL.H"

#ifndef _cdecl
#define _cdecl
#endif

void DirectDrawOn(void) ;
void DirectDrawOff(void) ;
void _cdecl WindowsUpdate(char *p_screen, unsigned char * palette) ;
void OutsideMouseDriverGet(T_word16 *xPos, T_word16 *yPos);
T_void DirectMouseSet(T_word16 newX, T_word16 newY) ;
T_buttonClick DirectMouseGetButton(T_void) ;
T_void DirectMouseSetButton(T_buttonClick click) ;
T_void DirectMouseClearButton(T_buttonClick click) ;

#ifdef __cplusplus
}
#endif

#endif // _WINDIRECT_H_
