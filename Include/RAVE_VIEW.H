/*-------------------------------------------------------------------------*
 * File:  RAVE_VIEW.H
 *-------------------------------------------------------------------------*/
/**
 *  QuickDraw 3D RAVE hardware-renderer backend (interface).
 *
 *  Alternative rasterization backend for the software 3D renderer. The BSP/
 *  visibility walk in 3D_VIEW.C still runs; when AA_RENDERER_RAVE is on it
 *  emits its geometry as textured, gouraud-lit triangles to the RAVE engine
 *  (RaveViewEmitQuad) between a RaveViewFrameBegin()/RaveViewFrameEnd() pair,
 *  instead of (or in addition to) the software rasterizer.
 *
 *  Enabled with -DAA_RENDERER_RAVE on the classic-Mac RAVE build (e.g. the
 *  PowerBook G3 / ATI Rage LT Pro target). On every other build these are
 *  harmless no-ops, so callers (VIEW.C, 3D_VIEW.C) link unconditionally.
 *
 *  The engine target was confirmed by rave_probe on real hardware:
 *  "ATI 3DRage QD3D Rave Engine" -- HW Texture/Gouraud/TextureHQ/Blend/ZSort,
 *  ~5MB texture VRAM. See project memory (renderer decouple branching).
 *
 *<!-----------------------------------------------------------------------*/
#ifndef _RAVE_VIEW_H_
#define _RAVE_VIEW_H_

#include "GENERAL.H"

/* Create/select the RAVE draw context on the screen device. Called from
   ViewInitialize() after View3dInitialize(). */
T_void RaveViewInit(T_void) ;

/* Tear down the RAVE context. Called from ViewFinish(). */
T_void RaveViewFinish(T_void) ;

/* Begin/end one RAVE frame around the BSP/geometry pass. FrameBegin opens the
   render (clears color+Z, sets render state); the 3D_VIEW.C walk then emits
   geometry via RaveViewEmitQuad; FrameEnd finishes the render and presents.
   VIEW.C wraps View3dDrawView() with these when RaveViewIsActive(). */
T_void RaveViewFrameBegin(T_void) ;
T_void RaveViewFrameEnd(T_void) ;

/* One projected, textured, gouraud-lit vertex handed to the RAVE backend.
   Plain floats only (no RAVE types leak out of RAVE_VIEW.C):
     x,y   -- pixel coords in the RAVE context (VIEW3D space, CLIP_LEFT origin)
     z     -- depth 0.0 (near) .. 1.0 (far)
     invW  -- 1/w (w = view-space distance); RAVE needs it for perspective
     u,v   -- TEXEL coords (0..texWidth / 0..texHeight, may exceed to tile);
              RaveViewEmitQuad normalizes by the texture size and does *invW
     light -- 0..1 brightness (sector light + distance) modulating this
              vertex's texels; gouraud-interpolated across the quad */
typedef struct {
    float x, y, z, invW, u, v, light ;
} T_raveVertex ;

/* Emit one wall/floor quad as two textured triangles (corners in order:
   top-left, bottom-left, bottom-right, top-right). p_texture is an AA texture
   pixel pointer (see IRaveGetTexture). alpha < 1 blends the quad (translucent
   walls / building windows); use 1.0 for opaque. masked != 0 = a masked wall
   (AA opaque==2) whose palette index 0 is the transparent cutout; use 0 for
   solid walls and floors, where index 0 is a real opaque black. No-op if the
   texture can't be uploaded or RAVE is inactive. */
T_void RaveViewEmitQuad(
           T_byte8 *p_texture,
           float alpha,
           T_byte8 masked,
           const T_raveVertex *topLeft,
           const T_raveVertex *bottomLeft,
           const T_raveVertex *bottomRight,
           const T_raveVertex *topRight) ;

/* Emit one sprite/object as a billboard quad (rave-6). p_picture is an AA
   column-sparse object picture (T_pictureRaster format); w,h are its logical
   texel size (padded to power-of-two internally). Same corner order as
   RaveViewEmitQuad; transparent texels are alpha-test-dropped. */
T_void RaveViewEmitSprite(
           T_byte8 *p_picture,
           T_word16 w,
           T_word16 h,
           const T_raveVertex *topLeft,
           const T_raveVertex *bottomLeft,
           const T_raveVertex *bottomRight,
           const T_raveVertex *topRight) ;

/* DIAGNOSTIC: draw a wall quad as a flat gouraud colour (no texture) -- used to
   colour wall segments by type to isolate the roof/window-seam artifact. */
T_void RaveViewEmitFlatDebug(float r, float g, float b,
           const T_raveVertex *tl, const T_raveVertex *bl,
           const T_raveVertex *br, const T_raveVertex *tr) ;

/* rave-8: sky backdrop quad. raw is a row-major 8-bit panorama (stride w),
   fully opaque; u,v are texel coords into it. Emit at far depth as the sky
   background (walls/ceilings occlude it). No-op off-RAVE. */
T_void RaveViewEmitSky(
           T_byte8 *raw,
           T_word16 w,
           T_word16 h,
           const T_raveVertex *topLeft,
           const T_raveVertex *bottomLeft,
           const T_raveVertex *bottomRight,
           const T_raveVertex *topRight) ;

