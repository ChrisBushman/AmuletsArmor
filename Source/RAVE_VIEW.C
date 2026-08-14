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
#include "COLOR.H"     /* ColorGetTintDelta() -- reapply live palette tint   */
#include "MEMORY.H"    /* MemAlloc()/MemFree()                               */
#include "PICS.H"      /* PictureGetWidth()/PictureGetHeight()               */
#include "ENDIAN_AA.H" /* EndianLE16() -- sprite raster column offsets       */
#include "MESSAGE.H"   /* MessageAdd() -- on-screen profiler readout         */
#include <Timer.h>     /* Microseconds() -- per-phase RAVE profiling         */
#include <stdio.h>     /* sprintf() for the profiler line                    */

/*-------------------------------------------------------------------------*
 * RAVE_PROFILE: lightweight per-phase timing. Accumulates upload / render /
 * readback / composite microseconds over ~1s and MessageAdd()s an average +
 * FPS line (shows in the on-screen log + F8 screenshots), so we can see which
 * phase dominates before optimizing. Compile-time gated -- set to 0 to strip.
 * RESSCALE.C feeds composite time in via RaveViewProfileFrame() at frame end.
 *-------------------------------------------------------------------------*/
#define RAVE_PROFILE 1

#if RAVE_PROFILE
static unsigned long G_profUploadUs   = 0 ;   /* this frame (reset FrameBegin) */
static T_word16      G_profUploads    = 0 ;   /* this frame                    */
static unsigned long G_profRenderUs   = 0 ;   /* this frame (FrameEnd)         */
static unsigned long G_profReadbackUs = 0 ;   /* this frame (FrameEnd)         */
static unsigned long G_accUpload = 0, G_accRender = 0 ;
static unsigned long G_accReadback = 0, G_accComposite = 0 ;
static unsigned long G_accUploads = 0, G_accFrames = 0 ;
static unsigned long G_profLastReportUs = 0 ;
static unsigned long G_profCompositeStart = 0 ;   /* set by CompositeBegin */
static E_Boolean     G_raveProfileOn = FALSE ;    /* runtime on/off (F8); default off */

/* rave-9 perf pass: the geometry/emit phase (FrameBegin->FrameEnd = BSP walk +
   RAVE emit) and the "rest" (FrameEnd->present = software UI draw + game logic),
   plus cheap per-frame WORK counters (emit quads, shade strips, textured-tri draw
   calls, and total texture-cache scan iterations) -- to find where the unmeasured
   ~180ms frame goes and whether the O(n) cache scan / strip-split is the cost. */
static unsigned long G_profGeoUs = 0, G_profRestUs = 0 ;
static unsigned long G_profFrameBeginUs = 0, G_profFrameEndUs = 0 ;
static unsigned long G_profDrawUs = 0 ;   /* time in IRaveDrawTexturedQuad this frame */
static T_word32      G_profQuads = 0, G_profStrips = 0, G_profDraws = 0 ;
static T_word32      G_profCacheScans = 0 ;
static unsigned long G_accGeo = 0, G_accRest = 0, G_accDraw = 0 ;
static T_word32      G_accQuads = 0, G_accStrips = 0, G_accDraws = 0 ;
static unsigned long G_accCacheScans = 0 ;

static unsigned long IProfNowUs(void)
{
    UnsignedWide t ;
    Microseconds(&t) ;
    return t.lo ;   /* 32-bit us; unsigned subtraction handles the ~71min wrap */
}
#endif

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

/* Runtime software<->RAVE switch (resolution.ini renderer=). When FALSE the whole
   RAVE path goes dormant -- IsActive/IsPresenting/GetFrame report off,
   FrameBegin/End/Emit no-op -- so the software renderer draws and is shown, with
   no rebuild. Default OFF for now: RAVE outperforms software in the 3D view, but
   the in-game 2D screens (townUI/shops/guild) still freeze in direct-present
   mode (WIP), so software stays the safe boot default. Opt in with
   renderer=rave. There is no in-game toggle anymore -- ALT+F12 is the shot. */
static E_Boolean G_raveEnabled    = FALSE ;

/* Per-frame suspend: the game sets this (e.g. while the escape/options menu is
   open over the full 3D view) so RAVE goes dormant and the software renderer
   draws the frame with that UI on top. Separate from G_raveEnabled so it
   doesn't fight the F12/ini choice. */
static E_Boolean G_raveSuspended  = FALSE ;

/* rave-9 DIRECT-TO-SCREEN: when TRUE, RAVE swaps its own double buffer straight
   to the fullscreen display (kQATag_DontSwap=0) at QARenderEnd -- no VRAM
   readback, no CPU composite (the profiler showed those ~20ms + ~38ms are the
   whole RAVE penalty). RESSCALE then skips its scale/composite/SDL_Flip. The 2D
   UI is drawn by RAVE as a full-screen textured quad over the 3D. SPIKE STAGE:
   present only (3D fills the context, no UI quad yet) to prove RAVE can own the
   fullscreen display without fighting SDL. Requires fullscreen. */
static E_Boolean G_raveDirectPresent = TRUE ;

/* rave-9: screen-space transform for emitted 3D vertices. The BSP walk emits x,y
   in the VIEW3D_WIDTH x VIEW3D_HEIGHT view space. In the readback path the context
   IS that size (identity: Org=0, Scl=1). In direct-to-screen the context is the
   full display, so the 3D is placed into the on-screen view rect: screen = Org +
   emitted * Scl (Scl = viewRectSize / VIEW3D size). This also renders the geometry
   at native screen resolution -- sharper than the old 300x140 upscale. */
static float G_raveViewOrgX = 0.0f, G_raveViewOrgY = 0.0f ;
static float G_raveViewSclX = 1.0f, G_raveViewSclY = 1.0f ;

/* rave-9: the RAVE draw context renders to the PHYSICAL display (GetMainDevice),
   but SDL's surface (G_real) is often smaller than the panel and SDL centers it
   there. Software presents centered; RAVE would render at the device top-left.
   So place the context's surface-sized buffer at the same centre (G_raveScrOff*)
   and offset all content into it. G_raveDst* is the content (letterbox) rect in
   DEVICE coords -- used by both the 3D transform and the UI quad so they align. */
static int G_raveScrOffX = 0, G_raveScrOffY = 0 ;
static int G_raveDstX = 0, G_raveDstY = 0, G_raveDstW = 0, G_raveDstH = 0 ;

/* rave-9: TRUE while a 3D render pass is open this frame (set by RaveViewFrameBegin,
   cleared once presented). When FALSE at present time we're on a 2D-only screen
   (main-menu-in-game, townUI, shops, guild, bank, full-screen menus): the BSP walk
   never ran, so no pass exists. RaveViewDrawUIAndPresent then opens its own pass and
   presents the whole 2D frame OPAQUE (no 3D-view transparency hole) -- without this
   the direct-present path skipped the SDL flip AND drew nothing, freezing the last
   3D frame on screen (only the cursor updated). */
static E_Boolean G_raveFrameOpen = FALSE ;

/* Physical main-display size in pixels (the panel resolution), for centering. */
static void IRaveScreenSize(int *w, int *h)
{
    GDHandle gd = GetMainDevice() ;
    *w = 0 ; *h = 0 ;
    if (gd != NULL) {
        Rect r = (**gd).gdRect ;
        *w = (int)(r.right - r.left) ;
        *h = (int)(r.bottom - r.top) ;
    }
}

/* RESSCALE layout getters (forward-declared to avoid pulling the SDL-heavy
   RESSCALE.H into this Mac file). */
extern int  ResScaleGetWindowWidth(void) ;
extern int  ResScaleGetWindowHeight(void) ;
extern int  ResScaleGetLogicalWidth(void) ;
extern int  ResScaleGetLogicalHeight(void) ;
extern void ResScaleGetDstRect(int *x, int *y, int *w, int *h) ;
extern void ResScaleSaveRaveShot(const void *rgb555, int w, int h, int pitchBytes) ;

/* rave-9: F8 in direct-to-screen mode. Set (RaveViewRequestShot) by the input
   loop; the next frame is rendered held (DontSwap) so FrameEnd can read the
   just-drawn buffer once and save it, then presenting resumes. One-shot -> the
   ~20ms read costs nothing in normal play. */
static E_Boolean G_raveShotPending = FALSE ;

/* rave-9 UI quad: AA's 320x200 2D frame (HUD/panels/weapon), uploaded each frame
   as an ARGB16 texture and drawn by the GPU over the 3D. Transparent (alpha 0)
   only in the view rect where the overlay is 0 (pure 3D background) so the 3D
   shows through; opaque everywhere else. Rebuilt per frame (UI animates). */
static TQATexture *G_raveUITex  = NULL ;
static int         G_raveUIPotW = 0, G_raveUIPotH = 0 ;   /* POT texture size   */
static int         G_raveUISW   = 0, G_raveUISH   = 0 ;   /* logical UI size    */

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
    T_sword16   level ;    /* baked shade level 0..63; -1 = the sky flat     */
    TQATexture *tex ;      /* uploaded RAVE texture (NULL == known-bad)      */
    T_word32    lastUsed ; /* frame # last drawn -- LRU eviction             */
} T_raveTexEntry ;

static T_raveTexEntry  *G_raveTexCache = NULL ;
static T_word16         G_raveTexCount = 0 ;
static T_word16         G_raveTexMax   = 0 ;
#define RAVE_TEX_BUDGET 256         /* max cached (texture,level) CL8 textures    */
static T_word32         G_raveFrameNum = 0 ;   /* bumped each FrameBegin (for LRU) */

/* Quantize a 0..63 shade level to 16 buckets (step 4). Baking each texture at
   every one of 64 levels would multiply VRAM/cache 64x; 16 shades is smooth
   enough and keeps a wall's strip-split to at most 16 distinct bakes. */
#define RAVE_SHADE_Q(lvl)  ((T_sword16)((lvl) & ~3))

