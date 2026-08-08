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
 *  This file compiles on EVERY platform (CMake globs the Source .C files):
 *  the real
 *  RAVE code is gated on (macintosh && AA_RENDERER_RAVE); otherwise it is
 *  no-op stubs so non-RAVE builds are unaffected. On classic Mac OS 9 the
 *  normal game .mcp simply doesn't include this file; the RAVE build variant
 *  adds it + QuickDraw3DLib + QuickDraw3DRAVELib and defines AA_RENDERER_RAVE.
 *
 *  Progress: rave-1 (draw context) + rave-2 (texture upload/cache) +
 *            rave-3 (teardown) + rave-4 (render state) DONE.
 *            rave-5..7 (geometry emission, sprites, present) still TODO.
 *
 *<!-----------------------------------------------------------------------*/
#include "RAVE_VIEW.H"

#if defined(macintosh) && defined(AA_RENDERER_RAVE)

#include <Quickdraw.h>
#include <RAVE.h>
#include "3D_VIEW.H"   /* View3dGetView() -- camera; VIEW3D_* viewport dims */
#include "3D_IO.H"     /* G_3d{Upper,Lower,Main,Floor,Ceiling}TextureArray   */
#include "GRAPHICS.H"  /* GrGetPalette(), T_palette                          */
#include "MEMORY.H"    /* MemAlloc()/MemFree()                               */
#include "PICS.H"      /* PictureGetWidth()/PictureGetHeight()               */

/*-------------------------------------------------------------------------*
 * RAVE engine + draw context (rave-1) and their backing device/clip.
 *-------------------------------------------------------------------------*/
static TQAEngine       *G_raveEngine  = NULL ;
static TQADrawContext  *G_raveContext = NULL ;
static TQADevice        G_raveDevice ;          /* main-screen GDevice       */
static TQAClip          G_raveClip ;            /* rectangular clip region   */
static RgnHandle        G_raveClipRgn = NULL ;  /* backing RgnHandle for it  */

/*-------------------------------------------------------------------------*
 * Texture cache (rave-2).  Keyed by the pixel pointer AA hands the software
 * renderer (G_3d*TextureArray[] entries); each maps to an uploaded
 * TQATexture.  Flushed whenever those arrays are freed (RaveViewFlushTextures,
 * called from IUnlockPictures) so a reused address can never alias a stale
 * texture.  Linear probe for now -- a level has a few hundred textures; a
 * hash is a later optimization if the per-surface lookup shows up on the G3.
 *-------------------------------------------------------------------------*/
typedef struct {
    T_byte8    *key ;      /* G_3d*TextureArray[] pixel pointer (identity)   */
    TQATexture *tex ;      /* uploaded RAVE texture (NULL == known-bad)      */
} T_raveTexEntry ;

static T_raveTexEntry  *G_raveTexCache = NULL ;
static T_word16         G_raveTexCount = 0 ;
static T_word16         G_raveTexMax   = 0 ;

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

/*-------------------------------------------------------------------------*
 * rave-1: RaveViewInit -- pick the engine and stand up the draw context.
 *-------------------------------------------------------------------------*/
