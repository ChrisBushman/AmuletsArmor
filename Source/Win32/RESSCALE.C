/****************************************************************************/
/*    FILE:  RESSCALE.C                                                    */
/****************************************************************************/
/*                                                                          */
/*    Render-resolution / window-scale module for the SDL 1.2 port of      */
/*    Amulets & Armor.  See RESSCALE.H for the integration notes.          */
/*                                                                          */
/*    Design: the game continues to draw into a surface with exactly the   */
/*    width / height / depth it asked SDL for (the "shadow" surface).      */
/*    Once per frame ResScaleUpdate() converts the shadow through the      */
/*    current palette and stretches it (nearest neighbour) into the real   */
/*    display surface, whose size comes from resolution.ini.               */
/*                                                                          */
/*    Because the shadow surface is a normal software SDL surface, every   */
/*    existing call in the game keeps working unchanged:                   */
/*        - direct pixel writes via screen->pixels / screen->pitch         */
/*        - SDL_LockSurface / SDL_UnlockSurface                            */
/*        - SDL_SetColors(screen, ...)   (palette animation included,     */
/*          the colour lookup table is rebuilt every frame)                */
/*                                                                          */
/*    License: GPLv3, same as the rest of Amulets & Armor.                 */
/*                                                                          */
/****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* See Include/OPTIONS.H for why WIN32 must be hidden from SDL's own
   headers here (WIN32 and TARGET_UNIX are both defined together on this
   codebase's Apple/Unix builds). */
#ifdef TARGET_UNIX
#undef WIN32
#include "SDL.h"
#define WIN32 1
#else
#include "SDL.h"
#endif
#include "RESSCALE.H"

/* PERFPROF.H pulls in GENERAL.H for its typedefs -- the one game-header
   dependency in this otherwise-standalone module, added to break down
   where ResScaleUpdate's per-frame cost actually goes (see the
   TICKER_TIME_ROUTINE_* call sites below). */
#include "PERFPROF.H"

/* rave-7: composite the hardware (RAVE) 3D frame into the display, when the
   RAVE renderer is active. RaveViewGetFrame is a no-op stub returning FALSE on
   every non-RAVE build, so ResScaleRaveComposite below is inert there. */
#include "RAVE_VIEW.H"

#ifndef SDL_NOEVENT
#define SDL_NOEVENT 0
#endif

#define RESSCALE_INI_FILENAME   "resolution.ini"
#define RESSCALE_MIN_SCALE      1
#define RESSCALE_MAX_SCALE      8
#define RESSCALE_HOTKEY_MAX     6      /* F11 cycles 1x .. 6x            */
#define RESSCALE_MAX_WINDOW_DIM 16384
/* Must match VIEW3D_SCALE_MAX (3D_VIEW.H) / HIGHRES_MAX_SCALE (HIGHRES.H)
   -- not #included directly, to keep this module dependency-free (SDL 1.2
   and the C library only, per the header comment), so the three ends of
   this contract are individually kept in sync by comment instead. */
#define RESSCALE_DETAIL_MAX     6

/*--------------------------------------------------------------------------*/
/* Configuration (from resolution.ini)                                      */
/*--------------------------------------------------------------------------*/
static int G_iniScale       = 2;   /* integer window scale (1..8)          */
static int G_iniFit         = 0;   /* 0=fit(letterbox) 1=integer 2=stretch  */
/* Fullscreen at an explicit size (matching windowed scale=2) rather than
   windowed at width/height=0 (desktop resolution): profiling on
   PowerPC/Tiger found SDL_Flip on a windowed surface never gets real
   hardware double-buffering there (always falls back to a full software
   update, ~40-47% of frame time), while fullscreen does -- and fullscreen
   at the *desktop* resolution (width/height left at 0) trades that saving
   right back by scaling to more pixels. Explicit matching dimensions get
   both: a near-free SDL_Flip and the smaller scale's pixel count. */
/* 1024x768 (not the wider/taller 1280x800 this used to default to): a
   4:3 SVGA mode close to universally supported on real Win9x-era
   hardware -- confirmed on real Windows 95 hardware that a fullscreen
   request for a size the display driver doesn't support fails outright
   (see the size fallback ladder in IOpenWindow, added at the same time
   as this default change, for when even this isn't supported). */
static int G_iniWidth       = 1024;/* explicit window width  (0 = use scale)*/
static int G_iniHeight      = 768; /* explicit window height (0 = use scale)*/
static int G_iniFullscreen  = 1;   /* 1 = fullscreen                        */
static int G_iniVsync       = 0;   /* 1 = sync present to vblank (macOS/Linux
                                      via sdl12-compat; see ResScaleInit)   */
static int G_iniAspect      = 0;   /* 1 = stretch 320x200 to 4:3 (x1.2 tall)*/
static int G_iniHotkeys     = 1;   /* 1 = F11 cycles scale, Alt+Enter FS    */
/* 0 = auto (try the desktop's own reported bpp first, then 32/16/8 --
   see IOpenWindow). An explicit value skips that whole search and is
   used as-is, both fullscreen and windowed -- for hardware/drivers where
   auto-detection guesses wrong, or to force a specific depth (e.g. an
   8bpp fullscreen mode some legacy Win9x cards only expose at their
   native palette depth, or forcing 16 instead of 32 for speed on slower
   real hardware). */
static int G_iniBpp         = 0;   /* 0 = auto, else 8/16/24/32             */
/* "Level of Detail": how many times larger than classic 320x200 the 3D
   view itself is rendered at (consumed by View3dSetActiveScale, not by
   this module directly -- see 3D_VIEW.H). Range 1..HIGHRES_MAX_SCALE,
   matching VIEW3D_SCALE_MAX. Default of 2 matches the value the game
   used before this was configurable. Independent of "scale" above,
   which is the separate *window* size multiplier. */
static int G_iniDetail       = 2;

/*--------------------------------------------------------------------------*/
/* State                                                                    */
/*--------------------------------------------------------------------------*/
static int          G_initDone     = 0;
static int          G_enabled      = 0;    /* 0 = pass-through fallback     */
static int          G_logicalW     = 0;
static int          G_logicalH     = 0;
static int          G_logicalBpp   = 0;
static Uint32       G_gameFlags    = 0;    /* flags the game asked for      */
static SDL_Surface *G_real         = NULL; /* the actual display surface    */
static SDL_Surface *G_shadow       = NULL; /* what the game draws into      */
static int          G_shotRequest  = 0;    /* F8 screenshot pending (captured at flip) */
static int         *G_xmap         = NULL; /* dest x -> logical x           */
static int         *G_ymap         = NULL; /* dest y -> logical y           */
/* G_xmap is a nearest-neighbour upscale map, identical for every row and
   every frame until the window is resized, and (being monotonic) breaks
   into runs of consecutive dest x's sharing the same source x. Precompute
   those run boundaries once here rather than re-detecting them in
   ResScaleUpdate's per-row, per-frame hot loop (measured: re-detecting
   them there is net *slower* on PowerPC than the plain per-pixel gather
   it was meant to replace -- the data-dependent run-length comparison
   doesn't pay for itself, unlike a precomputed fixed-trip-count fill). */
