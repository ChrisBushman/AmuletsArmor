/*-------------------------------------------------------------------------*
 * File:  RAVE_VIEW.H
 *-------------------------------------------------------------------------*/
/**
 *  QuickDraw 3D RAVE hardware-renderer backend (interface).
 *
 *  Alternative to the software 3D renderer (3D_VIEW.C's View3dDrawView).
 *  Enabled with -DAA_RENDERER_RAVE on the classic-Mac RAVE build (e.g. the
 *  PowerBook G3 / ATI Rage LT Pro target). On every other build these are
 *  harmless no-ops, so callers (VIEW.C) link unconditionally.
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

/* Render the current 3D view through RAVE and present it. Replaces
   View3dDrawView()+View3dDisplayView() when AA_RENDERER_RAVE is defined. */
T_void RaveViewDrawView(T_void) ;

/* TRUE once a usable RAVE engine/context is up (else the caller should fall
   back to the software renderer). */
E_Boolean RaveViewIsActive(T_void) ;

#endif /* _RAVE_VIEW_H_ */

/*-------------------------------------------------------------------------*
 * End of File:  RAVE_VIEW.H
 *-------------------------------------------------------------------------*/
