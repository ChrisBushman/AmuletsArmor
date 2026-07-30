#ifdef __cplusplus
extern "C" {
#endif

/* See Include/OPTIONS.H for why WIN32 must be hidden from SDL's own
   headers here (WIN32 and TARGET_UNIX are both defined together on this
   codebase's Apple/Unix builds). */
#ifdef TARGET_UNIX
#undef WIN32
#include <SDL.h>
#define WIN32 1
#else
#include <SDL.h>
#endif
#include "GENERAL.H"
#include "KEYBOARD.H"
#include "MOUSEMOD.H"

/* Not just TARGET_UNIX: the real (mingw/GCC-built) Windows 9x target
   uses the same RESSCALE/HighRes pipeline now too -- see main.c's own
   AA_USE_RESSCALE_PIPELINE (that name isn't shared across translation
   units, so this repeats the same defined(TARGET_UNIX) ||
   defined(__GNUC__) condition directly). Needs 3D_VIEW.H for the
   runtime VIEW3D_SCALE this file's mouse-coordinate conversion below
   depends on -- only the original MSVC-built Windows client, which
   never runs RESSCALE at all, doesn't need it. */
#if defined(TARGET_UNIX) || defined(__GNUC__)
#include "3D_VIEW.H"
#endif

static T_buttonClick G_mouseButton = 0 ;
static T_word16 G_mouseX = 0 ;
static T_word16 G_mouseY = 0 ;

T_void DirectMouseSet(T_word16 newX, T_word16 newY)
{
    /* newX/newY arrive in the raw window/logical coordinate space. On
       the RESSCALE/HighRes pipeline (TARGET_UNIX, or the real mingw-built
       Windows 9x target) that space is LOGICAL_W/H = 320/200 *
       VIEW3D_SCALE, and VIEW3D_SCALE is now a runtime "Level of Detail"
       setting (1..3, see 3D_VIEW.H) rather than the hardcoded 2 it used
       to be -- dividing by a fixed 2 here left every position past half
       the window's width/height clamped to 319/199 at detail=1 (visible
       as the cursor seeming locked in the upper-left quadrant), or would
       have under-scaled it at detail=3. Only the original MSVC-built
       Windows client (never on the RESSCALE pipeline, always a hardcoded
       640x400 window) still needs the fixed halving. */
#if defined(TARGET_UNIX) || defined(__GNUC__)
    newX = (T_word16)(newX / VIEW3D_SCALE);
    newY = (T_word16)(newY / VIEW3D_SCALE);
#else
newX >>= 1;
newY >>= 1;
#endif
#ifdef TARGET_UNIX
    /* In mouselook (relative mode), don't update cursor position from
     * absolute OS coords — the in-game cursor must stay fixed. */
    if (MouseIsRelativeMode())
        return;
#endif
    if (newX > 319)
        newX = 319  ;
    if (newY > 199)
        newY = 199 ;
    G_mouseX = newX ;
    G_mouseY = newY ;
}


void OutsideMouseDriverGet(T_word16 *xPos, T_word16 *yPos)
{
    *xPos = G_mouseX ;
    *yPos = G_mouseY ;
}

void OutsideMouseDriverSet(T_word16 xPos, T_word16 yPos)
{
    /* xPos/yPos arrive in UI space (0-319/0-199, same as
       OutsideMouseDriverGet's G_mouseX/Y); both SDL_WarpMouse (needs
       real window/logical coordinates) and DirectMouseSet (expects that
       same raw space -- it divides back down to UI space itself, see
       above) need them converted the same direction DirectMouseSet
       divides, i.e. multiplied by VIEW3D_SCALE. Previously hardcoded *2
       for both -- and inconsistently passed DirectMouseSet raw
       (un-multiplied) xPos/yPos on TARGET_UNIX, silently relying on
       DirectMouseSet's own now-removed unconditional halving to happen
       to cancel out; made explicit and correct here instead of
       depending on that coincidence. */
#if defined(TARGET_UNIX) || defined(__GNUC__)
    SDL_WarpMouse((Uint16)(xPos*VIEW3D_SCALE), (Uint16)(yPos*VIEW3D_SCALE));
    DirectMouseSet((T_word16)(xPos*VIEW3D_SCALE), (T_word16)(yPos*VIEW3D_SCALE));
#else
    SDL_WarpMouse(xPos*2, yPos*2);
    DirectMouseSet(xPos*2, yPos*2);
#endif
}

T_void DirectMouseSetButton(T_buttonClick click)
{
    G_mouseButton = click ;
}

T_buttonClick DirectMouseGetButton(T_void)
{
    return G_mouseButton ;
}

#if 0 // Old Windows version
extern void KeyboardUpdate(E_Boolean updateBuffers) ;

void _cdecl DirectDrawOn(void)
{
    G_screen = new CGraphicsScreen(
                       G_windowHandle,
                       320,
                       200,
                       GRAPHICS_SCREEN_TYPE_8BIT) ;
}

void _cdecl DirectDrawOff(void)
{
    delete G_screen ;
    G_screen = NULL ;
}

static CBmpGraphic *G_directBmp = NULL ;

extern int WindowsUpdateMessages(void) ;
void _cdecl WindowsUpdate(char *p_screen, char *palette)
{
    char *p_data ;
    CBmpGraphic *p_pal;
    HDC dc ;
    int i;
    RGBQUAD newPal[256] ;
    T_byte8 *p ;
    E_Boolean isNew ;

    if (!G_directBmp)  {
        G_directBmp = new CBmpGraphic(320, 200) ;
        p_pal = new CBmpGraphic("pal.bmp") ;
        G_screen->setPalette(p_pal->getPalette()) ;
        G_directBmp->forcePalette(p_pal->getPalette()) ;
        G_directBmp->fixUpPalette() ;
        delete p_pal ;
        isNew = TRUE ;
    } else {
        isNew = FALSE;
    }
    p = (T_byte8 *)palette ;
    for (i=0; i<256; i++)  {
        newPal[i].rgbRed = (p[0]&0x3F)<<2 ;
        newPal[i].rgbGreen = (p[1]&0x3F)<<2 ;
        newPal[i].rgbBlue = (p[2]&0x3F)<<2 ;
/*
        newPal[i].rgbRed = i ;
        newPal[i].rgbGreen = i ;
        newPal[i].rgbBlue = i ;
*/
        newPal[i].rgbReserved = 0 ;
        p+=3 ;
    }
//    if (isNew)
        G_directBmp->forcePalette(newPal) ;
    G_screen->setPalette(newPal) ;
    p_data = (char *)G_directBmp->getRawData() ;
    p_data += 199*320 ;
    for (i=0; i<200; i++)  {
        memcpy(p_data, p_screen, 320) ;
        p_data -= 320 ;
        p_screen += 320 ;
    }
    if ((G_screen) && ((dc = G_screen->GetDC()) != NULL))  {
//        RealizePalette(dc) ;
        G_directBmp->drawToDC(dc, 0, 0) ;
        G_screen->ReleaseDC(dc) ;

        G_screen->Flip(1) ;
    }
    WindowsUpdateMessages() ;
    KeyboardUpdate(TRUE) ;
}
#else
extern void _cdecl WindowsUpdate(char *p_screen, char *palette);
#endif

#ifdef __cplusplus
}
#endif