T_void RaveViewInit(T_void)
{
    TQARect rect ;
    long    w, h ;
    TQAError err ;

    /* Idempotent: ViewInitialize can run more than once (view teardown +
       reinit on detail changes) -- tear any prior context down first. */
    RaveViewFinish() ;

    G_raveDevice.deviceType     = kQADeviceGDevice ;
    G_raveDevice.device.gDevice = GetMainDevice() ;

    G_raveEngine = IPickHardwareEngine(&G_raveDevice) ;
    if (G_raveEngine == NULL)
        return ;                        /* no RAVE at all -> software path */

    /* The 3D viewport, in the game's scaled buffer pixels.  VIEW3D_CLIP_LEFT/
       RIGHT and VIEW3D_HEIGHT are the runtime, detail-scaled dimensions the
       software renderer fills (see 3D_VIEW.C / View3dSetSize).
       NOTE(rave-7): anchored at the device origin for now.  Final on-screen
       placement (the ResScale window offset) and compositing with AA's
       SDL-drawn 2D UI are settled when the present strategy lands in rave-7;
       until then the context simply exists at the right *size* so rave-4/5
       can create textures and issue geometry. */
    w = (long)(VIEW3D_CLIP_RIGHT - VIEW3D_CLIP_LEFT) ;
    h = (long)VIEW3D_HEIGHT ;
    if (w <= 0) w = VIEW3D_WIDTH ;
    if (w <= 0) w = 1 ;
    if (h <= 0) h = 1 ;

    rect.left   = 0 ;
    rect.top    = 0 ;
    rect.right  = w ;
    rect.bottom = h ;

    /* Rectangular clip covering the context rect. */
    G_raveClipRgn = NewRgn() ;
    if (G_raveClipRgn != NULL) {
        Rect r ;
        r.left = 0 ; r.top = 0 ; r.right = (short)w ; r.bottom = (short)h ;
        SetRectRgn(G_raveClipRgn, r.left, r.top, r.right, r.bottom) ;
    }
    G_raveClip.clipType      = kQAClipRgn ;
    G_raveClip.clip.clipRgn  = G_raveClipRgn ;

    /* Double-buffered + Z-buffered: AA's world is a full 3D portal scene, so
       we want hidden-surface removal; the ATI engine accelerates ZSort. */
    err = QADrawContextNew(&G_raveDevice,
                           &rect,
                           &G_raveClip,
                           G_raveEngine,
                           kQAContext_DoubleBuffer | kQAContext_DeepZ,
                           &G_raveContext) ;
    if (err != kQANoErr) {
        G_raveContext = NULL ;          /* fall back to software */
        if (G_raveClipRgn != NULL) {
            DisposeRgn(G_raveClipRgn) ;
            G_raveClipRgn = NULL ;
        }
    }
}

/*-------------------------------------------------------------------------*
 * rave-2: texture upload + cache.
 *-------------------------------------------------------------------------*/

/* Convert one 8-bit palette index to ARGB16 (1-5-5-5).  AA palette entries
   are 6-bit VGA DAC values (0..63) -- the same (c & 0x3F) << 2 the SDL path
   uses to get 8-bit -- so 6-bit -> 5-bit is just >> 1.  Palette index 0 is
   AA's transparent color (masked walls/sprites test != 0), so it maps to a
   fully transparent texel; every other index sets the alpha bit. */
static unsigned short IRaveArgb16(const T_palette pal, T_byte8 idx)
{
    unsigned short r5, g5, b5 ;

    if (idx == 0)
        return 0 ;                      /* A=0 -> transparent */

    r5 = (unsigned short)((pal[idx][0] & 0x3F) >> 1) ;
    g5 = (unsigned short)((pal[idx][1] & 0x3F) >> 1) ;
    b5 = (unsigned short)((pal[idx][2] & 0x3F) >> 1) ;

    return (unsigned short)(0x8000 | (r5 << 10) | (g5 << 5) | b5) ;
}

/* Upload a single AA texture (column-major, 8-bit palettized, pointer past
   its 4-byte w/h header) to a new ARGB16 TQATexture.  Returns NULL if the
   texture is unusable (bad size / non-power-of-two / alloc or RAVE failure);
   NULL is cached too, so we don't retry a known-bad texture every frame. */
static TQATexture *IRaveUploadTexture(T_byte8 *p)
{
    T_word16        w, h ;
    unsigned short *buf ;
    T_palette       pal ;
    TQAImage        image ;
    TQATexture     *tex = NULL ;
    T_word16        col, row ;
    TQAError        err ;

    if ((p == NULL) || (p == G_textureNone + 4))
        return NULL ;                   /* the "no texture" sentinel */

    w = PictureGetWidth(p) ;
    h = PictureGetHeight(p) ;

    /* RAVE textures must be power-of-two and non-degenerate.  AA's textures
       already are (64x64, 128x128, ...); reject anything else rather than
       feed the engine an illegal image. */
    if ((w == 0) || (h == 0) || (w > 1024) || (h > 1024))
        return NULL ;
    if ((w & (w - 1)) != 0 || (h & (h - 1)) != 0)
        return NULL ;

    buf = (unsigned short *)MemAlloc((T_word32)w * (T_word32)h * 2UL) ;
    if (buf == NULL)
        return NULL ;

    GrGetPalette(0, 256, pal) ;

    /* AA stores textures column-major: texel(col,row) at p[col*h + row].
       Emit a standard row-major image: dst[row*w + col].  Texel orientation
       vs RAVE's UV space is handled by the UV mapping in rave-5. */
    for (col = 0 ; col < w ; col++) {
        T_byte8        *srcCol = p + ((T_word32)col * h) ;
        unsigned short *dstCol = buf + col ;
        for (row = 0 ; row < h ; row++)
            dstCol[(T_word32)row * w] = IRaveArgb16(pal, srcCol[row]) ;
    }

    image.width    = (long)w ;
    image.height   = (long)h ;
    image.rowBytes = (long)w * 2 ;
    image.pixmap   = buf ;

    /* Default flags copy the image into engine/VRAM storage, so buf is ours
       to free immediately after. */
    err = QATextureNew(G_raveEngine, kQATexture_None, kQAPixel_ARGB16,
                       &image, &tex) ;
    MemFree(buf) ;

    if (err != kQANoErr)
        return NULL ;
    return tex ;
}