/* Bit OR'd into a cache-key level to mark a MASKED surface (opaque==2 walls,
   sprites) whose palette index 0 is the transparent cutout. On SOLID surfaces
   (walls opaque==1/3, floors) index 0 is instead a real opaque black, so the two
   bake differently and must cache as separate (pointer,level) variants. The low
   6 bits hold the 0..63 shade level; RAVE_SHADE_Q's &~3 preserves this bit. */
#define RAVE_MASKED_KEY  0x0100

/* An opaque near-black palette index (darkest non-0 entry), computed with the
   palette table. Used when a NON-transparent texel shades down to index 0: on a
   masked/solid surface that texel must stay opaque (software draws palette[0]
   solid), but our transparent index is 0 -- so we substitute this instead of
   punching a see-through hole. */
static T_byte8 G_raveSafeBlack = 1 ;

/*-------------------------------------------------------------------------*
 * Phase 3 shading: BAKED shade levels. RAVE color tables bind per-TEXTURE (not
 * per-draw) and draws are deferred, so a per-primitive CLUT can't work when many
 * walls share one texture (they'd all resolve to the last-bound table). Instead
 * each (texture, shadeLevel) pair is baked into its OWN CL8 texture whose indices
 * are the source indices remapped through P_shadeIndex[level] (index 0 kept
 * transparent) -- the exact software shade -- and ONE shared, unshaded palette
 * color table (G_paletteTable, entry i = palette[i], index 0 transparent) is bound
 * to every such texture (same table for all -> deferred-safe). Bakes are
 * on-demand, cache keyed by (pointer,level), LRU-evicted at RAVE_TEX_BUDGET. The
 * sky stays ARGB16/full-bright (it isn't shaded).
 *-------------------------------------------------------------------------*/
extern T_byte8 P_shadeIndex[16384] ;   /* 64 x 256 shade->index remap (3D_VIEW.C) */
static TQAColorTable *G_paletteTable = NULL ;

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

    /* Context size + placement depend on the present path:

       readback (rave-7): context is exactly VIEW3D_WIDTH x VIEW3D_HEIGHT at the
       device origin; the taps emit in that space (identity transform) and the
       composite scales it onto the on-screen view rect.

       direct-to-screen (rave-9): context buffer is SDL's surface size, but placed
       CENTERED on the physical display (GetMainDevice) so it lands where SDL
       centers its window -- not the device top-left. The per-frame transform
       (IRaveUpdateViewTransform) then maps the VIEW3D geometry into the letterbox
       rect in device coords. Keeping the buffer surface-sized (not panel-sized)
       avoids extra VRAM. */
    G_raveViewOrgX = 0.0f ; G_raveViewOrgY = 0.0f ;
    G_raveViewSclX = 1.0f ; G_raveViewSclY = 1.0f ;
    G_raveScrOffX  = 0 ;    G_raveScrOffY  = 0 ;
    if (G_raveDirectPresent) {
        int scrW = 0, scrH = 0 ;
        int ctxW = ResScaleGetWindowWidth(), ctxH = ResScaleGetWindowHeight() ;
        if (ctxW <= 0) ctxW = (int)VIEW3D_WIDTH ;
        if (ctxH <= 0) ctxH = (int)VIEW3D_HEIGHT ;
        IRaveScreenSize(&scrW, &scrH) ;
        if (scrW < ctxW) scrW = ctxW ;      /* panel at least as big as the surface */
        if (scrH < ctxH) scrH = ctxH ;
        G_raveScrOffX = (scrW - ctxW) / 2 ; /* centre the surface on the panel */
        G_raveScrOffY = (scrH - ctxH) / 2 ;
        w = (long)ctxW ;
        h = (long)ctxH ;
    } else {
        w = (long)VIEW3D_WIDTH ;
        h = (long)VIEW3D_HEIGHT ;
    }
    if (w <= 0) w = 1 ;
    if (h <= 0) h = 1 ;

    rect.left   = (short)G_raveScrOffX ;
    rect.top    = (short)G_raveScrOffY ;
    rect.right  = (short)(G_raveScrOffX + w) ;
    rect.bottom = (short)(G_raveScrOffY + h) ;

    /* Rectangular clip covering the context rect. */
    G_raveClipRgn = NewRgn() ;
    if (G_raveClipRgn != NULL) {
        SetRectRgn(G_raveClipRgn,
                   (short)G_raveScrOffX, (short)G_raveScrOffY,
                   (short)(G_raveScrOffX + w), (short)(G_raveScrOffY + h)) ;
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

    /* rave-7: readback buffer (RGB555) sized to the context -- only the readback
       path uses it. rave-9 direct-to-screen never reads back, so skip the (now
       full-screen) allocation; G_raveFrameW/H keep the VIEW3D emit space. */
    if (!G_raveDirectPresent) {
        G_raveFrameW   = (T_word16)w ;
        G_raveFrameH   = (T_word16)h ;
        G_raveFrameBuf = (T_word16 *)MemAlloc((T_word32)w * (T_word32)h * 2UL) ;
    } else {
        G_raveFrameW   = (T_word16)VIEW3D_WIDTH ;
        G_raveFrameH   = (T_word16)VIEW3D_HEIGHT ;
        G_raveFrameBuf = NULL ;
    }
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

/* Build the single shared unshaded-palette color table (entry i = palette[i] in
   RGB32; 6-bit VGA channels -> 8-bit via <<2). Bound to every baked CL8 texture;
   the shading lives in the baked indices, not the table. Lazy -- built when the
   palette is loaded. transparentIndexFlag=1 makes index 0 (AA's transparent
   colour) the transparent entry, matching the masked-wall/sprite cutout. */
static TQAColorTable *IPaletteTable(void)
{
    T_palette     pal ;
    unsigned long entry[256] ;
    int           i ;

    if (G_paletteTable != NULL)
        return G_paletteTable ;
    if (G_raveEngine == NULL)
        return NULL ;
    GrGetPalette(0, 256, pal) ;
    {
        int darkest = 0x7FFFFFFF ;
        G_raveSafeBlack = 1 ;
        for (i = 0 ; i < 256 ; i++) {
            unsigned long r = (unsigned long)((pal[i][0] & 0x3F) << 2) ;
            unsigned long g = (unsigned long)((pal[i][1] & 0x3F) << 2) ;
            unsigned long b = (unsigned long)((pal[i][2] & 0x3F) << 2) ;
            entry[i] = (r << 16) | (g << 8) | b ;   /* R=23:16 G=15:8 B=7:0 */
            /* darkest non-transparent (index != 0) entry -> opaque "safe black" */
            if (i != 0) {
                int lum = (int)(r + g + b) ;
                if (lum < darkest) { darkest = lum ; G_raveSafeBlack = (T_byte8)i ; }
            }
        }
    }
    QAColorTableNew(G_raveEngine, kQAColorTable_CL8_RGB32, entry, 1L, &G_paletteTable) ;
    return G_paletteTable ;
}

static void IFreePaletteTable(void)
{
    if ((G_paletteTable != NULL) && (G_raveEngine != NULL))
        QAColorTableDelete(G_raveEngine, G_paletteTable) ;
    G_paletteTable = NULL ;
}

static T_word16 INextPow2(T_word16 v) ;   /* defined below; used to POT-pad */

/* Upload a single AA texture (column-major, 8-bit palettized, pointer past
   its 4-byte w/h header) to a new ARGB16 TQATexture.  Returns NULL if the
   texture is unusable (bad size / alloc or RAVE failure); NULL is cached too,
   so we don't retry a known-bad texture every frame.

   RAVE requires power-of-two textures. ~89% of AA's wall/floor textures already
   are (64x64, 128x128, ...), but ~11% are not (decals, switches, a few
   320x200); those were previously REJECTED, leaving their surfaces as holes.
   Instead, pad any non-POT texture up into a transparent power-of-two buffer,
   placing the real texels at [0..w) x [0..h). RaveViewEmitQuad normalizes u,v
   by the POT size (INextPow2) to match, so POT textures are unchanged (potW==w)
   and non-POT textures sample the correct sub-rect. A non-POT texture that
   *tiles* may show its transparent pad at the wrap edge, but most non-POT
   textures are non-tiling decals, so this recovers nearly all missing surfaces. */
static TQATexture *IRaveUploadTexture(T_byte8 *p, T_sword16 level)
{
    T_word16        w, h, potW, potH ;
    T_byte8        *buf, *remap ;
    TQAImage        image ;
    TQATexture     *tex = NULL ;
    T_word16        col, row ;
    TQAError        err ;
    int             masked ;
    T_byte8         lut[256] ;

    if ((p == NULL) || (p == G_textureNone + 4))
        return NULL ;                   /* the "no texture" sentinel */

    w = PictureGetWidth(p) ;
    h = PictureGetHeight(p) ;

    if ((w == 0) || (h == 0) || (w > 1024) || (h > 1024))
        return NULL ;

    potW = INextPow2(w) ;
    potH = INextPow2(h) ;

    masked = (level & RAVE_MASKED_KEY) ? 1 : 0 ;
    level &= 0x3F ;                                  /* strip flag -> 0..63 */
    remap = &P_shadeIndex[(T_word32)level << 8] ;    /* shade remap for this level */

    /* CL8: one index byte per texel, shading baked in. Bake LUT: source index ->
       stored index. SOLID (masked==0, walls opaque==1/3 + floors): every index
       incl 0 gets its shaded colour, and a texel that shades to 0 becomes the
       opaque safe-black -- solid surfaces are NEVER transparent (index 0 in an AA
       solid texture is real black, not a cutout). MASKED (==1, opaque==2 walls):
       source index 0 stays the transparent 0; other indices shade, substituting
       safe-black for a shade-to-0 so lit-but-dark texels don't punch holes. */
    lut[0] = masked ? 0 : (remap[0] ? remap[0] : G_raveSafeBlack) ;
    { int li ; for (li = 1 ; li < 256 ; li++)
        lut[li] = remap[li] ? remap[li] : G_raveSafeBlack ; }

    buf = (T_byte8 *)MemAlloc((T_word32)potW * (T_word32)potH) ;
    if (buf == NULL)
        return NULL ;
    /* Index-0 pad first (index 0 = transparent via the color table). */
    { T_word32 n = (T_word32)potW * (T_word32)potH, k ;
      for (k = 0 ; k < n ; k++) buf[k] = 0 ; }

    /* AA stores textures column-major: texel(col,row) at p[col*h + row].
       Emit row-major into the POT buffer: dst[row*potW + col] = baked index. */
    for (col = 0 ; col < w ; col++) {
        T_byte8 *srcCol = p + ((T_word32)col * h) ;
        T_byte8 *dstCol = buf + col ;
        for (row = 0 ; row < h ; row++)
            dstCol[(T_word32)row * potW] = lut[srcCol[row]] ;
    }

    image.width    = (long)potW ;
    image.height   = (long)potH ;
    image.rowBytes = (long)potW ;      /* 1 byte/pixel (CL8) */
    image.pixmap   = buf ;

    /* Default flags copy the image into engine/VRAM storage, so buf is ours
       to free immediately after. */
#if RAVE_PROFILE
    { unsigned long _t = IProfNowUs() ;
      err = QATextureNew(G_raveEngine, kQATexture_None, kQAPixel_CL8, &image, &tex) ;
      G_profUploadUs += IProfNowUs() - _t ; G_profUploads++ ; }
#else
    err = QATextureNew(G_raveEngine, kQATexture_None, kQAPixel_CL8,
                       &image, &tex) ;
#endif
    MemFree(buf) ;

    if (err != kQANoErr)
        return NULL ;
    if (G_paletteTable == NULL) IPaletteTable() ;
    if (G_paletteTable != NULL)
        QATextureBindColorTable(G_raveEngine, tex, G_paletteTable) ;
    return tex ;
}

/* Smallest power-of-two >= v (>=1). RAVE textures must be power-of-two; walls,
   floors and sprites are all padded up to this in a transparent POT buffer
   (walls/floors in IRaveUploadTexture, sprites in IRaveUploadSprite). */
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
static TQATexture *IRaveUploadSprite(T_byte8 *p_picture, T_word16 w, T_word16 h, T_sword16 level)
{
    T_word16        potW, potH, vpad ;
    T_byte8        *buf, *remap ;
    TQAImage        image ;
    TQATexture     *tex = NULL ;
    T_word16        col ;
    const T_raveRaster *cols = (const T_raveRaster *)p_picture ;
    TQAError        err ;
    T_byte8         lut[256] ;

    if ((p_picture == NULL) || (w == 0) || (h == 0) || (w > 1024) || (h > 1024))
        return NULL ;

    potW = INextPow2(w) ;
    potH = INextPow2(h) ;
    level &= 0x3F ;                                  /* 0..63 (sprites are masked) */
    remap = &P_shadeIndex[(T_word32)level << 8] ;   /* shade remap for this level */

    /* Sprites are always masked: source index 0 = transparent; other indices
       shade, safe-black-substituting a shade-to-0 so dark texels stay opaque
       (software draws them palette[0], not see-through). See IRaveUploadTexture. */
    lut[0] = 0 ;
    { int li ; for (li = 1 ; li < 256 ; li++)
        lut[li] = remap[li] ? remap[li] : G_raveSafeBlack ; }
    /* RAVE samples texture V "upward" (v=0 is the BOTTOM row of the image), so
       the sprite content must sit flush with the BOTTOM of the POT texture, rows
       [vpad, potH), with the transparent pad ABOVE it. If it sat at the top
       ([0,h)) the quad's v-range [0, h/potH] would land on the padded bottom and
       the sprite would render as mostly pad (the "crop"). vpad=0 when h is POT. */
    vpad = (T_word16)(potH - h) ;

    /* CL8: one palette-index byte per texel (shaded by the bound color table). */
    buf = (T_byte8 *)MemAlloc((T_word32)potW * (T_word32)potH) ;
    if (buf == NULL)
        return NULL ;
    /* index-0 (transparent via color table) everywhere first */
    {
        T_word32 n = (T_word32)potW * (T_word32)potH, k ;
        for (k = 0 ; k < n ; k++)
            buf[k] = 0 ;
    }

    /* Decode each column's opaque run into the row-major POT buffer as raw
       indices. Content is BOTTOM-aligned (rows [vpad,potH)) -- RAVE samples V up. */
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
            buf[(T_word32)(vpad + r) * potW + col] = lut[colBase[r]] ;
    }

    image.width    = (long)potW ;
    image.height   = (long)potH ;
    image.rowBytes = (long)potW ;      /* 1 byte/pixel (CL8) */
    image.pixmap   = buf ;

#if RAVE_PROFILE
    { unsigned long _t = IProfNowUs() ;
      err = QATextureNew(G_raveEngine, kQATexture_None, kQAPixel_CL8, &image, &tex) ;
      G_profUploadUs += IProfNowUs() - _t ; G_profUploads++ ; }
#else
    err = QATextureNew(G_raveEngine, kQATexture_None, kQAPixel_CL8,
                       &image, &tex) ;
#endif
    MemFree(buf) ;

    if (err != kQANoErr)
        return NULL ;
    if (G_paletteTable == NULL) IPaletteTable() ;
    if (G_paletteTable != NULL)
        QATextureBindColorTable(G_raveEngine, tex, G_paletteTable) ;
    return tex ;
}

