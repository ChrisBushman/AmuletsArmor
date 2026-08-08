/*-------------------------------------------------------------------------*
 * File:  RAVE_VIEW.C
 *-------------------------------------------------------------------------*/
/**
 *  QuickDraw 3D RAVE hardware-renderer backend (SCAFFOLD).
 *
 *  First alternate renderer, forked off master after the extract-3d-geo
 *  decoupling landed. Target: PowerBook G3 Lombard, ATI Rage LT Pro (Mac OS 9).
 *  rave_probe confirmed the card exposes "ATI 3DRage QD3D Rave Engine" with
 *  HW-accelerated Texture/Gouraud/TextureHQ/Blend/ZSort and ~5MB texture VRAM.
 *
 *  Plan: feed AA's sector geometry (the same portal walk the software renderer
 *  in 3D_VIEW.C does) to the RAVE engine as textured + gouraud-lit triangles,
 *  perspective-correct and GPU-accelerated, offloading the G3.
 *
 *  This file compiles on EVERY platform (CMake globs Source/*.C): the real
 *  RAVE code is gated on (macintosh && AA_RENDERER_RAVE); otherwise it is
 *  no-op stubs so non-RAVE builds are unaffected. On classic Mac OS 9 the
 *  normal game .mcp simply doesn't include this file; the RAVE build variant
 *  adds it + QuickDraw3DLib + QuickDraw3DRAVELib and defines AA_RENDERER_RAVE.
 *
 *<!-----------------------------------------------------------------------*/
#include "RAVE_VIEW.H"

#if defined(macintosh) && defined(AA_RENDERER_RAVE)

#include <Quickdraw.h>
#include <RAVE.h>
#include "3D_VIEW.H"   /* View3dGetView() -- camera */

/* The chosen drawing engine (prefer the HW one) and its draw context. */
static TQAEngine       *G_raveEngine  = NULL ;
static TQADrawContext  *G_raveContext = NULL ;

/*-------------------------------------------------------------------------*
 * IPickHardwareEngine -- enumerate the RAVE engines on a device and return
 * the best one: the first that HW-accelerates textured triangles
 * (kQAFast_Texture), else the first engine at all (software fallback).
 * Mirrors rave_probe's enumeration.
 *-------------------------------------------------------------------------*/
static TQAEngine *IPickHardwareEngine(const TQADevice *device)
{
    TQAEngine *best = NULL ;
    TQAEngine *e ;

    for (e = QADeviceGetFirstEngine(device) ;
         e != NULL ;
         e = QADeviceGetNextEngine(device, e)) {
        unsigned long fast = 0 ;
        QAEngineGestalt(e, kQAGestalt_FastFeatures, &fast) ;
        if (fast & kQAFast_Texture) {   /* real hardware textured fill */
            best = e ;
            break ;
        }
        if (best == NULL)
            best = e ;                  /* provisional: first (maybe software) */
    }
    return best ;
}

T_void RaveViewInit(T_void)
{
    TQADevice device ;

    device.deviceType     = kQADeviceGDevice ;
    device.device.gDevice = GetMainDevice() ;

    G_raveEngine = IPickHardwareEngine(&device) ;

    /* TODO(rave-1): create the draw context to match View3dSetSize()'s
       viewport rectangle:
         QADrawContextNew(&device, &clip, &rect, G_raveEngine,
                          kQAContext_DoubleBuffer | kQAContext_DeepZ,
                          &G_raveContext) ;
       Bind it to the game's window/DrawSprocket surface (the classic screen
       AA already draws its 2D UI onto), so the 3D view lands in the same
       VIEW3D_CLIP rectangle the software path uses. */

    /* TODO(rave-2): upload AA's wall/floor/ceiling textures once as TQATexture
       objects (kQATexture_None / kQAPixel_CL8 palettized -> or expand to RGB),
       keyed by the externalized texture-array indices (3D_IO.C
       G_3d{Lower,Upper,Main}TextureArray, floor/ceiling per sector). Cache so
       per-frame draws just reference the cached TQATexture. */
}

T_void RaveViewFinish(T_void)
{
    /* TODO(rave-3): free cached TQATextures. */
    if (G_raveContext != NULL) {
        QADrawContextDelete(G_raveContext) ;
        G_raveContext = NULL ;
    }
    G_raveEngine = NULL ;
}

T_void RaveViewDrawView(T_void)
{
    T_sword16 x, y ;
    T_sword32 height ;
    T_word16  angle ;

    if (G_raveContext == NULL)
        return ;                        /* caller falls back to software */

    View3dGetView(&x, &y, &height, &angle) ;   /* current camera */

    QARenderStart(G_raveContext, NULL, NULL) ;

    /* TODO(rave-4): set render state -- Z buffering (kQATag_ZFunction),
       perspective-correct + HQ (trilinear) texturing, blend mode for
       translucent walls/objects. */

    /* TODO(rave-5): traverse the visible set. Reuse 3D_VIEW.C's portal/sector
       walk (the PVS the software renderer already computes) and, for each
       visible surface, emit two TQAVertex triangles:
         - walls:   quad(main/upper/lower) -> textured (+gouraud from sector
                    light) via QADrawTriGouraud / QADrawTriMesh
         - floors/ceilings: sector polygon fan, textured + gouraud
       Screen-space X/Y come from the same projection; texture coords from the
       side/sector texture mapping; the cached TQATexture from rave-2. */

    /* TODO(rave-6): draw sprites/objects (ObjectsUpdateAnimation set) as
       textured, alpha-tested billboard quads. */

    QARenderEnd(G_raveContext, NULL) ;

    /* TODO(rave-7): present -- swap/blit the RAVE context into the classic
       screen's VIEW3D rectangle so ViewDraw()'s subsequent overhead-map +
       overlay + UI composite (on GRAPHICS_ACTUAL_SCREEN) still layer on top. */
}

E_Boolean RaveViewIsActive(T_void)
{
    return (G_raveContext != NULL) ? TRUE : FALSE ;
}

#else  /* not a RAVE build -- no-op stubs so this file links everywhere */

T_void    RaveViewInit(T_void)      {}
T_void    RaveViewFinish(T_void)    {}
T_void    RaveViewDrawView(T_void)  {}
E_Boolean RaveViewIsActive(T_void)  { return FALSE ; }

#endif /* macintosh && AA_RENDERER_RAVE */

/*-------------------------------------------------------------------------*
 * End of File:  RAVE_VIEW.C
 *-------------------------------------------------------------------------*/