/* Look a texture up by its pixel pointer, uploading + caching on first use. */
static TQATexture *IRaveGetTexture(T_byte8 *p)
{
    T_word16 i ;

    if ((p == NULL) || (p == G_textureNone + 4))
        return NULL ;

    for (i = 0 ; i < G_raveTexCount ; i++) {
        if (G_raveTexCache[i].key == p)
            return G_raveTexCache[i].tex ;   /* may be NULL == known-bad */
    }

    /* Grow the cache if needed (double, starting at 64). */
    if (G_raveTexCount >= G_raveTexMax) {
        T_word16        newMax = (T_word16)(G_raveTexMax ? G_raveTexMax * 2 : 64) ;
        T_raveTexEntry *grown  =
            (T_raveTexEntry *)MemAlloc((T_word32)newMax * sizeof(T_raveTexEntry)) ;
        if (grown == NULL)
            return NULL ;                /* out of memory -> skip this texture */
        if (G_raveTexCache != NULL) {
            for (i = 0 ; i < G_raveTexCount ; i++)
                grown[i] = G_raveTexCache[i] ;
            MemFree(G_raveTexCache) ;
        }
        G_raveTexCache = grown ;
        G_raveTexMax   = newMax ;
    }

    G_raveTexCache[G_raveTexCount].key = p ;
    G_raveTexCache[G_raveTexCount].tex = IRaveUploadTexture(p) ;
    return G_raveTexCache[G_raveTexCount++].tex ;
}

T_void RaveViewFlushTextures(T_void)
{
    T_word16 i ;

    if (G_raveTexCache != NULL) {
        for (i = 0 ; i < G_raveTexCount ; i++) {
            if ((G_raveTexCache[i].tex != NULL) && (G_raveEngine != NULL))
                QATextureDelete(G_raveEngine, G_raveTexCache[i].tex) ;
        }
        MemFree(G_raveTexCache) ;
        G_raveTexCache = NULL ;
    }
    G_raveTexCount = 0 ;
    G_raveTexMax   = 0 ;
}

/*-------------------------------------------------------------------------*
 * rave-4: render state.
 *
 * RAVE context state variables persist for the life of the context, but we
 * (re)establish them every frame so the pipeline is robust to any driver that
 * resets defaults at QARenderStart, at negligible cost (~10 QASet calls).
 * kQATag_ColorBG_* MUST be set BEFORE QARenderStart -- that is when the
 * NULL-initialContext render clears the color/Z buffers -- so it lives in its
 * own helper; the rest are set right after QARenderStart, before any draw.
 *-------------------------------------------------------------------------*/

/* Background: clear to opaque black each frame. (rave-5 may later paint a sky
   into ceiling sectors instead of relying on the clear.) */
static T_void IRaveSetBackground(TQADrawContext *ctx)
{
    QASetFloat(ctx, kQATag_ColorBG_a, 1.0f) ;
    QASetFloat(ctx, kQATag_ColorBG_r, 0.0f) ;
    QASetFloat(ctx, kQATag_ColorBG_g, 0.0f) ;
    QASetFloat(ctx, kQATag_ColorBG_b, 0.0f) ;
}