/* Cache lookup by (key pointer, shade level); returns entry index or 0xFFFF and
   marks the hit as used (for LRU). level -1 is the sky flat. */
static T_word16 IRaveCacheFind(T_byte8 *key, T_sword16 level)
{
    T_word16 i ;
    for (i = 0 ; i < G_raveTexCount ; i++) {
#if RAVE_PROFILE
        G_profCacheScans++ ;   /* count scan iterations -- the O(n) probe cost */
#endif
        if ((G_raveTexCache[i].key == key) && (G_raveTexCache[i].level == level)) {
            G_raveTexCache[i].lastUsed = G_raveFrameNum ;
            return i ;
        }
    }
    return 0xFFFF ;
}

/* Add (key, level, tex). At RAVE_TEX_BUDGET, reclaim the least-recently-used
   slot -- but ONLY from entries NOT used this frame. An entry used this frame has
   a deferred QADrawTriTexture pending against its texture (RAVE renders at
   QARenderEnd, not at submit); deleting it now blanks that geometry -- the
   disappearing-when-close bug. If every entry is in-flight, append (grow) instead
   so nothing pending is freed; the overflow trims back on a later, lighter frame.
   NULL (known-bad) is cached too, so it isn't retried every frame. Returns tex. */
static TQATexture *IRaveCacheAdd(T_byte8 *key, T_sword16 level, TQATexture *tex)
{
    T_word16 slot = 0xFFFF ;

    if (G_raveTexCount >= RAVE_TEX_BUDGET) {
        T_word16 i, lru = 0xFFFF ;
        for (i = 0 ; i < G_raveTexCount ; i++) {
            if (G_raveTexCache[i].lastUsed == G_raveFrameNum)
                continue ;             /* in-flight this frame -- must not free */
            if ((lru == 0xFFFF) ||
                (G_raveTexCache[i].lastUsed < G_raveTexCache[lru].lastUsed))
                lru = i ;
        }
        if (lru != 0xFFFF) {
            if ((G_raveTexCache[lru].tex != NULL) && (G_raveEngine != NULL))
                QATextureDelete(G_raveEngine, G_raveTexCache[lru].tex) ;
            slot = lru ;
        }
        /* else: all entries used this frame -> fall through to append (grow) */
    }

    if (slot == 0xFFFF) {
        if (G_raveTexCount >= G_raveTexMax) {
            T_word16        newMax = (T_word16)(G_raveTexMax ? G_raveTexMax * 2 : 64) ;
            T_raveTexEntry *grown  =
                (T_raveTexEntry *)MemAlloc((T_word32)newMax * sizeof(T_raveTexEntry)) ;
            T_word16        i ;
            if (grown == NULL)
                return tex ;            /* out of memory: use it, don't cache */
            if (G_raveTexCache != NULL) {
                for (i = 0 ; i < G_raveTexCount ; i++)
                    grown[i] = G_raveTexCache[i] ;
                MemFree(G_raveTexCache) ;
            }
            G_raveTexCache = grown ;
            G_raveTexMax   = newMax ;
        }
        slot = G_raveTexCount ;
        G_raveTexCount++ ;
    }
    G_raveTexCache[slot].key      = key ;
    G_raveTexCache[slot].level    = level ;
    G_raveTexCache[slot].tex      = tex ;
    G_raveTexCache[slot].lastUsed = G_raveFrameNum ;
    return tex ;
}

/* Wall/floor texture baked at shade LEVEL (0..63), uploaded + cached on first use. */
static TQATexture *IRaveGetTexture(T_byte8 *p, T_sword16 level)
{
    T_word16 i ;

    if ((p == NULL) || (p == G_textureNone + 4))
        return NULL ;

    level = RAVE_SHADE_Q(level) ;        /* 16 shade buckets -- see RAVE_SHADE_Q */
    i = IRaveCacheFind(p, level) ;
    if (i != 0xFFFF)
        return G_raveTexCache[i].tex ;   /* may be NULL == known-bad */

    return IRaveCacheAdd(p, level, IRaveUploadTexture(p, level)) ;
}

