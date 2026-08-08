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
#include "ENDIAN_AA.H" /* EndianLE16() -- sprite raster column offsets       */

/*-------------------------------------------------------------------------*
 * RAVE engine + draw context (rave-1) and their backing device/clip.
 *-------------------------------------------------------------------------*/
static TQAEngine       *G_raveEngine  = NULL ;
static TQADrawContext  *G_raveContext = NULL ;
static TQADevice        G_raveDevice ;          /* main-screen GDevice       */
static TQAClip          G_raveClip ;            /* rectangular clip region   */
static RgnHandle        G_raveClipRgn = NULL ;  /* backing RgnHandle for it  */

/*-------------------------------------------------------------------------*
 * rave-7: readback frame. The context renders to the VRAM back buffer with
 * kQATag_DontSwap (HW-accelerated, never swapped to the visible screen so it
 * doesn't fight SDL). RaveViewFrameEnd reads that back buffer via
 * QAAccessDrawBuffer and converts it to a canonical RGB555 buffer; ResScale's
 * composite hook (RESSCALE.C) scales it into AA's SDL display at the VIEW3D
 * rect, on top of the UI, just before SDL_Flip.
 *-------------------------------------------------------------------------*/
static T_word16 *G_raveFrameBuf   = NULL ;  /* W*H RGB555 (bit15 unused)      */
static T_word16  G_raveFrameW     = 0 ;     /* == context width  (VIEW3D_WIDTH)*/
static T_word16  G_raveFrameH     = 0 ;     /* == context height (VIEW3D_HEIGHT)*/
static E_Boolean G_raveFrameReady = FALSE ; /* a frame is in G_raveFrameBuf   */

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

    /* The 3D viewport in the game's logical (detail-scaled) pixels. The context
       is exactly the VIEW3D_WIDTH x VIEW3D_HEIGHT logical view; the taps emit
       screen X in that same 0..VIEW3D_WIDTH space, and the composite hook maps
       this whole buffer onto the on-screen VIEW3D rect (ResScale offset/scale).
       Renders to an offscreen VRAM back buffer (DontSwap in rave-4) and is read
       back for compositing -- see rave-7. */
    w = (long)VIEW3D_WIDTH ;
    h = (long)VIEW3D_HEIGHT ;
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
        return ;
    }

    /* rave-7: readback buffer matching the context size (RGB555). */
    G_raveFrameW   = (T_word16)w ;
    G_raveFrameH   = (T_word16)h ;
    G_raveFrameBuf = (T_word16 *)MemAlloc((T_word32)w * (T_word32)h * 2UL) ;
    G_raveFrameReady = FALSE ;
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

/* Smallest power-of-two >= v (>=1). RAVE textures must be power-of-two; AA
   wall/floor textures already are, but sprites are arbitrary-sized and get
   padded up to this in a transparent POT buffer. */
static T_word16 INextPow2(T_word16 v)
{
    T_word16 p = 1 ;
    while (p < v)
        p = (T_word16)(p << 1) ;
    return p ;
}

/* AA sprite/object picture-raster column entry (private on-disk format in
   3D_VIEW.C's T_pictureRaster; redeclared here to decode sprites). A picture
   is width entries of these, followed by the per-column pixel runs. */
typedef struct {
    T_word16 offset ;   /* little-endian byte offset of this column's pixels */
    T_byte8  start ;    /* top texel row of the opaque run (255 = empty col)  */
    T_byte8  end ;      /* bottom texel row of the opaque run                 */
} T_raveRaster ;

/* Upload an AA sprite (T_pictureRaster column-sparse, w x h logical texels)
   into a power-of-two ARGB16 TQATexture, padded transparent. Sprite occupies
   [0..w) x [0..h) of the POT texture; the emitter normalizes u,v by the POT
   size. Empty columns / index-0 texels / rows outside [start..end] are
   transparent. Returns NULL (cached) if unusable. */
