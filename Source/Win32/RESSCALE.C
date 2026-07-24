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

#ifndef SDL_NOEVENT
#define SDL_NOEVENT 0
#endif

#define RESSCALE_INI_FILENAME   "resolution.ini"
#define RESSCALE_MIN_SCALE      1
#define RESSCALE_MAX_SCALE      8
#define RESSCALE_HOTKEY_MAX     6      /* F11 cycles 1x .. 6x            */
#define RESSCALE_MAX_WINDOW_DIM 16384

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
static int G_iniWidth       = 1280;/* explicit window width  (0 = use scale)*/
static int G_iniHeight      = 800; /* explicit window height (0 = use scale)*/
static int G_iniFullscreen  = 1;   /* 1 = fullscreen                        */
static int G_iniAspect      = 0;   /* 1 = stretch 320x200 to 4:3 (x1.2 tall)*/
static int G_iniHotkeys     = 1;   /* 1 = F11 cycles scale, Alt+Enter FS    */

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

    fp = fopen(RESSCALE_INI_FILENAME, "w");
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
        "\n"
        "scale=2\n"
        "fit=fit\n"
        "width=1280\n"
        "height=800\n"
        "fullscreen=1\n"
        "aspect=0\n"
        "hotkeys=1\n",
        fp);
    fclose(fp);
}

static void IReadIni(void)
{
    FILE *fp;
    char  line[256];

    fp = fopen(RESSCALE_INI_FILENAME, "r");
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
    int    winW;
    int    winH;
    Uint32 flags;

    IComputeWindowSize(&winW, &winH);
    if (winW < 1)
        winW = G_logicalW;
    if (winH < 1)
        winH = G_logicalH;

    /* Try for a real double-buffered hardware surface first: on a plain
       SDL_SWSURFACE, SDL_Flip() has no page to swap and falls back to
       SDL_UpdateRect() over the whole surface every call -- profiling on
       PowerPC/Tiger found this costing as much as the CPU-side stretch
       itself. HWSURFACE|DOUBLEBUF lets SDL_Flip do a real (cheap) buffer
       swap when the backend actually supports it; SDL_SetVideoMode
       silently grants whatever it can, so check G_real->flags afterward
       and fall back to plain SWSURFACE (the always-safe, previously-only
       option) if double buffering wasn't actually granted. */
    flags = SDL_HWSURFACE | SDL_DOUBLEBUF;
    if (G_iniFullscreen)
        flags |= SDL_FULLSCREEN;

    G_real = SDL_SetVideoMode(winW, winH, 32, flags);
    if ((G_real == NULL) || (!(G_real->flags & SDL_DOUBLEBUF)))  {
        flags = SDL_SWSURFACE;
        if (G_iniFullscreen)
            flags |= SDL_FULLSCREEN;
        G_real = SDL_SetVideoMode(winW, winH, 32, flags);
        if (G_real == NULL)
            return 0;
    }
    fprintf(stderr, "RESSCALE: video surface flags 0x%08X (%s%s%s)\n",
            (unsigned)G_real->flags,
            (G_real->flags & SDL_HWSURFACE) ? "HW " : "SW ",
            (G_real->flags & SDL_DOUBLEBUF) ? "DOUBLEBUF " : "",
            (G_real->flags & SDL_FULLSCREEN) ? "FULLSCREEN" : "windowed");

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
    G_real   = SDL_SetVideoMode(width, height, bpp, flags);
    G_shadow = G_real;
    return G_real;
}

/*--------------------------------------------------------------------------*/
/* Public: per-frame present                                                */
/*--------------------------------------------------------------------------*/
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

    __ppStart = PerfProfNow() ;
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