/* Sprite picture baked at shade LEVEL, uploaded + cached on first use. */
static TQATexture *IRaveGetSprite(T_byte8 *p_picture, T_word16 w, T_word16 h, T_sword16 level)
{
    T_word16 i ;

    if (p_picture == NULL)
        return NULL ;

    level = RAVE_SHADE_Q(level) ;        /* 16 shade buckets -- see RAVE_SHADE_Q */
    i = IRaveCacheFind(p_picture, level) ;
    if (i != 0xFFFF)
        return G_raveTexCache[i].tex ;

    return IRaveCacheAdd(p_picture, level, IRaveUploadSprite(p_picture, w, h, level)) ;
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

    /* The persistent UI texture is bound to the palette table below -- drop it too
       so it (and its binding) rebuild lazily on the next frame. */
    if ((G_raveUITex != NULL) && (G_raveEngine != NULL))
        QATextureDelete(G_raveEngine, G_raveUITex) ;
    G_raveUITex = NULL ;
    G_raveUIPotW = G_raveUIPotH = 0 ;

    /* Drop the shared palette color table too -- it was built from this level's
       palette and rebuilds lazily on the next bake. */
    IFreePaletteTable() ;
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

    /* Fast (point) filtering: the profiler showed the frame is FILL-bound (forest
       overdraw = ~145ms GEO), and bilinear (_Best) samples ~4x the texels per
       pixel. _Fast is ~4x cheaper AND gives the crisp classic-pixel look. Perf
       pass, rave-9. (Was _Best.) */
    QASetInt(ctx, kQATag_TextureFilter, kQATextureFilter_Fast) ;

    /* Modulate the texture by the per-vertex diffuse color so rave-5's gouraud
       sector lighting darkens/brightens the texels.
       Texture wrap: kQATextureOp_None is RAVE's default and REPEATS; non-wrapping
       (clamp) is opt-in via the kQATextureOp_Shrink bit (verified against RAVE.h,
       QuickDraw 3D 1.6 -- there is no kQATextureOp_Clamp_U/_V in this RAVE). We OR
       in only Modulate, never Shrink, so walls (whose u exceeds the texture width
       to tile) repeat correctly. (RAVE also exposes GL-style kQATagGL_TextureWrapU/V
       = kQAGL_Repeat/Clamp tags, but those OpenGL-extension tags may not be honored
       by the ATI Rage LT Pro engine; the Shrink bit is the standard RAVE control.) */
    QASetInt(ctx, kQATag_TextureOp, kQATextureOp_Modulate) ;

    /* Alpha-test cutout: our ARGB16 textures put alpha=0 on palette index 0
       (AA's transparent color) and alpha=1 on everything else, so "draw where
       alpha > 0.5" discards exactly the masked texels for solid AND masked
       surfaces alike -- crisp, no blend cost. Translucent surfaces (rave-5/6)
       layer real blending on top of this. */
    QASetInt(ctx,   kQATag_AlphaTestFunc, kQAAlphaTest_GT) ;
    QASetFloat(ctx, kQATag_AlphaTestRef,  0.25f) ;  /* drop alpha=0 (masked
                                    holes); keep 0.5 (translucent) and 1.0 */

    /* Interpolate blending (src-over-dst by alpha) is the default for the
       translucent draws rave-5/6 will flag per-primitive; opaque draws pass
       kQATriFlags_None and ignore it. Write all color channels. */
    QASetInt(ctx, kQATag_Blend,       kQABlend_Interpolate) ;
    QASetInt(ctx, kQATag_ChannelMask,
             kQAChannelMask_r | kQAChannelMask_g |
             kQAChannelMask_b | kQAChannelMask_a) ;

    /* rave-7/9: DontSwap=1 keeps the frame in the back buffer for the readback+
       composite path (software present). DontSwap=0 lets QARenderEnd swap the
       RAVE frame straight to the fullscreen display -- the direct-to-screen path
       (rave-9), which owns the present and skips SDL_Flip. */
    QASetInt(ctx, kQATag_DontSwap,
             (G_raveDirectPresent && !G_raveShotPending) ? 0 : 1) ;
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
    G_raveFrameOpen  = FALSE ;
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
/* rave-9: recompute the VIEW3D->screen transform from the CURRENT RESSCALE
   layout. Done per frame (not once at RaveViewInit) because the letterbox rect
   (G_dst*) isn't populated until after the first video-mode/map build, which can
   be AFTER RaveViewInit -- so an init-time compute would fall back to identity
   and leave the 3D in the top-left corner at native size. */
static void IRaveUpdateViewTransform(void)
{
    int dx = 0, dy = 0, dw = 0, dh = 0 ;

    if (!G_raveDirectPresent)
        return ;                            /* readback path stays identity */
    ResScaleGetDstRect(&dx, &dy, &dw, &dh) ;
    if ((dw > 0) && (dh > 0)) {
        /* All content coords are RECT-RELATIVE (0,0 = the context's top-left); the
           centered context rect (G_raveScrOff*, set in RaveViewInit) does the
           on-screen centering. So the letterbox rect is just the SDL letterbox
           within the surface -- no centre offset here (that was double-shifting
           the 3D relative to the UI). */
        G_raveDstX = dx ;
        G_raveDstY = dy ;
        G_raveDstW = dw ;
        G_raveDstH = dh ;
        /* Map emitted VIEW3D coords into the SAME base-320x200 grid the UI texture
           uses (origin 4,3; 1 emit unit = 1 base unit), then base->device. Using
           320/200 (not logicalW/H) guarantees the 3D fills the UI's transparent
           view hole exactly -- no black seam. */
        G_raveViewOrgX = (float)G_raveDstX + (float)(4 * dw) / 320.0f ;
        G_raveViewOrgY = (float)G_raveDstY + (float)(3 * dh) / 200.0f ;
        G_raveViewSclX = (float)dw / 320.0f ;
        G_raveViewSclY = (float)dh / 200.0f ;
    }
}

T_void RaveViewFrameBegin(T_void)
{
    if (!RaveViewIsActive())
        return ;

    G_raveFrameNum++ ;   /* for LRU (IRaveCacheFind/Add stamp lastUsed with this) */
    IRaveUpdateViewTransform() ;   /* rave-9: keep the 3D placed in the view rect */

#if RAVE_PROFILE
    G_profUploadUs = 0 ; G_profUploads = 0 ;   /* uploads accumulate over this frame's emit */
    G_profFrameBeginUs = IProfNowUs() ;        /* start of BSP walk + emit */
    G_profQuads = G_profStrips = G_profDraws = 0 ;
    G_profCacheScans = 0 ;
    G_profDrawUs = 0 ;
#endif

    /* rave-4: background must be set before QARenderStart, which (NULL initial
       context) clears the color + Z buffers to it. */
    IRaveSetBackground(G_raveContext) ;
    QARenderStart(G_raveContext, NULL, NULL) ;
    IRaveSetRenderState(G_raveContext) ;
    G_raveFrameOpen = TRUE ;   /* a 3D pass is now open for this frame */
}

/* rave-9: read the just-rendered (held, un-swapped) buffer once and save it as a
   BMP via RESSCALE. Called from FrameEnd only when an F8 shot is pending in
   direct mode; the frame was rendered DontSwap so QAAccessDrawBuffer sees it. */
static void IRaveSaveShot(void)
{
    TQAPixelBuffer pb ;
    T_word16      *tmp ;
    long           w, h, r, c ;

    if (QAAccessDrawBuffer(G_raveContext, &pb) != kQANoErr)
        return ;
    w = pb.width ; h = pb.height ;
    tmp = (T_word16 *)MemAlloc((T_word32)w * (T_word32)h * 2UL) ;
    if (tmp != NULL) {
        for (r = 0 ; r < h ; r++) {
            T_byte8  *srcRow = (T_byte8 *)pb.baseAddr + (T_word32)r * pb.rowBytes ;
            T_word16 *dstRow = tmp + (T_word32)r * w ;
            switch (pb.pixelType) {
            case kQAPixel_RGB16:
            case kQAPixel_ARGB16: {
                const unsigned short *s = (const unsigned short *)srcRow ;
                for (c = 0 ; c < w ; c++) dstRow[c] = (T_word16)(s[c] & 0x7FFF) ;
                break ; }
            case kQAPixel_RGB16_565: {
                const unsigned short *s = (const unsigned short *)srcRow ;
                for (c = 0 ; c < w ; c++) {
                    unsigned short p = s[c] ;
                    dstRow[c] = (T_word16)((((p>>11)&0x1F)<<10)|(((p>>6)&0x1F)<<5)|(p&0x1F)) ;
                }
                break ; }
            case kQAPixel_RGB32:
            case kQAPixel_ARGB32: {
                const unsigned long *s = (const unsigned long *)srcRow ;
                for (c = 0 ; c < w ; c++) {
                    unsigned long p = s[c] ;
                    dstRow[c] = (T_word16)((((p>>19)&0x1F)<<10)|(((p>>11)&0x1F)<<5)|((p>>3)&0x1F)) ;
                }
                break ; }
            default:
                for (c = 0 ; c < w ; c++) dstRow[c] = 0 ;
                break ;
            }
        }
        ResScaleSaveRaveShot(tmp, (int)w, (int)h, (int)(w * 2)) ;
        MemFree(tmp) ;
    }
    QAAccessDrawBufferEnd(G_raveContext, NULL) ;
}

/* rave-9: build the UI overlay texture from AA's 320x200 8-bit 2D frame. CL8
   (indexed) -- HALF the upload of ARGB16 and no per-pixel palette conversion; the
   shared unshaded palette table (index 0 = transparent) colours it, exactly like
   the walls. Index 0 (transparent) ONLY inside the view rect where the overlay is
   0 (pure 3D background); everywhere else the raw UI index, safe-black-substituted
   for a genuine index-0 UI pixel so the HUD's black stays opaque. Rebuilt each
   frame (UI animates); prior texture freed.

   NOTE: an in-place QAAccessTexture reuse was tried to avoid the per-frame
   QATextureNew/Delete, but the engine hands back raw VRAM in a swizzled layout, so
   linear writes garble the UI. QATextureNew (which does the swizzle) it is. */
static void IRaveUploadUI(const T_byte8 *ui8, int sw, int sh, int pitch,
                          E_Boolean fullOpaque)
{
    const T_byte8  *ovl ;
    T_byte8        *buf ;
    int             potW, potH, vpad, x, y ;
    int             vx0 = 4, vy0 = 3 ;                /* VIEW3D_ORIGIN_X/Y */
    int             vx1 = 4 + (int)VIEW3D_WIDTH ;
    int             vy1 = 3 + (int)VIEW3D_HEIGHT ;
    TQAImage        image ;
    TQAError        err ;
    TQATexture     *tex = NULL ;

    if (G_raveUITex != NULL) {
        QATextureDelete(G_raveEngine, G_raveUITex) ;
        G_raveUITex = NULL ;
    }
    if ((ui8 == NULL) || (sw <= 0) || (sh <= 0) || (sw > 1024) || (sh > 1024))
        return ;

    potW = (int)INextPow2((T_word16)sw) ;
    potH = (int)INextPow2((T_word16)sh) ;
    buf  = (T_byte8 *)MemAlloc((T_word32)potW * (T_word32)potH) ;   /* 1 byte/texel */
    if (buf == NULL)
        return ;
    { T_word32 n = (T_word32)potW * (T_word32)potH, k ;
      for (k = 0 ; k < n ; k++) buf[k] = 0 ; }     /* index 0 = transparent pad */

    ovl = (const T_byte8 *)View3dGetOverlayScreen() ;   /* 320x200, 0 = 3D bg */

    /* RAVE samples texture V upward (v=0 = bottom row), so the UI must sit flush
       with the BOTTOM of the POT texture -- like sprites/flats. The quad then
       maps screen-top->v=sh, screen-bottom->v=0 (see IRaveDrawUIQuad). */
    vpad = potH - sh ;
    for (y = 0 ; y < sh ; y++) {
        const T_byte8 *srow = ui8 + (T_word32)y * pitch ;
        T_byte8       *drow = buf + (T_word32)(vpad + y) * potW ;
        for (x = 0 ; x < sw ; x++) {
            /* transparent only where the 3D view shows: inside the view rect AND
               the overlay is 0 (no 2D drawn over the view there). fullOpaque
               forces the whole frame opaque -- used on 2D-only screens (no 3D
               pass this frame), where there is no 3D behind the hole to show. */
            if ((!fullOpaque) &&
                (x >= vx0) && (x < vx1) && (y >= vy0) && (y < vy1) &&
                (ovl != NULL) && (ovl[(T_word32)y * 320 + x] == 0)) {
                drow[x] = 0 ;                        /* transparent -> 3D shows */
            } else {
                T_byte8 idx = srow[x] ;
                drow[x] = idx ? idx : G_raveSafeBlack ; /* opaque; keep black opaque */
            }
        }
    }

    image.width = potW ; image.height = potH ; image.rowBytes = potW ; image.pixmap = buf ;
    err = QATextureNew(G_raveEngine, kQATexture_None, kQAPixel_CL8, &image, &tex) ;
    MemFree(buf) ;
    if (err == kQANoErr) {
        if (G_paletteTable == NULL) IPaletteTable() ;
        if (G_paletteTable != NULL)
            QATextureBindColorTable(G_raveEngine, tex, G_paletteTable) ;
        G_raveUITex  = tex ;
        G_raveUIPotW = potW ; G_raveUIPotH = potH ;
        G_raveUISW   = sw ;   G_raveUISH   = sh ;
    }
}

/* rave-9: fill a rect-relative screen rect with opaque black (untextured gouraud).
   Used for the letterbox bars -- tall 3D geometry projects above/below the view
   rect into the letterbox, and the UI quad (game area only) doesn't cover it. */
static void IRaveDrawSolidRect(float x0, float y0, float x1, float y1)
{
    TQAVGouraud v[4] ;
    int i ;
    v[0].x = x0 ; v[0].y = y0 ;
    v[1].x = x0 ; v[1].y = y1 ;
    v[2].x = x1 ; v[2].y = y1 ;
    v[3].x = x1 ; v[3].y = y0 ;
    for (i = 0 ; i < 4 ; i++) {
        v[i].z = -0.5f ; v[i].invW = 1.0f ;      /* near -> over all 3D */
        v[i].r = v[i].g = v[i].b = 0.0f ; v[i].a = 1.0f ;   /* opaque black */
    }
    QADrawTriGouraud(G_raveContext, &v[0], &v[1], &v[2], kQATriFlags_None) ;
    QADrawTriGouraud(G_raveContext, &v[0], &v[2], &v[3], kQATriFlags_None) ;
}

/* Cover the letterbox bars around the game (dst) rect with black. */
static void IRaveDrawLetterbox(void)
{
    int cw = ResScaleGetWindowWidth(), ch = ResScaleGetWindowHeight() ;
    int x0 = G_raveDstX, y0 = G_raveDstY ;
    int x1 = G_raveDstX + G_raveDstW, y1 = G_raveDstY + G_raveDstH ;
    if (cw <= 0) cw = x1 ;
    if (ch <= 0) ch = y1 ;
    if (y0 > 0)  IRaveDrawSolidRect(0.0f, 0.0f, (float)cw, (float)y0) ;        /* top    */
    if (y1 < ch) IRaveDrawSolidRect(0.0f, (float)y1, (float)cw, (float)ch) ;   /* bottom */
    if (x0 > 0)  IRaveDrawSolidRect(0.0f, 0.0f, (float)x0, (float)ch) ;        /* left   */
    if (x1 < cw) IRaveDrawSolidRect((float)x1, 0.0f, (float)cw, (float)ch) ;   /* right  */
}

/* rave-9: draw the UI texture as a screen-space quad over the dst rect. No view
   transform (it's already in screen space) and z at the near plane so it beats
   all 3D under the standard LT z-test; alpha-test drops the transparent texels. */
static void IRaveDrawUIQuad(void)
{
    TQAVTexture v[4] ;
    int   dx = 0, dy = 0, dw = 0, dh = 0, i ;
    float u1, v1 ;

    if (G_raveUITex == NULL)
        return ;
    dx = G_raveDstX ; dy = G_raveDstY ; dw = G_raveDstW ; dh = G_raveDstH ;
    if ((dw <= 0) || (dh <= 0))
        return ;
    u1 = (float)G_raveUISW / (float)G_raveUIPotW ;
    v1 = (float)G_raveUISH / (float)G_raveUIPotH ;

    QASetPtr(G_raveContext, kQATag_Texture, G_raveUITex) ;

    /* TL, BL, BR, TR (matches IRaveDrawTexturedQuad winding). V is flipped --
       screen-top samples v=v1 (UI top row), screen-bottom v=0 -- because the UI
       content is bottom-aligned in the texture and RAVE samples V upward. */
    v[0].x = (float)dx ;      v[0].y = (float)dy ;      v[0].uOverW = 0.0f ; v[0].vOverW = v1 ;
    v[1].x = (float)dx ;      v[1].y = (float)(dy+dh) ; v[1].uOverW = 0.0f ; v[1].vOverW = 0.0f ;
    v[2].x = (float)(dx+dw) ; v[2].y = (float)(dy+dh) ; v[2].uOverW = u1 ;   v[2].vOverW = 0.0f ;
    v[3].x = (float)(dx+dw) ; v[3].y = (float)dy ;      v[3].uOverW = u1 ;   v[3].vOverW = v1 ;
    for (i = 0 ; i < 4 ; i++) {
        v[i].z    = -0.5f ;    /* near -> in front of all 3D (z in [0,1]) */
        v[i].invW = 1.0f ;     /* screen space: no perspective divide */
        v[i].r = v[i].g = v[i].b = 0.0f ;
        v[i].a = 1.0f ;
        v[i].kd_r = v[i].kd_g = v[i].kd_b = 1.0f ;
        v[i].ks_r = v[i].ks_g = v[i].ks_b = 0.0f ;
    }
    QADrawTriTexture(G_raveContext, &v[0], &v[1], &v[2], kQATriFlags_None) ;
    QADrawTriTexture(G_raveContext, &v[0], &v[2], &v[3], kQATriFlags_None) ;
}

/* rave-9: called by RESSCALE at present time (after the 2D UI is drawn into its
   320x200 frame). The 3D render pass is still open (FrameEnd deferred the swap in
   direct mode); draw the UI quad over the 3D, then QARenderEnd swaps the whole
   composited frame to the fullscreen display. No readback, no CPU composite. */
T_void RaveViewDrawUIAndPresent(const T_byte8 *ui8, int sw, int sh, int pitch)
{
    E_Boolean twoD ;

    if (!RaveViewIsActive() || !G_raveDirectPresent)
        return ;

    /* twoD = no 3D pass was opened this frame (RaveViewFrameBegin didn't run):
       we're on a 2D-only screen (townUI/shops/guild/full-screen menu). There is no
       open pass to draw the UI over, so open one here, and present the 2D frame
       fully OPAQUE (no 3D-view transparency hole). Without this the direct-present
       path drew into a closed/stale pass and skipped SDL_Flip -> the last 3D frame
       stayed frozen on screen. */
    twoD = G_raveFrameOpen ? FALSE : TRUE ;
    if (twoD) {
        IRaveUpdateViewTransform() ;          /* refresh the dst rect (screen-only) */
        IRaveSetBackground(G_raveContext) ;   /* clear color+Z to black on Start    */
        QARenderStart(G_raveContext, NULL, NULL) ;
        IRaveSetRenderState(G_raveContext) ;
    }

#if RAVE_PROFILE
    if (!twoD)
        G_profRestUs = IProfNowUs() - G_profFrameEndUs ; /* software UI draw + game logic */
    RaveViewProfileCompositeBegin() ;      /* UI build + draw counts as composite */
#endif
    IRaveUploadUI(ui8, sw, sh, pitch, twoD) ;
    IRaveDrawUIQuad() ;
    IRaveDrawLetterbox() ;   /* hide 3D that projected into the letterbox bars */

#if RAVE_PROFILE
    { unsigned long _t = IProfNowUs() ;
      QARenderEnd(G_raveContext, NULL) ;   /* swaps unless held for a shot */
      G_profRenderUs = IProfNowUs() - _t ; }
#else
    QARenderEnd(G_raveContext, NULL) ;
#endif

    if (G_raveShotPending) {                /* frame held (DontSwap set in FrameBegin) */
        IRaveSaveShot() ;
        G_raveShotPending = FALSE ;
    }
    G_raveFrameReady = FALSE ;
    G_raveFrameOpen  = FALSE ;   /* pass presented; next frame reopens if 3D draws */
#if RAVE_PROFILE
    RaveViewProfileFrame() ;
#endif
}

T_void RaveViewFrameEnd(T_void)
{
    TQAPixelBuffer pb ;
    TQAError       err ;
#if RAVE_PROFILE
    unsigned long  _tRender, _tReadback ;
#endif

    if (!RaveViewIsActive())
        return ;

#if RAVE_PROFILE
    G_profGeoUs      = IProfNowUs() - G_profFrameBeginUs ;   /* BSP walk + emit */
    G_profFrameEndUs = IProfNowUs() ;
#endif

    /* rave-9 direct-to-screen: defer the present. The 3D is rendered but the pass
       stays OPEN so RaveViewDrawUIAndPresent (called from RESSCALE once the 2D UI
       is ready) can draw the UI quad into this same frame, then QARenderEnd. */
    if (G_raveDirectPresent)
        return ;

#if RAVE_PROFILE
    _tRender = IProfNowUs() ;
#endif
    QARenderEnd(G_raveContext, NULL) ;      /* DontSwap: stays in back buffer */
#if RAVE_PROFILE
    G_profRenderUs = IProfNowUs() - _tRender ;
    _tReadback = IProfNowUs() ;
#endif

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

        /* rave: reapply the live global palette tint (damage/pickup flash and
           fades) that COLOR.C pushes into the palette each frame via
           GrSetPalette. RAVE bakes the palette into its textures at upload, so
           this dynamic offset never reaches the baked scene -- add it here on
           the RGB555 read-back (6-bit VGA delta >>1 to 5-bit, per channel,
           clamped). Skipped entirely when no tint is active, so the common
           (no-flash) case pays nothing. Gamma is already baked at upload and is
           excluded (see ColorGetTintDelta). */
        {
            T_sword16 tr = 0, tg = 0, tb = 0 ;
            ColorGetTintDelta(&tr, &tg, &tb) ;
            if (tr || tg || tb) {
                int dr = (int)tr / 2, dg = (int)tg / 2, db = (int)tb / 2 ;
                for (r = 0 ; r < rows ; r++) {
                    T_word16 *d = G_raveFrameBuf + (T_word32)r * G_raveFrameW ;
                    for (c = 0 ; c < cols ; c++) {
                        int r5 = (d[c] >> 10) & 0x1F ;
                        int g5 = (d[c] >>  5) & 0x1F ;
                        int b5 =  d[c]        & 0x1F ;
                        r5 += dr ; g5 += dg ; b5 += db ;
                        if (r5 < 0) r5 = 0 ; else if (r5 > 31) r5 = 31 ;
                        if (g5 < 0) g5 = 0 ; else if (g5 > 31) g5 = 31 ;
                        if (b5 < 0) b5 = 0 ; else if (b5 > 31) b5 = 31 ;
                        d[c] = (T_word16)((r5 << 10) | (g5 << 5) | b5) ;
                    }
                }
            }
        }
    }

    QAAccessDrawBufferEnd(G_raveContext, NULL) ;
#if RAVE_PROFILE
    G_profReadbackUs = IProfNowUs() - _tReadback ;
#endif
    G_raveFrameReady = TRUE ;
}