static TQATexture *IRaveUploadSprite(T_byte8 *p_picture, T_word16 w, T_word16 h)
{
    T_word16        potW, potH ;
    unsigned short *buf ;
    T_palette       pal ;
    TQAImage        image ;
    TQATexture     *tex = NULL ;
    T_word16        col ;
    const T_raveRaster *cols = (const T_raveRaster *)p_picture ;
    TQAError        err ;

    if ((p_picture == NULL) || (w == 0) || (h == 0) || (w > 1024) || (h > 1024))
        return NULL ;

    potW = INextPow2(w) ;
    potH = INextPow2(h) ;

    buf = (unsigned short *)MemAlloc((T_word32)potW * (T_word32)potH * 2UL) ;
    if (buf == NULL)
        return NULL ;
    /* transparent everywhere first */
    {
        T_word32 n = (T_word32)potW * (T_word32)potH, k ;
        for (k = 0 ; k < n ; k++)
            buf[k] = 0 ;
    }

    GrGetPalette(0, 256, pal) ;

    /* Decode each column's opaque run into the row-major POT buffer. */
    for (col = 0 ; col < w ; col++) {
        T_byte8 st = cols[col].start ;
        T_byte8 en = cols[col].end ;
        T_byte8 *colBase ;
        T_word16 r ;

        if (st == 255)
            continue ;                  /* empty column */
        colBase = p_picture + EndianLE16(cols[col].offset) - st - 4 ;
        if (en >= h)
            en = (T_byte8)(h - 1) ;
        for (r = st ; r <= en ; r++)
            buf[(T_word32)r * potW + col] = IRaveArgb16(pal, colBase[r]) ;
    }

    image.width    = (long)potW ;
    image.height   = (long)potH ;
    image.rowBytes = (long)potW * 2 ;
    image.pixmap   = buf ;

    err = QATextureNew(G_raveEngine, kQATexture_None, kQAPixel_ARGB16,
                       &image, &tex) ;
    MemFree(buf) ;

    if (err != kQANoErr)
        return NULL ;
    return tex ;
}

/* Cache lookup by key pointer; returns the entry index or 0xFFFF. */
static T_word16 IRaveCacheFind(T_byte8 *key)
{
    T_word16 i ;
    for (i = 0 ; i < G_raveTexCount ; i++) {
        if (G_raveTexCache[i].key == key)
            return i ;
    }
    return 0xFFFF ;
}

/* Add (key, tex) to the cache (growing it), caching NULL too so a known-bad
   texture isn't retried every frame. Returns tex. */
static TQATexture *IRaveCacheAdd(T_byte8 *key, TQATexture *tex)
{
    if (G_raveTexCount >= G_raveTexMax) {
        T_word16        newMax = (T_word16)(G_raveTexMax ? G_raveTexMax * 2 : 64) ;
        T_raveTexEntry *grown  =
            (T_raveTexEntry *)MemAlloc((T_word32)newMax * sizeof(T_raveTexEntry)) ;
        T_word16        i ;
        if (grown == NULL)
            return tex ;                /* out of memory: use it, just don't cache */
        if (G_raveTexCache != NULL) {
            for (i = 0 ; i < G_raveTexCount ; i++)
                grown[i] = G_raveTexCache[i] ;
            MemFree(G_raveTexCache) ;
        }
        G_raveTexCache = grown ;
        G_raveTexMax   = newMax ;
    }
    G_raveTexCache[G_raveTexCount].key = key ;
    G_raveTexCache[G_raveTexCount].tex = tex ;
    G_raveTexCount++ ;
    return tex ;
}

/* Flat wall/floor texture, uploaded + cached on first use (keyed by pointer). */
static TQATexture *IRaveGetTexture(T_byte8 *p)
{
    T_word16 i ;

    if ((p == NULL) || (p == G_textureNone + 4))
        return NULL ;

    i = IRaveCacheFind(p) ;
    if (i != 0xFFFF)
        return G_raveTexCache[i].tex ;   /* may be NULL == known-bad */

    return IRaveCacheAdd(p, IRaveUploadTexture(p)) ;
}

/* Sprite picture (column-sparse), uploaded + cached on first use. */
static TQATexture *IRaveGetSprite(T_byte8 *p_picture, T_word16 w, T_word16 h)
{
    T_word16 i ;

    if (p_picture == NULL)
        return NULL ;

    i = IRaveCacheFind(p_picture) ;
    if (i != 0xFFFF)
        return G_raveTexCache[i].tex ;

    return IRaveCacheAdd(p_picture, IRaveUploadSprite(p_picture, w, h)) ;
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

    /* rave-7: never swap the double buffer to the visible screen -- we composite
       the back buffer into AA's SDL display ourselves. Without this, QARenderEnd
       would flip the RAVE frame straight onto the screen and fight SDL. */
    QASetInt(ctx, kQATag_DontSwap, 1) ;
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
    if (G_raveFrameBuf != NULL) {
        MemFree(G_raveFrameBuf) ;
        G_raveFrameBuf = NULL ;
    }
    G_raveFrameW = G_raveFrameH = 0 ;
    G_raveFrameReady = FALSE ;
    G_raveEngine = NULL ;
}

/*-------------------------------------------------------------------------*
 * rave-5: frame begin/end + geometry emission.
 *
 * VIEW.C brackets View3dDrawView() with FrameBegin/FrameEnd. The 3D_VIEW.C
 * BSP walk runs as normal (still software-rasterizing for now -- see rave-7)
 * and, per visible wall, calls RaveViewEmitQuad() with the corners it already
 * projected. FrameBegin opens the render + clear; FrameEnd finishes it.
 *-------------------------------------------------------------------------*/