static T_void IRaveSetRenderState(TQADrawContext *ctx)
{
    /* Depth: standard z-buffer -- nearer (smaller z) wins, and write z so
       later triangles are occluded. AA's world is a full portal scene, so we
       want real hidden-surface removal (the ATI engine accelerates ZSort). */
    QASetInt(ctx, kQATag_ZFunction,   kQAZFunction_LT) ;
    QASetInt(ctx, kQATag_ZBufferMask, kQAZBufferMask_Enable) ;

    /* Use z (not 1/w) for HSR; texture perspective-correctness comes from the
       per-vertex w/invW that rave-5 supplies, independent of this. */
    QASetInt(ctx, kQATag_PerspectiveZ, kQAPerspectiveZ_Off) ;

    /* Best filtering -- the probe confirmed the Rage LT Pro HW-accelerates
       TextureHQ. (Switch to kQATextureFilter_Fast for the crisp, unfiltered
       classic-pixel look if that reads better on hardware.) */
    QASetInt(ctx, kQATag_TextureFilter, kQATextureFilter_Best) ;

    /* Modulate the texture by the per-vertex diffuse color so rave-5's gouraud
       sector lighting darkens/brightens the texels. */
    QASetInt(ctx, kQATag_TextureOp, kQATextureOp_Modulate) ;

    /* Alpha-test cutout: our ARGB16 textures put alpha=0 on palette index 0
       (AA's transparent color) and alpha=1 on everything else, so "draw where
       alpha > 0.5" discards exactly the masked texels for solid AND masked
       surfaces alike -- crisp, no blend cost. Translucent surfaces (rave-5/6)
       layer real blending on top of this. */
    QASetInt(ctx,   kQATag_AlphaTestFunc, kQAAlphaTest_GT) ;
    QASetFloat(ctx, kQATag_AlphaTestRef,  0.5f) ;

    /* Interpolate blending (src-over-dst by alpha) is the default for the
       translucent draws rave-5/6 will flag per-primitive; opaque draws pass
       kQATriFlags_None and ignore it. Write all color channels. */
    QASetInt(ctx, kQATag_Blend,       kQABlend_Interpolate) ;
    QASetInt(ctx, kQATag_ChannelMask,
             kQAChannelMask_r | kQAChannelMask_g |
             kQAChannelMask_b | kQAChannelMask_a) ;
}

/*-------------------------------------------------------------------------*
 * rave-3: teardown (complete). Drop textures, delete the context, dispose
 * the clip region, forget the engine. Safe to call when nothing is up
 * (RaveViewInit calls it to be idempotent).
 *-------------------------------------------------------------------------*/
T_void RaveViewFinish(T_void)
{
    RaveViewFlushTextures() ;

    if (G_raveContext != NULL) {
        QADrawContextDelete(G_raveContext) ;
        G_raveContext = NULL ;
    }
    if (G_raveClipRgn != NULL) {
        DisposeRgn(G_raveClipRgn) ;
        G_raveClipRgn = NULL ;
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

    /* rave-4: background clear color must be set before QARenderStart, which
       (with a NULL initial context) clears the color + Z buffers. */
    IRaveSetBackground(G_raveContext) ;

    QARenderStart(G_raveContext, NULL, NULL) ;

    /* rave-4: (re)establish the depth/texture/blend render state for the frame. */
    IRaveSetRenderState(G_raveContext) ;

    /* TODO(rave-5): traverse the visible set. Reuse 3D_VIEW.C's portal/sector
       walk (the PVS the software renderer already computes) and, for each
       visible surface, look up its texture with IRaveGetTexture() (rave-2,
       done) and emit two TQAVertex triangles:
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
       overlay + UI composite (on GRAPHICS_ACTUAL_SCREEN) still layer on top.
       This is also where the final on-screen placement of the rave-1 rect
       (the ResScale window offset) is resolved. */
}

E_Boolean RaveViewIsActive(T_void)
{
    return (G_raveContext != NULL) ? TRUE : FALSE ;
}

#else  /* not a RAVE build -- no-op stubs so this file links everywhere */

T_void    RaveViewInit(T_void)          {}
T_void    RaveViewFinish(T_void)        {}
T_void    RaveViewDrawView(T_void)      {}
T_void    RaveViewFlushTextures(T_void) {}
E_Boolean RaveViewIsActive(T_void)      { return FALSE ; }

#endif /* macintosh && AA_RENDERER_RAVE */

/*-------------------------------------------------------------------------*
 * End of File:  RAVE_VIEW.C
 *-------------------------------------------------------------------------*/