#if RAVE_PROFILE
/* RESSCALE.C calls this just before the composite pixel loop; RaveViewProfile
   Frame() below then measures composite time itself (keeps the Mac timer call
   out of the cross-platform RESSCALE.C). */
T_void RaveViewProfileCompositeBegin(void)
{
    G_profCompositeStart = IProfNowUs() ;
}

/* Called once per composited frame from RESSCALE.C (right after the composite
   pixel loop). Rolls this frame's upload/render/readback (in the globals) plus
   the composite time into ~1s accumulators, and every ~second MessageAdd()s an
   averaged per-phase ms line + FPS -- so the on-screen log / F8 screenshot shows
   which phase dominates. up=<ms>/<uploads>  rn=render  rd=readback  cp=composite. */
T_void RaveViewProfileFrame(void)
{
    unsigned long now, elapsed, compositeUs ;

    if (!G_raveEnabled || !G_raveProfileOn)   /* F8 toggles G_raveProfileOn (default off) */
        return ;
    compositeUs = IProfNowUs() - G_profCompositeStart ;
    G_accUpload    += G_profUploadUs ;
    G_accUploads   += G_profUploads ;
    G_accRender    += G_profRenderUs ;
    G_accReadback  += G_profReadbackUs ;
    G_accComposite += compositeUs ;
    G_accGeo       += G_profGeoUs ;
    G_accRest      += G_profRestUs ;
    G_accDraw      += G_profDrawUs ;
    G_accQuads     += G_profQuads ;
    G_accStrips    += G_profStrips ;
    G_accDraws     += G_profDraws ;
    G_accCacheScans+= G_profCacheScans ;
    G_accFrames++ ;

    now = IProfNowUs() ;
    if (G_profLastReportUs == 0) {          /* first frame: start the window here */
        G_profLastReportUs = now ;
        return ;
    }
    elapsed = now - G_profLastReportUs ;    /* unsigned, wrap-safe */
    if ((elapsed >= 1000000UL) && (G_accFrames > 0)) {
        char          buf[128] ;
        unsigned long f  = G_accFrames ;
        unsigned long up = (G_accUpload    / f) / 100UL ;   /* tenths of a ms */
        unsigned long rn = (G_accRender    / f) / 100UL ;
        unsigned long rd = (G_accReadback  / f) / 100UL ;
        unsigned long cp = (G_accComposite / f) / 100UL ;
        unsigned long nu = G_accUploads    / f ;            /* uploads / frame */
        unsigned long fp = (f * 10000000UL) / (elapsed ? elapsed : 1UL) ; /* fps*10 */
        {   /* line 2: split GEO into RAVE draw (setup+submit+fill) vs the software
               geometry WALK (geo - draw), plus rest and draw count. */
            unsigned long ge = (G_accGeo  / f) / 100UL ;   /* tenths of a ms */
            unsigned long dw = (G_accDraw / f) / 100UL ;
            unsigned long wk = (G_accGeo > G_accDraw)
                             ? (((G_accGeo - G_accDraw) / f) / 100UL) : 0UL ;
            unsigned long re = (G_accRest / f) / 100UL ;
            char buf2[128] ;
            sprintf(buf,
                "RAVE ms up=%lu.%lu/%lu rn=%lu.%lu rd=%lu.%lu cp=%lu.%lu fps=%lu.%lu",
                up/10, up%10, nu, rn/10, rn%10, rd/10, rd%10, cp/10, cp%10, fp/10, fp%10) ;
            MessageAdd((T_byte8 *)buf) ;
            sprintf(buf2,
                "RAVE geo=%lu.%lu draw=%lu.%lu walk=%lu.%lu rest=%lu.%lu dr=%lu cs=%luk",
                ge/10, ge%10, dw/10, dw%10, wk/10, wk%10, re/10, re%10,
                (unsigned long)(G_accDraws / f),
                (unsigned long)((G_accCacheScans / f) / 1000UL)) ;
            MessageAdd((T_byte8 *)buf2) ;
        }
        G_accUpload = G_accRender = G_accReadback = G_accComposite = 0 ;
        G_accUploads = G_accFrames = 0 ;
        G_accGeo = G_accRest = G_accDraw = 0 ;
        G_accQuads = G_accStrips = G_accDraws = 0 ; G_accCacheScans = 0 ;
        G_profLastReportUs = now ;
    }
}

