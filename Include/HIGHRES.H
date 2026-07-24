/****************************************************************************/
/*    FILE:  HIGHRES.H                                                      */
/****************************************************************************/
/*                                                                          */
/*    High-resolution hybrid frame for Amulets & Armor.                     */
/*                                                                          */
/*    The game keeps its native 320x200 8-bit screen for ALL user           */
/*    interface drawing (forms, banner, text, buttons) -- none of that      */
/*    code changes.  Only the 3D view is rendered at higher density,        */
/*    directly into an N-times-larger "frame" owned by this module.         */
/*    Each frame, HighResCompose() pixel-replicates everything OUTSIDE      */
/*    the 3D view window from the 320x200 screen into the frame, around     */
/*    the natively rendered view.  The platform layer then presents the     */
/*    frame instead of the 320x200 screen.                                  */
/*                                                                          */
/*    The module is self-contained C89 with no game or SDL dependencies    */
/*    so it can be unit tested in isolation.  Pixel format is 8-bit         */
/*    palette indexes, identical to the game's screens.                     */
/*                                                                          */
/*    Copyright (c) 2026.  Licensed under the GNU GPL v3, the same          */
/*    license as the Amulets & Armor source release.                        */
/*                                                                          */
/****************************************************************************/
#ifndef _HIGHRES_H_
#define _HIGHRES_H_

#ifdef __cplusplus
extern "C" {
#endif

#define HIGHRES_BASE_WIDTH   320
#define HIGHRES_BASE_HEIGHT  200
#define HIGHRES_MAX_SCALE    6

/* Set up the frame at the given integer scale (1..HIGHRES_MAX_SCALE).    */
/* Returns 1 on success, 0 on failure (out of memory / bad scale).        */
/* Calling it again re-initializes at the new scale.                       */
int HighResInit(int scale);

/* Free the frame. Safe to call when not initialized.                      */
void HighResFinish(void);

/* Current scale, or 0 if not initialized.                                 */
int HighResGetScale(void);

/* The output frame: (320*scale) x (200*scale) bytes, row-major,           */
/* stride == HighResFrameStride().  NULL if not initialized.               */
unsigned char *HighResFrame(void);
int HighResFrameWidth(void);
int HighResFrameHeight(void);
int HighResFrameStride(void);

/* Declare where the 3D view sits in LOGICAL 320x200 coordinates.          */
/* Call this from the same place the game calls View3dSetSize() /          */
/* positions the view.  The rect is clamped to the 320x200 screen.         */
/* Pass w<=0 or h<=0 (or call HighResDisableViewWindow) when no 3D         */
/* view is on screen (menus, forms, town UI full-screen pages).            */
void HighResSetViewWindow(int x, int y, int w, int h);
void HighResDisableViewWindow(void);
int  HighResViewWindowEnabled(void);

/* Where the renderer should draw: pointer to the top-left pixel of the    */
/* 3D view inside the frame, at native (scaled) density.  The view is      */
/* (w*scale) x (h*scale) pixels with row stride HighResFrameStride().      */
/* NULL when not initialized or window disabled.                           */
unsigned char *HighResViewPtr(void);
int HighResViewNativeWidth(void);
int HighResViewNativeHeight(void);

/* Pixel-replicate the 320x200 screen into the frame, skipping the view    */
/* window rectangle (the renderer already filled it at native density).    */
/* If the window is disabled, the whole screen is replicated.              */
void HighResCompose(const unsigned char *screen320);

/* Replicate the ENTIRE 320x200 screen (ignores the view window).          */
/* Use when the 3D view was not rendered this frame.                       */
void HighResComposeAll(const unsigned char *screen320);

/* Overlay support: within the view window only, pixel-replicate every     */
/* NON-ZERO pixel of the 320x200 screen on top of the native view.         */
/* This lets the existing 320-space overlay drawing (weapon animation,     */
/* crosshairs, "look" text) appear above the high-res view: clear the      */
/* view rect of the 320 screen to color 0 before the overlay handler       */
/* runs, then call this after HighResCompose().  Color 0 is already the    */
/* game's transparent color (see GrDrawBitmapMasked).                      */
void HighResComposeMaskedOver(const unsigned char *screen320);

/* Utility: fill the view-window rect of a 320x200 screen with a color.    */
/* (For pre-clearing the overlay area to the transparent color 0.)         */
void HighResClearViewRect320(unsigned char *screen320, unsigned char color);

/* Down-sample the native view back onto the view-window rect of a         */
/* 320x200 screen (point sampling).  Call after rendering a frame so the   */
/* classic screen stays coherent for frames where the 3D view is not       */
/* redrawn (pause, forms over view, screenshots by old code paths).        */
void HighResExtractViewTo320(unsigned char *screen320);

#ifdef __cplusplus
}
#endif

#endif /* _HIGHRES_H_ */
/****************************************************************************/
/*    END OF FILE:  HIGHRES.H                                               */
/****************************************************************************/