T_void RaveViewFrameBegin(T_void)
{
    if (G_raveContext == NULL)
        return ;

    /* rave-4: background must be set before QARenderStart, which (NULL initial
       context) clears the color + Z buffers to it. */
    IRaveSetBackground(G_raveContext) ;
    QARenderStart(G_raveContext, NULL, NULL) ;
    IRaveSetRenderState(G_raveContext) ;
}

T_void RaveViewFrameEnd(T_void)
{
    TQAPixelBuffer pb ;
    TQAError       err ;

    if (G_raveContext == NULL)
        return ;

    QARenderEnd(G_raveContext, NULL) ;      /* DontSwap: stays in back buffer */

    /* rave-7: read the rendered back buffer and convert it to canonical RGB555
       in G_raveFrameBuf. ResScaleRaveComposite (RESSCALE.C) then scales that
       into AA's SDL display at the VIEW3D rect, over the UI, before SDL_Flip. */
    G_raveFrameReady = FALSE ;
    if (G_raveFrameBuf == NULL)
        return ;

    err = QAAccessDrawBuffer(G_raveContext, &pb) ;
    if (err != kQANoErr)
        return ;

    {
        T_word16 rows = G_raveFrameH, cols = G_raveFrameW ;
        T_word16 r, c ;

        if ((long)cols > pb.width)  cols = (T_word16)pb.width ;
        if ((long)rows > pb.height) rows = (T_word16)pb.height ;

        for (r = 0 ; r < rows ; r++) {
            T_byte8  *srcRow = (T_byte8 *)pb.baseAddr + (T_word32)r * pb.rowBytes ;
            T_word16 *dstRow = G_raveFrameBuf + (T_word32)r * G_raveFrameW ;

            switch (pb.pixelType) {
            case kQAPixel_RGB16:            /* already 5-5-5 (bit15 unused) */
            case kQAPixel_ARGB16: {
                const unsigned short *s = (const unsigned short *)srcRow ;
                for (c = 0 ; c < cols ; c++)
                    dstRow[c] = (T_word16)(s[c] & 0x7FFF) ;
                break ; }
            case kQAPixel_RGB16_565: {      /* 5-6-5 -> drop green LSB */
                const unsigned short *s = (const unsigned short *)srcRow ;
                for (c = 0 ; c < cols ; c++) {
                    unsigned short p = s[c] ;
                    dstRow[c] = (T_word16)((((p >> 11) & 0x1F) << 10) |
                                           (((p >>  6) & 0x1F) <<  5) |
                                            ( p        & 0x1F)) ;
                }
                break ; }
            case kQAPixel_RGB32:            /* 8-8-8 -> top 5 bits each */
            case kQAPixel_ARGB32: {
                const unsigned long *s = (const unsigned long *)srcRow ;
                for (c = 0 ; c < cols ; c++) {
                    unsigned long p = s[c] ;
                    dstRow[c] = (T_word16)((((p >> 19) & 0x1F) << 10) |
                                           (((p >> 11) & 0x1F) <<  5) |
                                            ((p >>  3) & 0x1F)) ;
                }
                break ; }
            default:
                for (c = 0 ; c < cols ; c++)
                    dstRow[c] = 0 ;
                break ;
            }
        }
    }

    QAAccessDrawBufferEnd(G_raveContext, NULL) ;
    G_raveFrameReady = TRUE ;
}

/* rave-7: hand the composited RGB555 frame (+ its dimensions) to the display
   compositor. Returns FALSE when no RAVE frame is available (inactive build,
   no context, or readback failed) -- caller then shows the software frame. */
E_Boolean RaveViewGetFrame(T_word16 **p_base, T_word16 *p_w, T_word16 *p_h)
{
    if (!G_raveFrameReady || (G_raveFrameBuf == NULL))
        return FALSE ;
    if (p_base) *p_base = G_raveFrameBuf ;
    if (p_w)    *p_w    = G_raveFrameW ;
    if (p_h)    *p_h    = G_raveFrameH ;
    return TRUE ;
}

/* Core: bind an already-resolved TQATexture and draw the quad (TL,BL,BR,TR)
   as 2 textured, gouraud-lit triangles. u,v are texel coords normalized by
   (normW,normH) -- the *texture's* real dimensions (POT), which for a padded
   sprite differ from its logical w x h. light -> kd_r/g/b (Modulate). */