/* ALT+F8 toggles the on-screen fps/phase readout (default off). Returns new state.
   RaveViewProfileIsOn lets RESSCALE gate its software-mode fps line on the same
   flag, so the counter is one feature across both renderers. */
E_Boolean RaveViewProfileToggle(void)
{
    G_raveProfileOn = G_raveProfileOn ? FALSE : TRUE ;
    G_profLastReportUs = 0 ;   /* restart the averaging window */
    return G_raveProfileOn ;
}

E_Boolean RaveViewProfileIsOn(void)
{
    return G_raveProfileOn ;
}
#else
T_void    RaveViewProfileCompositeBegin(void) {}
T_void    RaveViewProfileFrame(void) {}
E_Boolean RaveViewProfileToggle(void) { return FALSE ; }
E_Boolean RaveViewProfileIsOn(void)   { return FALSE ; }
#endif

/* rave-7: hand the composited RGB555 frame (+ its dimensions) to the display
   compositor. Returns FALSE when no RAVE frame is available (inactive build,
   no context, or readback failed) -- caller then shows the software frame. */
E_Boolean RaveViewGetFrame(T_raveFrame *out)
{
    T_sword16 clipL, clipR ;

    if (out == NULL)
        return FALSE ;
    if (!G_raveEnabled || !G_raveFrameReady || (G_raveFrameBuf == NULL))
        return FALSE ;

    /* Current visible view width (shrinks when a menu opens; View3dClipCenter
       left-anchors it, so viewX is CLIP_LEFT and the width follows the clip). */
    clipL = VIEW3D_CLIP_LEFT ;
    clipR = VIEW3D_CLIP_RIGHT ;
    if (clipL < 0) clipL = 0 ;
    if (clipR > (T_sword16)G_raveFrameW) clipR = (T_sword16)G_raveFrameW ;
    if (clipR <= clipL) { clipL = 0 ; clipR = (T_sword16)G_raveFrameW ; }

    out->pixels      = G_raveFrameBuf ;
    out->stride      = G_raveFrameW ;
    out->viewX       = (T_word16)clipL ;
    out->viewW       = (T_word16)(clipR - clipL) ;
    out->viewH       = G_raveFrameH ;
    out->flipY       = 0 ;   /* read-back is top-up (confirmed on hardware) */
    out->overlay     = (T_byte8 *)View3dGetOverlayScreen() ;
    out->overlayW    = 320 ; /* overlay layer is base 320x200 (3D_VIEW.C) */
    out->overlayH    = 200 ;
    out->overlayOrgX = 4 ;   /* VIEW3D_ORIGIN_X in base coords */
    out->overlayOrgY = 3 ;   /* VIEW3D_ORIGIN_Y */
    return TRUE ;
}