/* Drop every cached TQATexture. MUST be called whenever the level's texture
   arrays (G_3d*TextureArray in 3D_IO.C) are freed/reloaded, else the cache
   keys (pixel pointers) go stale and a reused address returns the wrong
   texture. Wired into IUnlockPictures() (3D_IO.C) and RaveViewFinish(). */
T_void RaveViewFlushTextures(T_void) ;

/* TRUE once a usable RAVE engine/context is up (else the caller should fall
   back to the software renderer). */
E_Boolean RaveViewIsActive(T_void) ;

/* rave-7: everything the display compositor needs to lay the RAVE view over
   the UI, at the CURRENT (possibly menu-shrunk) view rect. Plain types only
   (no RAVE/3D internals leak to RESSCALE.C). */
typedef struct {
    T_word16 *pixels ;     /* RGB555 back-buffer read-back (bit15 unused)      */
    T_word16  stride ;     /* buffer row stride in pixels (full context width) */
    T_word16  viewX ;      /* visible view's left edge within the buffer       */
    T_word16  viewW ;      /* visible view width  (shrinks with menus)         */
    T_word16  viewH ;      /* visible view height                              */
    T_word16  flipY ;      /* 1 = buffer is bottom-up; composite reads flipped */
    T_byte8  *overlay ;    /* UI key layer, base 320x200: 0 = show 3D, !=0 UI  */
    T_word16  overlayW ;   /* overlay width  (base logical, e.g. 320)          */
    T_word16  overlayH ;   /* overlay height (base logical, e.g. 200)          */
    T_word16  overlayOrgX ;/* view's left in overlay/base coords (VIEW3D_ORIGIN_X)*/
    T_word16  overlayOrgY ;/* view's top  in overlay/base coords (VIEW3D_ORIGIN_Y)*/
} T_raveFrame ;

/* Fill *out with the last rendered RAVE frame + placement. Returns FALSE if
   none is available (RAVE inactive/disabled/no context/readback failed) -- the
   compositor then leaves the software view showing. */
E_Boolean RaveViewGetFrame(T_raveFrame *out) ;

/* Profiling hooks: RESSCALE.C calls CompositeBegin() just before the composite
   pixel loop and ProfileFrame() right after. ProfileFrame accumulates the RAVE
   per-phase timings (upload/render/readback/composite) and periodically
   MessageAdd()s an averaged ms/FPS line. No-ops on non-RAVE builds and when
   profiling is compiled out. */
T_void RaveViewProfileCompositeBegin(void) ;
T_void RaveViewProfileFrame(void) ;

/* ALT+F8 toggles the on-screen fps/phase readout (default off). RaveViewProfileIsOn
   lets RESSCALE gate its software-mode fps line on the same flag. No-op / FALSE on
   non-RAVE builds. */
E_Boolean RaveViewProfileToggle(void) ;
E_Boolean RaveViewProfileIsOn(void) ;

/* rave-7 offload: TRUE once RAVE is successfully presenting frames -- the
   caller (3D_VIEW.C) then skips the software 3D raster since the HW view is
   composited over it. FALSE until the first frame is confirmed presented, and
   again after any failed present, so the software renderer always covers gaps
   (never a permanent black 3D view). Always FALSE on non-RAVE builds. */
E_Boolean RaveViewIsPresenting(T_void) ;

/* rave-9 direct-to-screen: TRUE when RAVE owns the fullscreen present (swaps its
   own buffer at QARenderEnd). RESSCALE's ResScaleUpdate then skips its software
   scale + RAVE composite + SDL_Flip so RAVE and SDL don't both drive the screen.
   Always FALSE on non-RAVE builds and in the readback+composite path. */
E_Boolean RaveViewIsDirectPresenting(T_void) ;

/* rave-9: request an F8 screenshot of the RAVE direct-to-screen frame (the SDL
   SaveBMP path can't see RAVE's own buffer). No-op on non-RAVE builds. */
T_void RaveViewRequestShot(T_void) ;

/* rave-9: called by RESSCALE at present time when RaveViewIsDirectPresenting().
   ui8 is AA's 320x200 8-bit 2D frame (HUD/panels/weapon); RAVE draws it as a GPU
   quad over the 3D (transparent in the view rect) and swaps to the fullscreen
   display -- no readback, no CPU composite. No-op on non-RAVE builds. */
T_void RaveViewDrawUIAndPresent(const T_byte8 *ui8, int sw, int sh, int pitch) ;

/* Runtime software<->RAVE switch (in-game F-key hotkey + resolution.ini
   "renderer" option). SetEnabled(FALSE) makes the whole RAVE path dormant so
   the software renderer draws + shows, no rebuild. Toggle flips and returns the
   new state; GetEnabled reports it. All no-ops / FALSE on non-RAVE builds. */
T_void    RaveViewSetEnabled(E_Boolean on) ;
E_Boolean RaveViewGetEnabled(T_void) ;
E_Boolean RaveViewToggle(T_void) ;

/* Per-frame suspend: pass TRUE (e.g. while the escape/options menu is open over
   the full 3D view) to make RAVE dormant for that frame so the software
   renderer draws the frame + that UI on top instead of the composite hiding
   it. No-op on non-RAVE builds. */
T_void    RaveViewSetSuspended(E_Boolean s) ;

#endif /* _RAVE_VIEW_H_ */

/*-------------------------------------------------------------------------*
 * End of File:  RAVE_VIEW.H
 *-------------------------------------------------------------------------*/
