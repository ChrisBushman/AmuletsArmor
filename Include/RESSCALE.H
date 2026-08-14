/****************************************************************************/
/*    FILE:  RESSCALE.H                                                     */
/****************************************************************************/
/*                                                                          */
/*    Drop-in render-resolution / window-scale module for the SDL 1.2      */
/*    port of Amulets & Armor.                                             */
/*                                                                          */
/*    The game keeps rendering at its native logical resolution            */
/*    (320x200, 8-bit palette).  This module hands the game a "shadow"     */
/*    surface of exactly the size/format it asked for, then upscales       */
/*    that shadow to a real window of any size on every frame              */
/*    (nearest-neighbour, palette-aware, optional 4:3 aspect correction).  */
/*                                                                          */
/*    Configuration is read from resolution.ini in the working directory   */
/*    (created with defaults on first run).  No game headers required;     */
/*    depends only on SDL 1.2 and the C library.                           */
/*                                                                          */
/*    Integration (3 small edits in Build/Windows/VC2013/AA/main.c):       */
/*       1.  screen = SDL_SetVideoMode(w, h, bpp, flags);                  */
/*                        --> screen = ResScaleSetVideoMode(w,h,bpp,flags);*/
/*       2.  SDL_Flip(screen);   or   SDL_UpdateRect(screen, ...);         */
/*                        --> ResScaleUpdate();                            */
/*       3.  after SDL_PollEvent(&event) / SDL_WaitEvent(&event):          */
/*                        --> ResScaleEvent(&event);                       */
/*    Optional:                                                            */
/*       SDL_WarpMouse(x, y)          --> ResScaleWarpMouse(x, y)          */
/*       SDL_GetMouseState(&x, &y)    --> ResScaleGetMouseState(&x, &y)    */
/*                                                                          */
/*    License: GPLv3, same as the rest of Amulets & Armor.                 */
/*                                                                          */
/****************************************************************************/
#ifndef _RESSCALE_H_
#define _RESSCALE_H_

/* See OPTIONS.H for why WIN32 must be hidden from SDL's own headers here
   (WIN32 and TARGET_UNIX are both defined together on this codebase's
   Apple/Unix builds). */
#ifdef TARGET_UNIX
#undef WIN32
#include "SDL.h"
#define WIN32 1
#else
#include "SDL.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Call once before ResScaleSetVideoMode (safe to call more than once).
   Loads resolution.ini from the current directory; if the file does not
   exist it is created with commented defaults. */
void          ResScaleInit(void);

/* Replacement for SDL_SetVideoMode().  Creates the real (scaled) window
   and returns a shadow surface of the requested width/height/bpp that the
   game can treat exactly like the old screen surface (draw, lock,
   SDL_SetColors, etc.).  Falls back to a plain SDL_SetVideoMode with the
   original arguments if scaling cannot be set up. */
SDL_Surface  *ResScaleSetVideoMode(
                  int width,
                  int height,
                  int bpp,
                  Uint32 flags);

/* Replacement for SDL_Flip(screen) / SDL_UpdateRect(screen, 0,0,0,0).
   Converts + upscales the shadow surface into the real window and
   presents it.  Always updates the full frame (fast at these sizes). */
void          ResScaleUpdate(void);

/* Request a screenshot of the next composited frame (game UI + RAVE 3D view).
   Saved as AAshotNN.bmp in the app folder at the next ResScaleUpdate present.
   Wired to F8 in the client input loop; used for software-vs-RAVE A/B compares. */
void          ResScaleRequestScreenshot(void);

/* rave-9: save an RGB555 buffer (RAVE direct-to-screen frame) as AAshotNN.bmp. */
void          ResScaleSaveRaveShot(const void *rgb555, int w, int h, int pitchBytes);

/* Call on every event obtained from SDL_PollEvent / SDL_WaitEvent.
   Rewrites mouse coordinates from window space back to the game's
   logical space, and (if hotkeys=1 in resolution.ini) consumes
   F11 (cycle window scale) and Alt+Enter (toggle fullscreen). */
void          ResScaleEvent(SDL_Event *event);

/* Replacement for SDL_WarpMouse(): takes logical (game) coordinates. */
void          ResScaleWarpMouse(Uint16 logicalX, Uint16 logicalY);

/* Replacement for SDL_GetMouseState(): returns logical coordinates.
   Either pointer may be NULL.  Returns the SDL button mask. */
Uint8         ResScaleGetMouseState(int *logicalX, int *logicalY);

/* Current sizes, for diagnostics (valid after ResScaleSetVideoMode). */
int           ResScaleGetLogicalWidth(void);
int           ResScaleGetLogicalHeight(void);
int           ResScaleGetWindowWidth(void);
int           ResScaleGetWindowHeight(void);

/* rave-9: letterboxed destination rect inside the window (game picture area). */
void          ResScaleGetDstRect(int *x, int *y, int *w, int *h);

/* "Level of Detail" from resolution.ini (1..6, default 2) -- how many
   times larger than classic 320x200 the 3D view is actually rendered at.
   Not applied automatically: the caller must feed this into
   View3dSetActiveScale() (3D_VIEW.H) before ViewInitialize() runs. */
int           ResScaleGetDetail(void);

#ifdef __cplusplus
}
#endif

#endif /* _RESSCALE_H_ */

/****************************************************************************/
/*    END OF FILE: RESSCALE.H                                               */
/****************************************************************************/