/* rave-7 offload: TRUE when RAVE is successfully presenting, so the caller can
   skip the software 3D raster. Read during a frame's geometry pass, G_raveFrameReady
   still holds the PREVIOUS frame's readback result (FrameEnd sets it; FrameBegin
   doesn't touch it) -- so we only skip software once RAVE has proven it produced
   a composited frame, and a failed readback auto-falls-back to software next
   frame. Never a permanent black screen. */
E_Boolean RaveViewIsPresenting(T_void)
{
    /* rave-9 direct-to-screen has no readback frame to gate on -- RAVE swaps its
       own buffer, so it "presents" whenever it's active. (The 1-frame software
       fallback of the readback path doesn't apply; the first RAVE swap shows the
       3D directly.) */
    if (G_raveDirectPresent)
        return RaveViewIsActive() ;
    return (G_raveFrameReady && G_raveEnabled) ? TRUE : FALSE ;
}

/* Core: bind an already-resolved TQATexture and draw the quad (TL,BL,BR,TR)
   as 2 textured, gouraud-lit triangles. u,v are texel coords normalized by
   (normW,normH) -- the *texture's* real dimensions (POT), which for a padded
   sprite differ from its logical w x h. light -> kd_r/g/b (Modulate). */
static T_void IRaveDrawTexturedQuad(
           TQATexture *tex, float normW, float normH, float alpha,
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
#if RAVE_PROFILE
    unsigned long       _tDraw = G_raveProfileOn ? IProfNowUs() : 0 ; /* draw = setup+submit(+fill) */
    G_profDraws++ ;   /* one textured quad (2 tris) -- wall strip, sprite, or sky */
#endif
    QASetPtr(G_raveContext, kQATag_Texture, tex) ;

    src[0] = topLeft ; src[1] = bottomLeft ;
    src[2] = bottomRight ; src[3] = topRight ;

    for (i = 0 ; i < 4 ; i++) {
        /* rave-9: map VIEW3D emit space -> screen (identity in the readback path,
           where Org=0/Scl=1; the on-screen view rect in direct-to-screen). */
        v[i].x    = G_raveViewOrgX + src[i]->x * G_raveViewSclX ;
        v[i].y    = G_raveViewOrgY + src[i]->y * G_raveViewSclY ;
        v[i].z    = src[i]->z ;
        v[i].invW = src[i]->invW ;
        /* RAVE wants u/w, v/w; it divides by interpolated invW per pixel. */
        v[i].uOverW = (src[i]->u * invNW) * src[i]->invW ;
        v[i].vOverW = (src[i]->v * invNH) * src[i]->invW ;
        /* Shading is BAKED into the texture (indices pre-remapped through
           P_shadeIndex per level), so kd is full-bright 1.0 -- Modulate leaves the
           texel colour intact. alpha < 1 blends (translucent walls / windows);
           final texel alpha = texel.a * alpha still passes the alpha-test cutout. */
        v[i].r = v[i].g = v[i].b = 0.0f ;
        v[i].a = alpha ;
        v[i].kd_r = v[i].kd_g = v[i].kd_b = 1.0f ;
        v[i].ks_r = v[i].ks_g = v[i].ks_b = 0.0f ;
    }

    QADrawTriTexture(G_raveContext, &v[0], &v[1], &v[2], kQATriFlags_None) ;
    QADrawTriTexture(G_raveContext, &v[0], &v[2], &v[3], kQATriFlags_None) ;
#if RAVE_PROFILE
    if (G_raveProfileOn) G_profDrawUs += IProfNowUs() - _tDraw ;
#endif
}

/* Interpolate a wall vertex along the top or bottom edge (a = left/near end,
   b = right/far end) at fraction t, for splitting a wall into shade strips. On a
   perspective wall x/y/z/invW and light are linear in screen X; u is
   perspective-correct (u*invW is the linear quantity); v is constant along the
   edge (top or bottom). */
static void IInterpAlong(T_raveVertex *out, const T_raveVertex *a,
                         const T_raveVertex *b, float t)
{
    float uowA = a->u * a->invW ;
    float uowB = b->u * b->invW ;
    out->x     = a->x    + (b->x    - a->x)    * t ;
    out->y     = a->y    + (b->y    - a->y)    * t ;
    out->z     = a->z    + (b->z    - a->z)    * t ;
    out->invW  = a->invW + (b->invW - a->invW) * t ;
    out->u     = (uowA + (uowB - uowA) * t) / out->invW ;
    out->v     = a->v ;
    out->light = a->light + (b->light - a->light) * t ;
}

/* Walls + floors/ceilings: flat AA texture (pointer past its 4-byte header).
   alpha < 1 blends the quad (translucent walls / building windows).

   SHADE-STRIP split: on a wall the shade level runs left->right (near end vs far
   end), so we split the quad into one vertical strip per shade level it spans and
   fetch each strip's OWN texture baked at that level (IRaveGetTexture) -- so
   shading follows distance smoothly across the face (near software's per-column
   look) instead of one flat level for the whole face. Uniform quads -- floor
   scanlines are one distance -> levelL==levelR -> a single quad, so only walls
   actually split.

   masked != 0 marks an opaque==2 wall whose palette index 0 is the transparent
   cutout; it is OR'd into the bake/cache key (RAVE_MASKED_KEY) so index 0 stays
   see-through. On solid walls + floors (masked==0) index 0 is a real opaque
   black. */
#define RAVE_MAX_STRIPS 16
T_void RaveViewEmitQuad(
           T_byte8 *p_texture,
           float alpha,
           T_byte8 masked,
           const T_raveVertex *topLeft,
           const T_raveVertex *bottomLeft,
           const T_raveVertex *bottomRight,
           const T_raveVertex *topRight)
{
    TQATexture *tex ;
    float       normW, normH ;
    int         levelL, levelR, n, i ;
    T_sword16   mkey = masked ? RAVE_MASKED_KEY : 0 ;

    if (!RaveViewIsActive())
        return ;
    if ((p_texture == NULL) || (p_texture == G_textureNone + 4))
        return ;                        /* untextured -> skip */

#if RAVE_PROFILE
    G_profQuads++ ;   /* one wall/floor emit (may split into strips below) */
#endif
    /* Normalize u,v by the POT upload size (not logical) -- see IRaveUploadTexture. */
    normW = (float)INextPow2(PictureGetWidth(p_texture)) ;
    normH = (float)INextPow2(PictureGetHeight(p_texture)) ;

    levelL = (int)(topLeft->light  * 63.0f + 0.5f) ;
    levelR = (int)(topRight->light * 63.0f + 0.5f) ;
    if (levelL < 0) levelL = 0 ; if (levelL > 63) levelL = 63 ;
    if (levelR < 0) levelR = 0 ; if (levelR > 63) levelR = 63 ;
    /* strips = number of distinct 16-shade buckets the face spans (bake level is
       quantized to those buckets inside IRaveGetTexture -- see RAVE_SHADE_Q), so a
       wall never generates more than 16 baked variants no matter how close. */
    n = (RAVE_SHADE_Q(levelL) - RAVE_SHADE_Q(levelR)) >> 2 ;
    if (n < 0) n = -n ; n += 1 ;
    if (n <= 1) {                                        /* uniform -> one quad */
#if RAVE_PROFILE
        G_profStrips++ ;
#endif
        tex = IRaveGetTexture(p_texture, (T_sword16)(levelL | mkey)) ;
        if (tex != NULL)
            IRaveDrawTexturedQuad(tex, normW, normH, alpha,
                                  topLeft, bottomLeft, bottomRight, topRight) ;
        return ;
    }
    if (n > RAVE_MAX_STRIPS)
        n = RAVE_MAX_STRIPS ;
#if RAVE_PROFILE
    G_profStrips += (T_word32)n ;
#endif
    for (i = 0 ; i < n ; i++) {
        float        t0 = (float)i / (float)n ;
        float        t1 = (float)(i + 1) / (float)n ;
        T_raveVertex sTL, sBL, sBR, sTR ;
        int          slevel ;
        IInterpAlong(&sTL, topLeft,    topRight,    t0) ;
        IInterpAlong(&sBL, bottomLeft, bottomRight, t0) ;
        IInterpAlong(&sBR, bottomLeft, bottomRight, t1) ;
        IInterpAlong(&sTR, topLeft,    topRight,    t1) ;
        slevel = (int)(((sTL.light + sBL.light + sBR.light + sTR.light) * 0.25f)
                       * 63.0f + 0.5f) ;
        if (slevel < 0)  slevel = 0 ;
        if (slevel > 63) slevel = 63 ;
        tex = IRaveGetTexture(p_texture, (T_sword16)(slevel | mkey)) ;
        if (tex != NULL)
            IRaveDrawTexturedQuad(tex, normW, normH, alpha,
                                  &sTL, &sBL, &sBR, &sTR) ;
    }
}

/* Flat row-major 8-bit raster (the sky backdrop) -> POT ARGB16, fully OPAQUE
   (index 0 is a real sky color here, not transparent). Cached by pointer. */