static T_void IRaveDrawTexturedQuad(
           TQATexture *tex, float normW, float normH,
           const T_raveVertex *topLeft,
           const T_raveVertex *bottomLeft,
           const T_raveVertex *bottomRight,
           const T_raveVertex *topRight)
{
    TQAVTexture         v[4] ;
    const T_raveVertex *src[4] ;
    float               invNW = 1.0f / normW ;
    float               invNH = 1.0f / normH ;
    int                 i ;

    QASetPtr(G_raveContext, kQATag_Texture, tex) ;

    src[0] = topLeft ; src[1] = bottomLeft ;
    src[2] = bottomRight ; src[3] = topRight ;

    for (i = 0 ; i < 4 ; i++) {
        v[i].x    = src[i]->x ;
        v[i].y    = src[i]->y ;
        v[i].z    = src[i]->z ;
        v[i].invW = src[i]->invW ;
        /* RAVE wants u/w, v/w; it divides by interpolated invW per pixel. */
        v[i].uOverW = (src[i]->u * invNW) * src[i]->invW ;
        v[i].vOverW = (src[i]->v * invNH) * src[i]->invW ;
        /* Under kQATextureOp_Modulate, r/g/b are ignored; light goes in kd_*. */
        v[i].r = v[i].g = v[i].b = 0.0f ;
        v[i].a = 1.0f ;
        v[i].kd_r = v[i].kd_g = v[i].kd_b = src[i]->light ;
        v[i].ks_r = v[i].ks_g = v[i].ks_b = 0.0f ;
    }

    QADrawTriTexture(G_raveContext, &v[0], &v[1], &v[2], kQATriFlags_None) ;
    QADrawTriTexture(G_raveContext, &v[0], &v[2], &v[3], kQATriFlags_None) ;
}

/* Walls + floors/ceilings: flat AA texture (pointer past its 4-byte header). */
T_void RaveViewEmitQuad(
           T_byte8 *p_texture,
           const T_raveVertex *topLeft,
           const T_raveVertex *bottomLeft,
           const T_raveVertex *bottomRight,
           const T_raveVertex *topRight)
{
    TQATexture *tex ;

    if (G_raveContext == NULL)
        return ;
    tex = IRaveGetTexture(p_texture) ;
    if (tex == NULL)
        return ;                        /* untextured / bad -> skip */

    IRaveDrawTexturedQuad(tex,
        (float)PictureGetWidth(p_texture), (float)PictureGetHeight(p_texture),
        topLeft, bottomLeft, bottomRight, topRight) ;
}

/* Sprites/objects: column-sparse picture-raster (rave-6). w,h are the sprite's
   logical texel size; the texture is padded up to POT, so u,v (0..w / 0..h)
   are normalized by the POT size. Alpha-test cutout (rave-4) drops the
   transparent texels. */
T_void RaveViewEmitSprite(
           T_byte8 *p_picture, T_word16 w, T_word16 h,
           const T_raveVertex *topLeft,
           const T_raveVertex *bottomLeft,
           const T_raveVertex *bottomRight,
           const T_raveVertex *topRight)
{
    TQATexture *tex ;

    if (G_raveContext == NULL)
        return ;
    tex = IRaveGetSprite(p_picture, w, h) ;
    if (tex == NULL)
        return ;

    IRaveDrawTexturedQuad(tex,
        (float)INextPow2(w), (float)INextPow2(h),
        topLeft, bottomLeft, bottomRight, topRight) ;
}

E_Boolean RaveViewIsActive(T_void)
{
    return (G_raveContext != NULL) ? TRUE : FALSE ;
}

#else  /* not a RAVE build -- no-op stubs so this file links everywhere */

T_void    RaveViewInit(T_void)          {}
T_void    RaveViewFinish(T_void)        {}
T_void    RaveViewFrameBegin(T_void)    {}
T_void    RaveViewFrameEnd(T_void)      {}
T_void    RaveViewFlushTextures(T_void) {}
E_Boolean RaveViewIsActive(T_void)      { return FALSE ; }

E_Boolean RaveViewGetFrame(T_word16 **p_base, T_word16 *p_w, T_word16 *p_h)
{
    (void)p_base ; (void)p_w ; (void)p_h ;
    return FALSE ;
}

T_void RaveViewEmitQuad(
           T_byte8 *p_texture,
           const T_raveVertex *topLeft,
           const T_raveVertex *bottomLeft,
           const T_raveVertex *bottomRight,
           const T_raveVertex *topRight)
{
    (void)p_texture ; (void)topLeft ; (void)bottomLeft ;
    (void)bottomRight ; (void)topRight ;
}

T_void RaveViewEmitSprite(
           T_byte8 *p_picture, T_word16 w, T_word16 h,
           const T_raveVertex *topLeft,
           const T_raveVertex *bottomLeft,
           const T_raveVertex *bottomRight,
           const T_raveVertex *topRight)
{
    (void)p_picture ; (void)w ; (void)h ; (void)topLeft ; (void)bottomLeft ;
    (void)bottomRight ; (void)topRight ;
}

#endif /* macintosh && AA_RENDERER_RAVE */

/*-------------------------------------------------------------------------*
 * End of File:  RAVE_VIEW.C
 *-------------------------------------------------------------------------*/
