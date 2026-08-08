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
   pixel pointer (see IRaveGetTexture). No-op if the texture can't be uploaded
   or RAVE is inactive. */
T_void RaveViewEmitQuad(
           T_byte8 *p_texture,
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

/* Drop every cached TQATexture. MUST be called whenever the level's texture
   arrays (G_3d*TextureArray in 3D_IO.C) are freed/reloaded, else the cache
   keys (pixel pointers) go stale and a reused address returns the wrong
   texture. Wired into IUnlockPictures() (3D_IO.C) and RaveViewFinish(). */
T_void RaveViewFlushTextures(T_void) ;

/* TRUE once a usable RAVE engine/context is up (else the caller should fall
   back to the software renderer). */
E_Boolean RaveViewIsActive(T_void) ;

#endif /* _RAVE_VIEW_H_ */

/*-------------------------------------------------------------------------*
 * End of File:  RAVE_VIEW.H
 *-------------------------------------------------------------------------*/