static TQATexture *IRaveUploadFlat(T_byte8 *raw, T_word16 w, T_word16 h)
{
    T_word16        potW, potH, col, row, vpad ;
    unsigned short *buf ;
    T_palette       pal ;
    TQAImage        image ;
    TQATexture     *tex = NULL ;
    TQAError        err ;

    if ((raw == NULL) || (w == 0) || (h == 0) || (w > 2048) || (h > 1024))
        return NULL ;
    potW = INextPow2(w) ;
    potH = INextPow2(h) ;
    /* Bottom-align content, rows [vpad,potH), same as sprites -- RAVE samples V
       upward, so the sky emitter's v=[0,h] range hits the real content. */
    vpad = (T_word16)(potH - h) ;
    buf = (unsigned short *)MemAlloc((T_word32)potW * (T_word32)potH * 2UL) ;
    if (buf == NULL)
        return NULL ;
    { T_word32 n = (T_word32)potW * (T_word32)potH, k ; for (k = 0 ; k < n ; k++) buf[k] = 0 ; }

    GrGetPalette(0, 256, pal) ;
    for (row = 0 ; row < h ; row++) {
        T_byte8        *src = raw + (T_word32)row * w ;
        unsigned short *dst = buf + (T_word32)(vpad + row) * potW ;
        for (col = 0 ; col < w ; col++) {
            T_byte8 idx = src[col] ;
            dst[col] = (unsigned short)(0x8000 |
                       (((pal[idx][0] & 0x3F) >> 1) << 10) |
                       (((pal[idx][1] & 0x3F) >> 1) <<  5) |
                        ((pal[idx][2] & 0x3F) >> 1)) ;
        }
    }
    /* Clamp the top edge: replicate content row 0 (image row vpad) up through the
       transparent POT padding [0,vpad). RAVE's V sampling can round a texel past
       the content into the pad; without this that shows as a transparent (black)
       band at the top of the sky when pitched. Filling the pad with the top cloud
       row makes any overshoot show the top cloud instead -- exactly software's
       backdropRow clamp (3D_VIEW.C:5163). Content reaches the image bottom
       (row potH-1), so no bottom pad to clamp. */
    {
        T_word16 rr, cc ;
        for (rr = 0 ; rr < vpad ; rr++)
            for (cc = 0 ; cc < potW ; cc++)
                buf[(T_word32)rr * potW + cc] = buf[(T_word32)vpad * potW + cc] ;
    }
    image.width = potW ; image.height = potH ; image.rowBytes = potW * 2 ; image.pixmap = buf ;
#if RAVE_PROFILE
    { unsigned long _t = IProfNowUs() ;
      err = QATextureNew(G_raveEngine, kQATexture_None, kQAPixel_ARGB16, &image, &tex) ;
      G_profUploadUs += IProfNowUs() - _t ; G_profUploads++ ; }
#else
    err = QATextureNew(G_raveEngine, kQATexture_None, kQAPixel_ARGB16, &image, &tex) ;
#endif
    MemFree(buf) ;
    return (err == kQANoErr) ? tex : NULL ;
}

static TQATexture *IRaveGetFlat(T_byte8 *raw, T_word16 w, T_word16 h)
{
    T_word16 i ;
    if (raw == NULL)
        return NULL ;
    i = IRaveCacheFind(raw, -1) ;   /* -1 = the unshaded sky flat */
    if (i != 0xFFFF)
        return G_raveTexCache[i].tex ;
    return IRaveCacheAdd(raw, -1, IRaveUploadFlat(raw, w, h)) ;
}

/* rave-8: sky backdrop quad. raw is a row-major 8-bit panorama (stride w);
   u,v are texel coords into it (normalized by POT). Opaque, drawn at far depth
   so walls/ceilings occlude it and it shows only through sky-ceiling sectors.
   Caller emits 1-2 of these to handle the panorama's horizontal wrap. */
T_void RaveViewEmitSky(
           T_byte8 *raw, T_word16 w, T_word16 h,
           const T_raveVertex *topLeft,
           const T_raveVertex *bottomLeft,
           const T_raveVertex *bottomRight,
           const T_raveVertex *topRight)
{
    TQATexture *tex ;

    if (!RaveViewIsActive())
        return ;
    tex = IRaveGetFlat(raw, w, h) ;
    if (tex == NULL)
        return ;
    IRaveDrawTexturedQuad(tex,
        (float)INextPow2(w), (float)INextPow2(h), 1.0f,   /* sky ARGB16, full bright */
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
    int         level ;

    if (!RaveViewIsActive())
        return ;
    /* One shade level for the whole billboard (a sprite is at a single distance). */
    level = (int)(topLeft->light * 63.0f + 0.5f) ;
    if (level < 0)  level = 0 ;
    if (level > 63) level = 63 ;
    tex = IRaveGetSprite(p_picture, w, h, (T_sword16)level) ;
    if (tex == NULL)
        return ;

    IRaveDrawTexturedQuad(tex,
        (float)INextPow2(w), (float)INextPow2(h), 1.0f,   /* shading baked in */
        topLeft, bottomLeft, bottomRight, topRight) ;
}

E_Boolean RaveViewIsActive(T_void)
{
    return ((G_raveContext != NULL) && G_raveEnabled && !G_raveSuspended)
               ? TRUE : FALSE ;
}

/* rave-9: TRUE when RAVE owns the fullscreen present (QARenderEnd swaps its own
   buffer to the display). RESSCALE then skips its scale/composite/SDL_Flip so
   the two don't fight. FALSE = the readback+composite path (software present). */
E_Boolean RaveViewIsDirectPresenting(T_void)
{
    return (RaveViewIsActive() && G_raveDirectPresent) ? TRUE : FALSE ;
}

/* rave-9: request an F8 screenshot of the direct-to-screen frame. The next frame
   is held + read back once in FrameEnd. Used by CLIENT.C when direct-presenting
   (the SDL SaveBMP path can't see RAVE's own buffer). */
T_void RaveViewRequestShot(T_void)
{
    G_raveShotPending = TRUE ;
}

/* Per-frame suspend (escape/options menu over the full 3D view). While
   suspended the RAVE path is dormant and the software renderer draws + shows,
   so that UI isn't hidden by the composite. Clears the ready frame so the
   compositor doesn't draw a stale RAVE view over the menu. */
T_void RaveViewSetSuspended(E_Boolean s)
{
    G_raveSuspended = s ? TRUE : FALSE ;
    if (G_raveSuspended)
        G_raveFrameReady = FALSE ;
}

/* Runtime software<->RAVE switch. RaveViewSetEnabled(FALSE) drops the RAVE
   path immediately (and clears the ready frame so the compositor stops), so
   the next frame renders + shows software. RaveViewToggle returns the new
   state. Used by the in-game hotkey and the resolution.ini "renderer" option. */
T_void RaveViewSetEnabled(E_Boolean on)
{
    G_raveEnabled = on ? TRUE : FALSE ;
    if (!G_raveEnabled)
        G_raveFrameReady = FALSE ;
}

E_Boolean RaveViewGetEnabled(T_void)
{
    return G_raveEnabled ;
}

E_Boolean RaveViewToggle(T_void)
{
    RaveViewSetEnabled(G_raveEnabled ? FALSE : TRUE) ;
    return G_raveEnabled ;
}

#else  /* not a RAVE build -- no-op stubs so this file links everywhere */

T_void    RaveViewInit(T_void)          {}
T_void    RaveViewFinish(T_void)        {}
T_void    RaveViewFrameBegin(T_void)    {}
T_void    RaveViewFrameEnd(T_void)      {}
T_void    RaveViewFlushTextures(T_void) {}
T_void    RaveViewProfileCompositeBegin(void) {}
T_void    RaveViewProfileFrame(void) {}
E_Boolean RaveViewProfileToggle(void)   { return FALSE ; }
E_Boolean RaveViewProfileIsOn(void)     { return FALSE ; }
E_Boolean RaveViewIsActive(T_void)      { return FALSE ; }
E_Boolean RaveViewIsDirectPresenting(T_void) { return FALSE ; }
T_void    RaveViewRequestShot(T_void)   {}
T_void    RaveViewDrawUIAndPresent(const T_byte8 *ui8, int sw, int sh, int pitch)
          { (void)ui8 ; (void)sw ; (void)sh ; (void)pitch ; }

E_Boolean RaveViewGetFrame(T_raveFrame *out)
{
    (void)out ;
    return FALSE ;
}

E_Boolean RaveViewIsPresenting(T_void)  { return FALSE ; }
T_void    RaveViewSetEnabled(E_Boolean on) { (void)on ; }
E_Boolean RaveViewGetEnabled(T_void)    { return FALSE ; }
E_Boolean RaveViewToggle(T_void)        { return FALSE ; }
T_void    RaveViewSetSuspended(E_Boolean s) { (void)s ; }

T_void RaveViewEmitQuad(
           T_byte8 *p_texture,
           float alpha,
           const T_raveVertex *topLeft,
           const T_raveVertex *bottomLeft,
           const T_raveVertex *bottomRight,
           const T_raveVertex *topRight)
{
    (void)p_texture ; (void)alpha ; (void)topLeft ; (void)bottomLeft ;
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

T_void RaveViewEmitSky(
           T_byte8 *raw, T_word16 w, T_word16 h,
           const T_raveVertex *topLeft,
           const T_raveVertex *bottomLeft,
           const T_raveVertex *bottomRight,
           const T_raveVertex *topRight)
{
    (void)raw ; (void)w ; (void)h ; (void)topLeft ; (void)bottomLeft ;
    (void)bottomRight ; (void)topRight ;
}

#endif /* macintosh && AA_RENDERER_RAVE */

/*-------------------------------------------------------------------------*
 * End of File:  RAVE_VIEW.C
 *-------------------------------------------------------------------------*/