static int         *G_xRunStart    = NULL; /* start dest x of each run      */
static int         *G_xRunLen      = NULL; /* length (pixels) of each run   */
static int          G_numXRuns     = 0;
static int G_dstX = 0;             /* destination rect inside the window:   */
static int G_dstY = 0;             /* the picture is drawn here, borders    */
static int G_dstW = 0;             /* stay black (letterboxing).            */
static int G_dstH = 0;
static int          G_relRemX      = 0;    /* sub-pixel remainders so tiny  */
static int          G_relRemY      = 0;    /* relative motions aren't lost  */

/*--------------------------------------------------------------------------*/
/* Small helpers                                                            */
/*--------------------------------------------------------------------------*/
static char *IsTrimWhitespace(char *s)
{
    char *end;

    while ((*s != '\0') && (isspace((unsigned char)*s)))
        s++;
    end = s + strlen(s);
    while ((end > s) && (isspace((unsigned char)end[-1])))
        *--end = '\0';
    return s;
}

static int ICompareKey(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0'))  {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb)
            return (ca - cb);
        a++;
        b++;
    }
    return (tolower((unsigned char)*a) - tolower((unsigned char)*b));
}

static int IClampInt(int value, int lo, int hi)
{
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

/*--------------------------------------------------------------------------*/
/* resolution.ini                                                           */
/*--------------------------------------------------------------------------*/
static void IWriteDefaultIni(void)
{
    FILE *fp;

    fp = fopen(RESSCALE_INI_FILENAME, "wb");
    if (fp == NULL)
        return;

    fputs(
        "; Amulets & Armor - window / resolution settings (RESSCALE module)\n"
        "; This file is read once at startup from the game's working directory.\n"
        ";\n"
        "; scale       Integer window scale of the game's native resolution.\n"
        ";             2 turns the 320x200 game into a 640x400 window.\n"
        ";             Ignored if width AND height are set below.  Range 1-8.\n"
        "; width       Explicit window width in pixels  (0 = use scale).\n"
        "; height      Explicit window height in pixels (0 = use scale).\n",
        fp);
    fputs(
        "; fit         How the picture fills the window when sizes differ:\n"
        ";             fit     = as large as possible, aspect kept, black\n"
        ";                       borders (recommended; the default)\n"
        ";             integer = only whole multiples: sharpest possible\n"
        ";             stretch = fill the window, may distort\n",
        fp);
    fputs(
        "; fullscreen  1 = fullscreen.  If width/height are 0, the desktop\n"
        ";             resolution is used.  Defaulted on: SDL_Flip only\n"
        ";             gets real hardware double-buffering fullscreen on\n"
        ";             this game's SDL 1.2 backends, not windowed (measured\n"
        ";             ~40-47% of frame time otherwise). Explicit\n"
        ";             width/height below keep that saving from being\n"
        ";             spent right back on scaling to the full desktop\n"
        ";             resolution instead.\n"
        "; aspect      1 = stretch the picture 20% taller, reproducing the\n"
        ";             4:3 look of a CRT (320x200 was never square pixels).\n"
        ";             0 = crisp square pixels.\n"
        "; hotkeys     1 = F11 cycles the window scale in game, Alt+Enter\n"
        ";             toggles fullscreen.  0 = disable both.\n"
        "; bpp         Color depth: 8, 16, 24, or 32.  0 (default) = auto,\n"
        ";             which tries the desktop's own reported depth first,\n"
        ";             then 32/16/8 in order, and uses whichever one the\n"
        ";             display driver actually grants.  Set this explicitly\n"
        ";             if auto picks wrong (older/real hardware can behave\n"
        ";             differently in exclusive fullscreen than windowed),\n"
        ";             or to force a lower depth for speed.\n"
        "; detail      \"Level of Detail\": how many times larger than\n"
        ";             classic 320x200 the 3D view itself is actually\n"
        ";             rendered at, 1-6.\n"
        ";             Higher looks sharper but costs more CPU per frame;\n"
        ";             lower is faster.  Applies in both windowed and\n"
        ";             fullscreen.  Separate from scale/width/height above,\n"
        ";             which only control the final window size.\n"
        "; vsync       1 = sync the present to the monitor's refresh (removes\n"
        ";             tearing, caps FPS to the refresh rate).  macOS/Linux\n"
        ";             (sdl12-compat) only; ignored on the real-SDL-1.2 builds\n"
        ";             (Windows/PPC/O2).  0 = off (default).\n"
        "; renderer    Which 3D renderer draws the world view:\n"
        ";             software = the CPU renderer (all platforms).\n"
        ";             rave     = the QuickDraw 3D RAVE hardware backend\n"
        ";                        (Mac OS RAVE builds only; ignored elsewhere).\n"
        ";             Only the 3D view changes; all menus/HUD are unaffected.\n"
        ";             In game, F12 toggles it live.  If RAVE can't start, the\n"
        ";             software renderer is used automatically.\n"
        "\n"
        "scale=2\n"
        "fit=fit\n"
        "width=1024\n"
        "height=768\n"
        "fullscreen=1\n"
        "aspect=0\n"
        "hotkeys=1\n"
        "bpp=0\n"
        "detail=2\n"
        "vsync=0\n"
        "renderer=software\n",
        fp);
    fclose(fp);
}

static void IReadIni(void)
{
    FILE *fp;
    char  line[256];

    fp = fopen(RESSCALE_INI_FILENAME, "rb");
    if (fp == NULL)  {
        IWriteDefaultIni();
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL)  {
        char *comment;
        char *equals;
        char *key;
        char *value;

        comment = strchr(line, ';');
        if (comment != NULL)
            *comment = '\0';
        comment = strchr(line, '#');
        if (comment != NULL)
            *comment = '\0';

        equals = strchr(line, '=');
        if (equals == NULL)
            continue;
        *equals = '\0';
        key   = IsTrimWhitespace(line);
        value = IsTrimWhitespace(equals + 1);
        if ((*key == '\0') || (*value == '\0'))
            continue;

        if (ICompareKey(key, "scale") == 0)
            G_iniScale = IClampInt(atoi(value),
                                   RESSCALE_MIN_SCALE,
                                   RESSCALE_MAX_SCALE);
        else if (ICompareKey(key, "width") == 0)
            G_iniWidth = IClampInt(atoi(value), 0, RESSCALE_MAX_WINDOW_DIM);
        else if (ICompareKey(key, "height") == 0)
            G_iniHeight = IClampInt(atoi(value), 0, RESSCALE_MAX_WINDOW_DIM);
        else if (ICompareKey(key, "fullscreen") == 0)
            G_iniFullscreen = (atoi(value) != 0);
        else if (ICompareKey(key, "fit") == 0)  {
            if (ICompareKey(value, "integer") == 0)
                G_iniFit = 1;
            else if (ICompareKey(value, "stretch") == 0)
                G_iniFit = 2;
            else
                G_iniFit = 0;   /* "fit" and anything else */
        }
        else if (ICompareKey(key, "aspect") == 0)
            G_iniAspect = (atoi(value) != 0);
        else if (ICompareKey(key, "hotkeys") == 0)
            G_iniHotkeys = (atoi(value) != 0);
        else if (ICompareKey(key, "bpp") == 0)  {
            int v = atoi(value);
            /* Only real SDL/hardware pixel depths are accepted -- anything
               else (including 0 written back out by hand) falls through to
               auto-detect rather than handing SDL_SetVideoMode a bpp no
               driver actually supports. */
            if ((v == 8) || (v == 16) || (v == 24) || (v == 32))
                G_iniBpp = v;
            else
                G_iniBpp = 0;
        }
        else if (ICompareKey(key, "detail") == 0)
            G_iniDetail = IClampInt(atoi(value), 1, RESSCALE_DETAIL_MAX);
        else if (ICompareKey(key, "vsync") == 0)
            G_iniVsync = (atoi(value) != 0);
        else if (ICompareKey(key, "renderer") == 0)  {
            /* rave-7: 3D renderer choice. "software" forces the CPU renderer;
               "rave"/"hardware" selects the RAVE HW backend. No-op on non-RAVE
               builds. Unset -> software (RAVE is opt-in; see G_raveEnabled). The
               in-game F12 hotkey toggles it live regardless. */
            if (ICompareKey(value, "software") == 0)
                RaveViewSetEnabled(FALSE);
            else if ((ICompareKey(value, "rave") == 0) ||
                     (ICompareKey(value, "hardware") == 0))
                RaveViewSetEnabled(TRUE);
        }
    }
    fclose(fp);
}

/*--------------------------------------------------------------------------*/
/* Window sizing and the nearest-neighbour maps                             */
/*--------------------------------------------------------------------------*/
static void IComputeWindowSize(int *winW, int *winH)
{
    if ((G_iniWidth > 0) && (G_iniHeight > 0))  {
        /* Explicit size wins over everything.                             */
        *winW = G_iniWidth;
        *winH = G_iniHeight;
        return;
    }

    if (G_iniFullscreen)  {
        /* Fullscreen with no explicit size: use the desktop resolution.   */
        const SDL_VideoInfo *info;

        info = SDL_GetVideoInfo();
        if ((info != NULL) && (info->current_w > 0) && (info->current_h > 0))  {
            *winW = info->current_w;
            *winH = info->current_h;
            return;
        }
    }

    {
        /* Scale-derived size; never larger than the desktop.              */
        const SDL_VideoInfo *info;
        int scale = G_iniScale;

        info = SDL_GetVideoInfo();
        while ((scale > 1) && (info != NULL) &&
               (info->current_w > 0) && (info->current_h > 0) &&
               ((G_logicalW * scale > info->current_w) ||
                (((G_iniAspect) ? (G_logicalH * scale * 6) / 5
                                : (G_logicalH * scale)) > info->current_h)))
            scale--;

        *winW = G_logicalW * scale;
        *winH = G_logicalH * scale;
        if (G_iniAspect)  {
            /* 320x200 was drawn on 4:3 monitors: 20% taller than square. */
            *winH = (*winH * 6) / 5;
        }
    }
}

/* Where the picture goes inside the window, per the fit mode.  The
   "shape" of the source is the logical size, made 20% taller when the
   4:3 CRT aspect is requested. */
static void IComputeDestRect(int winW, int winH)
{
    long rw = G_logicalW;
    long rh = (G_iniAspect) ? (((long)G_logicalH * 6) / 5) : G_logicalH;
    int  dw = winW;
    int  dh = winH;

    if (G_iniFit == 1)  {
        /* integer: largest whole multiple that fits (min 1).             */
        long k = winW / rw;
        long ky = winH / rh;

        if (ky < k)
            k = ky;
        if (k < 1)
            k = 1;
        dw = (int)(rw * k);
        dh = (int)(rh * k);
        if ((dw > winW) || (dh > winH))  {
            dw = winW;                  /* window smaller than 1x: fit.   */
            dh = winH;
        }
    }
    if (G_iniFit == 0 || ((G_iniFit == 1) && ((dw > winW) || (dh > winH))))  {
        /* fit: as large as possible with the shape kept.                 */
        if (((long)winW * rh) <= ((long)winH * rw))  {
            dw = winW;
            dh = (int)(((long)winW * rh) / rw);
        } else {
            dh = winH;
            dw = (int)(((long)winH * rw) / rh);
        }
    }
    /* stretch (2): dw/dh stay the full window.                           */
    if (dw < 1)
        dw = 1;
    if (dh < 1)
        dh = 1;

    G_dstW = dw;
    G_dstH = dh;
    G_dstX = (winW - dw) / 2;
    G_dstY = (winH - dh) / 2;
}

/* Paint the whole real surface black so letterbox borders are clean.     */
static void IClearReal(void)
{
    if (G_real == NULL)
        return;
    if (SDL_MUSTLOCK(G_real))  {
        if (SDL_LockSurface(G_real) != 0)
            return;
    }
    memset(G_real->pixels, 0, (size_t)G_real->pitch * (size_t)G_real->h);
    if (SDL_MUSTLOCK(G_real))
        SDL_UnlockSurface(G_real);
}

static int IBuildMaps(int winW, int winH)
{
    int i;
    int runStart;

    IComputeDestRect(winW, winH);

    free(G_xmap);
    free(G_ymap);
    free(G_xRunStart);
    free(G_xRunLen);
    G_xmap = (int *)malloc(sizeof(int) * (size_t)G_dstW);
    G_ymap = (int *)malloc(sizeof(int) * (size_t)G_dstH);
    /* At most G_dstW runs (worst case: every dest pixel its own run). */
    G_xRunStart = (int *)malloc(sizeof(int) * (size_t)(G_dstW > 0 ? G_dstW : 1));
    G_xRunLen   = (int *)malloc(sizeof(int) * (size_t)(G_dstW > 0 ? G_dstW : 1));
    G_numXRuns = 0;
    if ((G_xmap == NULL) || (G_ymap == NULL) ||
        (G_xRunStart == NULL) || (G_xRunLen == NULL))  {
        free(G_xmap);
        free(G_ymap);
        free(G_xRunStart);
        free(G_xRunLen);
        G_xmap = NULL;
        G_ymap = NULL;
        G_xRunStart = NULL;
        G_xRunLen = NULL;
        return 0;
    }

    for (i = 0; i < G_dstW; i++)
        G_xmap[i] = (int)(((long)i * G_logicalW) / G_dstW);
    for (i = 0; i < G_dstH; i++)
        G_ymap[i] = (int)(((long)i * G_logicalH) / G_dstH);

    runStart = 0;
    for (i = 1; i <= G_dstW; i++)  {
        if ((i == G_dstW) || (G_xmap[i] != G_xmap[runStart]))  {
            G_xRunStart[G_numXRuns] = runStart;
            G_xRunLen[G_numXRuns] = i - runStart;
            G_numXRuns++;
            runStart = i;
        }
    }

    IClearReal();
    return 1;
}

/* (Re)creates the real display surface at the configured size and rebuilds
   the stretch maps.  Returns 1 on success.  The shadow surface (and thus
   the pointer the game holds) is never touched here, so this is also used
   for live resizing via the hotkeys. */
static int IOpenWindow(void)
{
    int    reqW, reqH;
    int    sizeListW[4], sizeListH[4];
    int    numSizes;
    int    s;
    Uint32 flags;
    int    bppList[4];
    int    numBpp;
    int    i;
    const SDL_VideoInfo *info;

    IComputeWindowSize(&reqW, &reqH);
    if (reqW < 1)
        reqW = G_logicalW;
    if (reqH < 1)
        reqH = G_logicalH;

/* Only the modern macOS build (sdl12-compat over SDL2) needs this; the PPC/
   Tiger build is also __APPLE__ but links real SDL 1.2 (AA_REAL_SDL12), where
   true fullscreen works and doesn't flicker -- leave it alone. */
#if defined(__APPLE__) && !defined(AA_REAL_SDL12)
    /* macOS: a real fullscreen surface (fullscreen Space / direct scanout)
       makes the display's content-adaptive backlight pulse the brightness of
       dark scenes -- a whole-screen flicker no rendering setting fixes.  So
       when fullscreen is requested we instead make a borderless *window* the
       size of the whole desktop (created below with SDL_NOFRAME, not
       SDL_FULLSCREEN) and cover the screen via ResScaleMacBorderlessFullscreen;
       that keeps us on the flicker-free compositor path.  Force the window to
       the desktop resolution here so it actually fills the display. */
    if (G_iniFullscreen)  {
        const SDL_VideoInfo *dvi = SDL_GetVideoInfo();
        if ((dvi != NULL) && (dvi->current_w > 0) && (dvi->current_h > 0))  {
            reqW = dvi->current_w;
            reqH = dvi->current_h;
        }
    }
#endif

    /* An explicit/computed size can simply not exist on real hardware --
       confirmed on real Windows 95 hardware: a fullscreen request at
       1280x800 (this module's own default) failed outright and fell all
       the way back to ResScaleSetVideoMode's last-resort unscaled
       640x400 path, losing scaling/letterboxing entirely. Try
       progressively smaller common sizes before giving up, same
       rationale as the bpp ladder just below -- a smaller scaled window
       beats no scaling at all. Skipped for an explicit width=/height= in
       resolution.ini when it's already <= the smallest rung (nothing
       smaller to fall back to anyway). */
    numSizes = 0;
    sizeListW[numSizes] = reqW; sizeListH[numSizes] = reqH; numSizes++;
    {
        static const int fallbackW[3] = { 1024, 800, 640 };
        static const int fallbackH[3] = { 768,  600, 480 };
        int f;
        for (f = 0; f < 3; f++)  {
            if ((fallbackW[f] < reqW) || (fallbackH[f] < reqH))  {
                sizeListW[numSizes] = fallbackW[f];
                sizeListH[numSizes] = fallbackH[f];
                numSizes++;
            }
        }
    }

    /* NOTE on the two nested loops below: SDL 1.2 does not guarantee the
       *previous* video surface stays valid once SDL_SetVideoMode is
       called again (a failed call is not guaranteed to leave the prior
       surface intact) -- so as soon as ANY attempt below succeeds
       (G_real != NULL), both loops stop immediately rather than trying
       for something "better" (e.g. real fullscreen instead of a
       downgraded windowed grant) at the risk of trading away a surface
       already in hand for nothing. Candidates are already ordered
       best-first (largest/most-preferred size, then desktop-native bpp),
       so the first success is normally also the best available. */
    G_real = NULL;
    for (s = 0; s < numSizes; s++)  {
    int winW = sizeListW[s];
    int winH = sizeListH[s];

    /* Legacy Win9x-era display drivers frequently only support one
       specific bpp (often 8 or 16) for an *exclusive* fullscreen mode,
       even though the same card handles any bpp fine in a windowed
       (desktop-composited) surface -- SDL_SetVideoMode silently grants
       whatever it can, so a hardcoded 32bpp fullscreen request can fail
       outright and fall all the way back to windowed with no error,
       exactly the failure mode seen on real Windows 95 hardware
       (resolution.ini's fullscreen=1 honoured on windowed but not
       fullscreen). An explicit bpp= in resolution.ini skips all of this
       and is tried alone -- for hardware/drivers where auto-detection
       still guesses wrong, or to just force a specific depth. Otherwise,
       try the desktop's own current bpp first (the one value guaranteed
       supported by the installed driver), then fall back through the
       other common legacy depths. The multi-bpp search only matters for
       fullscreen -- windowed mode goes through SDL's own desktop-format
       conversion regardless of what's requested. */
    numBpp = 0;
    if (G_iniBpp != 0)  {
        bppList[numBpp++] = G_iniBpp;
    } else {
        if (G_iniFullscreen)  {
            info = SDL_GetVideoInfo();
            if ((info != NULL) && (info->vfmt != NULL) &&
                (info->vfmt->BitsPerPixel > 0))
                bppList[numBpp++] = info->vfmt->BitsPerPixel;
        }
        bppList[numBpp++] = 32;
        if (G_iniFullscreen)  {
            bppList[numBpp++] = 16;
            bppList[numBpp++] = 8;
        }
    }

    for (i = 0; i < numBpp; i++)  {
        /* Skip an exact duplicate of the bpp already tried (desktop bpp
           landing on 32/16/8). */
        if ((i > 0) && (bppList[i] == bppList[i-1]))
            continue ;

        /* Try for a real double-buffered hardware surface first: on a
           plain SDL_SWSURFACE, SDL_Flip() has no page to swap and falls
           back to SDL_UpdateRect() over the whole surface every call --
           profiling on PowerPC/Tiger found this costing as much as the
           CPU-side stretch itself. HWSURFACE|DOUBLEBUF lets SDL_Flip do
           a real (cheap) buffer swap when the backend actually supports
           it; check G_real->flags afterward and fall back to plain
           SWSURFACE (the always-safe option) at the same bpp if double
           buffering wasn't actually granted. */
#if defined(__APPLE__) && !defined(AA_REAL_SDL12)
        /* macOS (sdl12-compat): borderless windowed-fullscreen instead of real
           fullscreen (see the note above, and RESSCALE_MAC.m).  A borderless
           desktop-sized window stays on the compositor path -- no flicker --
           while ResScaleMacBorderlessFullscreen() makes it cover everything.
           SDL12COMPAT_FIX_BORDERLESS_FS_WIN=0 (set in main.c) stops
           sdl12-compat from promoting this borderless window back into a real
           fullscreen surface. */
        if (G_iniFullscreen)  {
            extern void ResScaleMacBorderlessFullscreen(void) ;
            flags = SDL_SWSURFACE | SDL_NOFRAME ;
            G_real = SDL_SetVideoMode(winW, winH, bppList[i], flags) ;
            if (G_real != NULL)
                ResScaleMacBorderlessFullscreen() ;
        } else
#endif
        {
        flags = SDL_HWSURFACE | SDL_DOUBLEBUF;
        if (G_iniFullscreen)
            flags |= SDL_FULLSCREEN;

        G_real = SDL_SetVideoMode(winW, winH, bppList[i], flags);
        if ((G_real == NULL) || (!(G_real->flags & SDL_DOUBLEBUF)))  {
            flags = SDL_SWSURFACE;
            if (G_iniFullscreen)
                flags |= SDL_FULLSCREEN;
            G_real = SDL_SetVideoMode(winW, winH, bppList[i], flags);
        }
        }

        if (G_real != NULL)
            break ;
    }

    if (G_real != NULL)
        break ;
    }

    if (G_real == NULL)
        return 0;

    fprintf(stderr, "RESSCALE: video surface flags 0x%08X (%s%s%s), bpp=%d, %dx%d\n",
            (unsigned)G_real->flags,
            (G_real->flags & SDL_HWSURFACE) ? "HW " : "SW ",
            (G_real->flags & SDL_DOUBLEBUF) ? "DOUBLEBUF " : "",
            (G_real->flags & SDL_FULLSCREEN) ? "FULLSCREEN" : "windowed",
            G_real->format->BitsPerPixel, G_real->w, G_real->h);

    return IBuildMaps(G_real->w, G_real->h);
}

/*--------------------------------------------------------------------------*/
/* Public: init + mode setting                                              */
/*--------------------------------------------------------------------------*/
void ResScaleInit(void)
{
    if (G_initDone)
        return;
    G_initDone = 1;
    IReadIni();

#if defined(TARGET_UNIX) && !defined(macintosh)
    /* resolution.ini "vsync=" -> sdl12-compat's SDL12COMPAT_SYNC_TO_VBLANK
       (macOS/Linux; the AALauncher exposes this option only there, since the
       real-SDL-1.2 builds have no runtime vsync toggle).  It is read during
       SDL_SetVideoMode, which ResScaleSetVideoMode calls after this, so set it
       here.  overwrite=1 makes the launcher's choice authoritative.  Harmless
       on real SDL 1.2 (PPC/O2), which ignores the variable.

       putenv(), not setenv(): IRIX's old libc/stdlib.h doesn't declare setenv
       (it links unresolved there), while putenv() is plain POSIX.1-2001. The
       buffer must outlive the call (putenv may store the pointer, not a copy),
       hence static. */
    static char vsyncEnv[40] ;
    sprintf(vsyncEnv, "SDL12COMPAT_SYNC_TO_VBLANK=%s", G_iniVsync ? "1" : "0") ;
    putenv(vsyncEnv) ;
#endif
}

SDL_Surface *ResScaleSetVideoMode(
        int width,
        int height,
        int bpp,
        Uint32 flags)
{
    SDL_Surface *shadow;

    ResScaleInit();

    /* The game may legitimately set the mode more than once.             */
    if (G_shadow != NULL)  {
        SDL_FreeSurface(G_shadow);
        G_shadow = NULL;
    }
    G_enabled    = 0;
    G_logicalW   = width;
    G_logicalH   = height;
    G_logicalBpp = (bpp > 0) ? bpp : 8;
    G_gameFlags  = flags;
    G_relRemX    = 0;
    G_relRemY    = 0;

    if (!IOpenWindow())
        goto fallback;

    /* The shadow surface is what the game gets back.  For 8-bit we create
       a palettized surface (SDL allocates the palette; the game fills it
       with SDL_SetColors as before).  For higher depths we clone the real
       surface's pixel format so the game's SDL_MapRGB(screen->format,...)
       calls stay correct and the stretch is a plain pixel copy.          */
    if (G_logicalBpp == 8)  {
        shadow = SDL_CreateRGBSurface(SDL_SWSURFACE,
                                      G_logicalW, G_logicalH, 8,
                                      0, 0, 0, 0);
    } else {
        shadow = SDL_CreateRGBSurface(SDL_SWSURFACE,
                                      G_logicalW, G_logicalH,
                                      G_real->format->BitsPerPixel,
                                      G_real->format->Rmask,
                                      G_real->format->Gmask,
                                      G_real->format->Bmask,
                                      0);
    }
    if (shadow == NULL)
        goto fallback;

    G_shadow  = shadow;
    G_enabled = 1;
    return G_shadow;

fallback:
    /* Could not set up scaling: behave exactly like the original code.   */
    fprintf(stderr,
            "RESSCALE: falling back to unscaled %dx%d video (%s)\n",
            width, height, SDL_GetError());
    free(G_xmap);
    free(G_ymap);
    free(G_xRunStart);
    free(G_xRunLen);
    G_xmap       = NULL;
    G_ymap       = NULL;
    G_xRunStart  = NULL;
    G_xRunLen    = NULL;
    G_numXRuns   = 0;
    /* IOpenWindow already tried every plausible bpp for fullscreen and
       still didn't get a usable surface at all -- but still honour
       resolution.ini's fullscreen=1 here rather than silently going
       windowed: the caller's own `flags` (the game's original
       SDL_SetVideoMode request) never includes SDL_FULLSCREEN itself,
       so without this a total IOpenWindow failure downgraded a
       fullscreen-configured game straight to windowed with no
       diagnostic at all. */
    if (G_iniFullscreen)
        flags |= SDL_FULLSCREEN;
    G_real   = SDL_SetVideoMode(width, height, bpp, flags);
    G_shadow = G_real;
    return G_real;
}

/*--------------------------------------------------------------------------*/
/* rave-7: composite the hardware RAVE 3D frame into the display window.     */
/*                                                                          */
/* Called at the end of ResScaleUpdate, after the 8-bit UI+software frame is */
/* already stretched into G_real, just before SDL_Flip. Scales the RAVE      */
/* RGB555 view (VIEW3D_WIDTH x VIEW3D_HEIGHT) into the on-screen VIEW3D rect */
/* (same logical->window mapping ResScale uses; the 3D view sits at base     */
/* 320x200 origin (4,3)) and packs pixels straight into G_real's format via  */
/* its shifts/losses (no per-pixel SDL_MapRGB -- too slow on the G3).        */
/* No-op on non-RAVE builds (RaveViewGetFrame returns FALSE).               */
/*--------------------------------------------------------------------------*/
#define RAVE_BASE_W       320       /* classic logical frame the UI maps from */
#define RAVE_BASE_H       200
#define RAVE_VIEW_ORIGX     4       /* VIEW3D_ORIGIN_X in base 320x200 coords */
#define RAVE_VIEW_ORIGY     3

static void ResScaleRaveComposite(void)
{
    T_raveFrame     fr ;
    SDL_PixelFormat *fmt ;
    Uint8          *dstBase ;
    int             dstPitch, realBytes ;
    int             x0, y0, rw, rh, x, y ;
    int             rLoss, gLoss, bLoss, rSh, gSh, bSh ;

    if ((G_real == NULL) || (G_logicalW <= 0) || (G_logicalH <= 0))
        return ;
    if (!RaveViewGetFrame(&fr))
        return ;                       /* no RAVE frame -> software view shows */
    if ((fr.viewW == 0) || (fr.viewH == 0) || (fr.pixels == NULL))
        return ;

    /* On-screen rect of the CURRENT (possibly menu-shrunk) view, in G_real
       window pixels. The view is ALWAYS left-anchored at the base origin (4,3)
       -- View3dClipCenter centers the clip in the buffer but the renderer
       left-aligns the visible content (G_doublePtrLookup -= CLIP_LEFT), so the
       destination gets NO viewX shift; viewX only offsets the SOURCE read
       (below) to pick the centered visible columns. Width follows the clip. */
    x0 = G_dstX + (int)(((long)fr.overlayOrgX * G_dstW) / RAVE_BASE_W) ;
    y0 = G_dstY + (int)(((long)fr.overlayOrgY * G_dstH) / RAVE_BASE_H) ;
    rw = (int)(((long)fr.viewW * G_dstW) / G_logicalW) ;
    rh = (int)(((long)fr.viewH * G_dstH) / G_logicalH) ;
    if ((rw <= 0) || (rh <= 0))
        return ;

    if (SDL_MUSTLOCK(G_real))  {
        if (SDL_LockSurface(G_real) != 0)
            return ;
    }
    fmt       = G_real->format ;
    dstBase   = (Uint8 *)G_real->pixels ;
    dstPitch  = G_real->pitch ;
    realBytes = fmt->BytesPerPixel ;
    rLoss = fmt->Rloss ; gLoss = fmt->Gloss ; bLoss = fmt->Bloss ;
    rSh   = fmt->Rshift ; gSh  = fmt->Gshift ; bSh  = fmt->Bshift ;

    for (y = 0 ; y < rh ; y++)  {
        int       wy = y0 + y ;
        int       sy = (int)(((long)y * fr.viewH) / rh) ;   /* nearest-neighbour */
        T_word16 *srcRow ;
        Uint8    *dstRow ;
        int       by ;

        if ((wy < 0) || (wy >= G_real->h))
            continue ;
        if (sy >= fr.viewH) sy = fr.viewH - 1 ;
        if (fr.flipY) sy = fr.viewH - 1 - sy ;   /* RAVE back-buffer is bottom-up */
        srcRow = fr.pixels + (long)sy * fr.stride + fr.viewX ;
        dstRow = dstBase + (long)wy * dstPitch ;

        /* overlay row (base 320x200), from this window row */
        by = (int)(((long)(wy - G_dstY) * fr.overlayH) / G_dstH) ;
        if (by < 0) by = 0 ; if (by >= fr.overlayH) by = fr.overlayH - 1 ;

        for (x = 0 ; x < rw ; x++)  {
            int      wx = x0 + x ;
            int      sx = (int)(((long)x * fr.viewW) / rw) ;
            int      bx ;
            unsigned r8, g8, b8 ;
            Uint32   col ;
            T_word16 p ;

            if ((wx < 0) || (wx >= G_real->w))
                continue ;
            if (sx >= fr.viewW) sx = fr.viewW - 1 ;

            /* Overlay key: where AA drew UI over the view (weapon, overhead
               map, crosshair, cursor -> overlay != 0), keep it; only the 3D
               background (overlay == 0) gets the RAVE pixel. */
            if (fr.overlay != NULL)  {
                bx = (int)(((long)(wx - G_dstX) * fr.overlayW) / G_dstW) ;
                if (bx < 0) bx = 0 ; if (bx >= fr.overlayW) bx = fr.overlayW - 1 ;
                if (fr.overlay[(long)by * fr.overlayW + bx] != 0)
                    continue ;
            }

            p  = srcRow[sx] ;
            r8 = (unsigned)(((p >> 10) & 0x1F) << 3) ;   /* 5-bit -> 8-bit */
            g8 = (unsigned)(((p >>  5) & 0x1F) << 3) ;
            b8 = (unsigned)(( p        & 0x1F) << 3) ;
            col = (Uint32)(((r8 >> rLoss) << rSh) |
                           ((g8 >> gLoss) << gSh) |
                           ((b8 >> bLoss) << bSh)) ;

            switch (realBytes)  {
            case 4: *((Uint32 *)(dstRow + (size_t)wx * 4)) = col ; break ;
            case 2: *((Uint16 *)(dstRow + (size_t)wx * 2)) = (Uint16)col ; break ;
            case 3:
                dstRow[(size_t)wx * 3 + 0] = (Uint8)( col        & 0xFF) ;
                dstRow[(size_t)wx * 3 + 1] = (Uint8)((col >>  8)  & 0xFF) ;
                dstRow[(size_t)wx * 3 + 2] = (Uint8)((col >> 16)  & 0xFF) ;
                break ;
            default: break ;
            }
        }
    }

    if (SDL_MUSTLOCK(G_real))
        SDL_UnlockSurface(G_real) ;
}

/*--------------------------------------------------------------------------*/
/* Public: per-frame present                                                */
/*--------------------------------------------------------------------------*/
/* F8 screenshot. The input loop (CLIENT.C) calls ResScaleRequestScreenshot();
   the actual save happens here at the next present, when G_real holds the fully
   composited frame (game UI + the RAVE 3D view laid over it) and is unlocked.
   Writes AAshotNN.bmp into the app folder so software-vs-RAVE frames can be
   compared as real images off-device. Session-incrementing name -- copy the
   BMPs off between runs (a later run restarts the numbering). */
void ResScaleRequestScreenshot(void)
{
    G_shotRequest = 1;
}

static void IMaybeSaveShot(void)
{
    static int shotNum = 0;
    char       name[32];

    if ((!G_shotRequest) || (G_real == NULL))
        return;
    G_shotRequest = 0;
    sprintf(name, "AAshot%02d.bmp", shotNum++);
    SDL_SaveBMP(G_real, name);   /* G_real is unlocked at both flip sites */
}

void ResScaleUpdate(void)
{
    Uint32       lut[256];
    int          x;
    int          y;
    int          winW;
    int          winH;
    int          realBytes;
    Uint8       *srcBase;
    Uint8       *dstBase;
    int          srcPitch;
    int          dstPitch;
    static T_perfProfSlot *__ppSlotLut = NULL ;
    static T_perfProfSlot *__ppSlotCopy = NULL ;
    static T_perfProfSlot *__ppSlotFlip = NULL ;
    unsigned long long __ppStart ;

    if (G_real == NULL)
        return;

    if ((!G_enabled) || (G_shadow == G_real))  {
        IMaybeSaveShot();
        SDL_Flip(G_real);
        return;
    }

    /* Rebuild the colour lookup table every frame: the game animates the
       palette (fades, damage flashes, lava glow), so it is never safe to
       cache this across frames.  256 SDL_MapRGB calls are negligible.    */
    __ppStart = PerfProfNow() ;
    if (G_shadow->format->BitsPerPixel == 8)  {
        SDL_Palette *pal;

        pal = G_shadow->format->palette;
        for (x = 0; x < 256; x++)  {
            if ((pal != NULL) && (x < pal->ncolors))
                lut[x] = SDL_MapRGB(G_real->format,
                                    pal->colors[x].r,
                                    pal->colors[x].g,
                                    pal->colors[x].b);
            else
                lut[x] = 0;
        }
    }
    PerfProfRecord(&__ppSlotLut, "ResScaleUpdate/lut", __ppStart) ;
    __ppStart = PerfProfNow() ;

    if (SDL_MUSTLOCK(G_real))  {
        if (SDL_LockSurface(G_real) != 0)
            return;
    }
    if (SDL_MUSTLOCK(G_shadow))  {
        if (SDL_LockSurface(G_shadow) != 0)  {
            if (SDL_MUSTLOCK(G_real))
                SDL_UnlockSurface(G_real);
            return;
        }
    }

    winW      = G_dstW;
    winH      = G_dstH;
    realBytes = G_real->format->BytesPerPixel;
    srcBase   = (Uint8 *)G_shadow->pixels;
    dstBase   = (Uint8 *)G_real->pixels;
    srcPitch  = G_shadow->pitch;
    dstPitch  = G_real->pitch;

    if (G_shadow->format->BitsPerPixel == 8)  {
        /* 8-bit palettized game frame -> true-colour window.  G_xRunStart/
           G_xRunLen (precomputed once in IBuildMaps, see its comment)
           group consecutive dest x's that share the same source column,
           so each row does one lut lookup per run and a fixed-trip-count
           fill, instead of re-walking G_xmap->srcRow->lut (three
           dependent loads) for every output pixel; this is the loop
           PERFPROF identified as ~40% of frame time on PowerPC/Tiger. */
        for (y = 0; y < winH; y++)  {
            Uint8 *srcRow = srcBase + (G_ymap[y] * srcPitch);
            Uint8 *dstRow = dstBase + ((y + G_dstY) * dstPitch)
                            + ((size_t)G_dstX * realBytes);
            int r;

            if (realBytes == 4)  {
                Uint32 *dst = (Uint32 *)dstRow;

                for (r = 0; r < G_numXRuns; r++)  {
                    int start = G_xRunStart[r];
                    int len = G_xRunLen[r];
                    Uint32 value = lut[srcRow[G_xmap[start]]];

                    for (x = 0; x < len; x++)
                        dst[start + x] = value;
                }
            } else if (realBytes == 2)  {
                Uint16 *dst = (Uint16 *)dstRow;

                for (r = 0; r < G_numXRuns; r++)  {
                    int start = G_xRunStart[r];
                    int len = G_xRunLen[r];
                    Uint16 value = (Uint16)lut[srcRow[G_xmap[start]]];

                    for (x = 0; x < len; x++)
                        dst[start + x] = value;
                }
            } else {
                /* 24 bpp or anything exotic: byte-wise, still correct.   */
                Uint8 *dst = dstRow;

                for (r = 0; r < G_numXRuns; r++)  {
                    int start = G_xRunStart[r];
                    int len = G_xRunLen[r];
                    Uint32 v = lut[srcRow[G_xmap[start]]];

                    for (x = 0; x < len; x++)
                        memcpy(dst + ((size_t)(start + x) * realBytes), &v,
                               (size_t)realBytes);
                }
            }
        }
    } else {
        /* Shadow shares the real surface's format: straight stretch.     */
        int shadowBytes = G_shadow->format->BytesPerPixel;

        for (y = 0; y < winH; y++)  {
            Uint8 *srcRow = srcBase + (G_ymap[y] * srcPitch);
            Uint8 *dst    = dstBase + ((y + G_dstY) * dstPitch)
                            + ((size_t)G_dstX * G_shadow->format->BytesPerPixel);

            if (shadowBytes == 4)  {
                Uint32 *d = (Uint32 *)dst;
                Uint32 *s = (Uint32 *)srcRow;

                for (x = 0; x < winW; x++)
                    d[x] = s[G_xmap[x]];
            } else if (shadowBytes == 2)  {
                Uint16 *d = (Uint16 *)dst;
                Uint16 *s = (Uint16 *)srcRow;

                for (x = 0; x < winW; x++)
                    d[x] = s[G_xmap[x]];
            } else {
                for (x = 0; x < winW; x++)
                    memcpy(dst + ((size_t)x * shadowBytes),
                           srcRow + ((size_t)G_xmap[x] * shadowBytes),
                           (size_t)shadowBytes);
            }
        }
    }

    if (SDL_MUSTLOCK(G_shadow))
        SDL_UnlockSurface(G_shadow);
    if (SDL_MUSTLOCK(G_real))
        SDL_UnlockSurface(G_real);
    PerfProfRecord(&__ppSlotCopy, "ResScaleUpdate/copy", __ppStart) ;

    /* rave-7: lay the hardware 3D view over the UI just before present. */
    ResScaleRaveComposite() ;

    __ppStart = PerfProfNow() ;
    IMaybeSaveShot();
    SDL_Flip(G_real);
    PerfProfRecord(&__ppSlotFlip, "ResScaleUpdate/flip", __ppStart) ;
}

/*--------------------------------------------------------------------------*/
/* Coordinate translation                                                   */
/*--------------------------------------------------------------------------*/
static int IWindowToLogicalX(int x)
{
    int v;

    if ((!G_enabled) || (G_real == NULL) || (G_dstW <= 0))
        return x;
    v = (int)(((long)(x - G_dstX) * G_logicalW) / G_dstW);
    return IClampInt(v, 0, G_logicalW - 1);
}

static int IWindowToLogicalY(int y)
{
    int v;

    if ((!G_enabled) || (G_real == NULL) || (G_dstH <= 0))
        return y;
    v = (int)(((long)(y - G_dstY) * G_logicalH) / G_dstH);
    return IClampInt(v, 0, G_logicalH - 1);
}

/* Relative motion is scaled with carry so that slow mouse movement in a
   large window still trickles through as occasional 1-pixel steps
   instead of rounding away to zero forever. */
static int IScaleRelX(int xrel)
{
    long num;
    int  out;

    if ((!G_enabled) || (G_real == NULL) || (G_dstW <= 0))
        return xrel;
    num      = ((long)xrel * G_logicalW) + G_relRemX;
    out      = (int)(num / G_dstW);
    G_relRemX = (int)(num - ((long)out * G_dstW));
    return out;
}

static int IScaleRelY(int yrel)
{
    long num;
    int  out;

    if ((!G_enabled) || (G_real == NULL) || (G_dstH <= 0))
        return yrel;
    num      = ((long)yrel * G_logicalH) + G_relRemY;
    out      = (int)(num / G_dstH);
    G_relRemY = (int)(num - ((long)out * G_dstH));
    return out;
}

/*--------------------------------------------------------------------------*/
/* Hotkeys                                                                  */
/*--------------------------------------------------------------------------*/
static void IHotkeyResize(void)
{
    if (!IOpenWindow())  {
        /* Very unlikely; try to fall back to 1x windowed.                */
        G_iniFullscreen = 0;
        G_iniScale      = 1;
        G_iniWidth      = 0;
        G_iniHeight     = 0;
        IOpenWindow();
    }
}

static int IHandleHotkey(SDL_Event *event)
{
    SDLKey sym;
    int    altHeld;

    if ((!G_iniHotkeys) || (!G_enabled))
        return 0;
    if (event->type != SDL_KEYDOWN)
        return 0;

    sym     = event->key.keysym.sym;
    altHeld = ((event->key.keysym.mod & (KMOD_LALT | KMOD_RALT)) != 0);

    if ((sym == SDLK_RETURN) && (altHeld))  {
        G_iniFullscreen = !G_iniFullscreen;
        IHotkeyResize();
        return 1;
    }

    if (sym == SDLK_F11)  {
        /* Cycle the windowed integer scale.                              */
        G_iniFullscreen = 0;
        G_iniWidth      = 0;
        G_iniHeight     = 0;
        G_iniScale++;
        if (G_iniScale > RESSCALE_HOTKEY_MAX)
            G_iniScale = 1;
        IHotkeyResize();
        return 1;
    }

    return 0;
}

/*--------------------------------------------------------------------------*/
/* Public: events + mouse                                                   */
/*--------------------------------------------------------------------------*/
void ResScaleEvent(SDL_Event *event)
{
    if (event == NULL)
        return;

    if (IHandleHotkey(event))  {
        /* Swallow the keystroke so the game never sees it.               */
        event->type = SDL_NOEVENT;
        return;
    }

    if (!G_enabled)
        return;

    switch (event->type)  {
        case SDL_MOUSEMOTION:
            event->motion.x    = (Uint16)IWindowToLogicalX(event->motion.x);
            event->motion.y    = (Uint16)IWindowToLogicalY(event->motion.y);
            event->motion.xrel = (Sint16)IScaleRelX(event->motion.xrel);
            event->motion.yrel = (Sint16)IScaleRelY(event->motion.yrel);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            event->button.x = (Uint16)IWindowToLogicalX(event->button.x);
            event->button.y = (Uint16)IWindowToLogicalY(event->button.y);
            break;
        default:
            break;
    }
}

void ResScaleWarpMouse(Uint16 logicalX, Uint16 logicalY)
{
    int wx;
    int wy;

    if ((!G_enabled) || (G_real == NULL))  {
        SDL_WarpMouse(logicalX, logicalY);
        return;
    }

    /* Map to the centre of the destination pixel block so warping to
       (x, y) and reading the position back returns (x, y).               */
    wx = G_dstX +
         (int)((((long)logicalX * G_dstW) + (G_dstW / 2)) / G_logicalW);
    wy = G_dstY +
         (int)((((long)logicalY * G_dstH) + (G_dstH / 2)) / G_logicalH);
    wx = IClampInt(wx, 0, G_real->w - 1);
    wy = IClampInt(wy, 0, G_real->h - 1);
    SDL_WarpMouse((Uint16)wx, (Uint16)wy);
}

Uint8 ResScaleGetMouseState(int *logicalX, int *logicalY)
{
    int   x;
    int   y;
    Uint8 buttons;

    buttons = SDL_GetMouseState(&x, &y);
    if (logicalX != NULL)
        *logicalX = IWindowToLogicalX(x);
    if (logicalY != NULL)
        *logicalY = IWindowToLogicalY(y);
    return buttons;
}

/*--------------------------------------------------------------------------*/
/* Public: diagnostics                                                      */
/*--------------------------------------------------------------------------*/
int ResScaleGetLogicalWidth(void)
{
    return G_logicalW;
}

int ResScaleGetLogicalHeight(void)
{
    return G_logicalH;
}

int ResScaleGetWindowWidth(void)
{
    return (G_real != NULL) ? G_real->w : 0;
}

int ResScaleGetWindowHeight(void)
{
    return (G_real != NULL) ? G_real->h : 0;
}

int ResScaleGetDetail(void)
{
    ResScaleInit();
    return G_iniDetail;
}

#ifdef RESSCALE_TESTING
/* Test hook: reset all module state between unit-test scenarios.  Compiled
   only when -DRESSCALE_TESTING is given; not part of the game build.      */
void ResScaleTestReset(void)
{
    if ((G_shadow != NULL) && (G_shadow != G_real))
        SDL_FreeSurface(G_shadow);
    free(G_xmap);
    free(G_ymap);
    free(G_xRunStart);
    free(G_xRunLen);
    G_xmap        = NULL;
    G_ymap        = NULL;
    G_xRunStart   = NULL;
    G_xRunLen     = NULL;
    G_numXRuns    = 0;
    G_shadow      = NULL;
    G_real        = NULL;
    G_initDone    = 0;
    G_enabled     = 0;
    G_logicalW    = 0;
    G_logicalH    = 0;
    G_logicalBpp  = 0;
    G_relRemX     = 0;
    G_relRemY     = 0;
    G_iniScale    = 3;
    G_iniFit      = 0;
    G_iniWidth    = 0;
    G_iniHeight   = 0;
    G_iniFullscreen = 0;
    G_iniAspect   = 0;
    G_iniHotkeys  = 1;
    G_dstX        = 0;
    G_dstY        = 0;
    G_dstW        = 0;
    G_dstH        = 0;
}
#endif /* RESSCALE_TESTING */

/****************************************************************************/
/*    END OF FILE: RESSCALE.C                                               */
/****************************************************************************/
