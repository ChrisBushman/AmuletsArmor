/****************************************************************************/
/*    FILE:  HIGHRES.C                                                      */
/****************************************************************************/
/*                                                                          */
/*    High-resolution hybrid frame for Amulets & Armor.                     */
/*    See HIGHRES.H for the design description and API contract.            */
/*                                                                          */
/*    Copyright (c) 2026.  Licensed under the GNU GPL v3, the same          */
/*    license as the Amulets & Armor source release.                        */
/*                                                                          */
/****************************************************************************/
#include <stdlib.h>
#include <string.h>
#include "HIGHRES.H"

/* MAX_STRIDE/MAX_ROWS: the buffer is ALWAYS allocated (and its row-to-row
   pointer math always computed) at HIGHRES_MAX_SCALE, regardless of what
   scale is actually requested -- callers outside this file (3D_VIEW.C's
   View3dInitialize, in particular) walk this buffer using MAX_VIEW3D_WIDTH/
   HEIGHT (3D_VIEW.H), which are themselves always sized for
   VIEW3D_SCALE_MAX, since the 3D renderer's hundreds of `p_pixel +=
   MAX_VIEW3D_WIDTH` pointer-arithmetic sites are compiled once and can't
   vary their stride per frame. Allocating (and internally strproviding)
   only `scale`-sized rows here -- as this used to do -- left every row
   after the first walked at MAX_VIEW3D_WIDTH bytes apart while the real
   buffer's rows were only HIGHRES_BASE_WIDTH*scale bytes apart whenever
   scale < HIGHRES_MAX_SCALE: a heap buffer overrun starting on the very
   first frame after a level/quest load, confirmed on real Windows 95
   hardware as a silent crash (access violation, not a DebugCheck -- no
   error.log) with no visible 3D view at all. G_frameW/G_frameH (and
   HighResFrameWidth/Height) still report the *active* scale-sized
   region within that fixed-stride buffer -- callers that need to know
   how much of each row is actually meaningful (main.c's per-frame
   compose-and-copy loop) still want that, just not as the stride. */
static unsigned char *G_frame = 0;      /* the scaled output frame        */
static int G_scale = 0;                 /* 0 = not initialized            */
static int G_frameW = 0;                /* active width  (<= MAX_STRIDE)  */
static int G_frameH = 0;                /* active height (<= MAX_ROWS)    */

static int G_winEnabled = 0;            /* logical 320x200 view rect      */
static int G_winX = 0;
static int G_winY = 0;
static int G_winW = 0;
static int G_winH = 0;

#define HIGHRES_MAX_STRIDE (HIGHRES_BASE_WIDTH  * HIGHRES_MAX_SCALE)
#define HIGHRES_MAX_ROWS   (HIGHRES_BASE_HEIGHT * HIGHRES_MAX_SCALE)

/*--------------------------------------------------------------------------*/
int HighResInit(int scale)
{
    if ((scale < 1) || (scale > HIGHRES_MAX_SCALE))
        return 0;

    HighResFinish();

    G_frame = (unsigned char *)malloc((size_t)HIGHRES_MAX_STRIDE * (size_t)HIGHRES_MAX_ROWS);
    if (!G_frame)
        return 0;
    memset(G_frame, 0, (size_t)HIGHRES_MAX_STRIDE * (size_t)HIGHRES_MAX_ROWS);

    G_frameW = HIGHRES_BASE_WIDTH * scale;
    G_frameH = HIGHRES_BASE_HEIGHT * scale;
    G_scale = scale;
    return 1;
}

/*--------------------------------------------------------------------------*/
void HighResFinish(void)
{
    if (G_frame)
        free(G_frame);
    G_frame = 0;
    G_scale = 0;
    G_frameW = 0;
    G_frameH = 0;
    G_winEnabled = 0;
}

/*--------------------------------------------------------------------------*/
int HighResGetScale(void)               {  return G_scale;  }
unsigned char *HighResFrame(void)       {  return G_frame;  }
int HighResFrameWidth(void)             {  return G_frameW;  }
int HighResFrameHeight(void)            {  return G_frameH;  }
int HighResFrameStride(void)            {  return HIGHRES_MAX_STRIDE;  }

/*--------------------------------------------------------------------------*/
void HighResSetViewWindow(int x, int y, int w, int h)
{
    if ((w <= 0) || (h <= 0))  {
        G_winEnabled = 0;
        return;
    }

    /* Clamp the rect to the logical screen. */
    if (x < 0)  {  w += x;  x = 0;  }
    if (y < 0)  {  h += y;  y = 0;  }
    if (x + w > HIGHRES_BASE_WIDTH)
        w = HIGHRES_BASE_WIDTH - x;
    if (y + h > HIGHRES_BASE_HEIGHT)
        h = HIGHRES_BASE_HEIGHT - y;
    if ((w <= 0) || (h <= 0))  {
        G_winEnabled = 0;
        return;
    }

    G_winX = x;
    G_winY = y;
    G_winW = w;
    G_winH = h;
    G_winEnabled = 1;
}

/*--------------------------------------------------------------------------*/
void HighResDisableViewWindow(void)     {  G_winEnabled = 0;  }
int HighResViewWindowEnabled(void)      {  return G_winEnabled;  }

int HighResViewWindowX(void)            {  return G_winX;  }
int HighResViewWindowY(void)            {  return G_winY;  }
int HighResViewWindowW(void)            {  return G_winW;  }
int HighResViewWindowH(void)            {  return G_winH;  }

/*--------------------------------------------------------------------------*/
unsigned char *HighResViewPtr(void)
{
    if ((!G_frame) || (!G_winEnabled))
        return 0;
    /* HIGHRES_MAX_STRIDE, not G_frameW: this is the buffer's real
       row-to-row distance (see HighResInit's own comment) -- the 3D
       renderer writes through this pointer using MAX_VIEW3D_WIDTH as
       its own stride assumption, which only matches HIGHRES_MAX_STRIDE,
       not the active (possibly smaller) G_frameW. */
    return G_frame
           + ((size_t)G_winY * (size_t)G_scale) * (size_t)HIGHRES_MAX_STRIDE
           + ((size_t)G_winX * (size_t)G_scale);
}

/*--------------------------------------------------------------------------*/
int HighResViewNativeWidth(void)
{
    return (G_winEnabled) ? (G_winW * G_scale) : 0;
}

/*--------------------------------------------------------------------------*/
int HighResViewNativeHeight(void)
{
    return (G_winEnabled) ? (G_winH * G_scale) : 0;
}

/*--------------------------------------------------------------------------*/
/* Replicate logical pixels src[0..count) into the native row p_dest,       */
/* each pixel expanded to G_scale bytes.                                    */
static void IReplicateSegment(
        unsigned char *p_dest,
        const unsigned char *p_src,
        int count)
{
    int i;
    int s;
    unsigned char c;

    switch (G_scale)  {
        case 1:
            memcpy(p_dest, p_src, (size_t)count);
            break;
        case 2:
            for (i = 0; i < count; i++)  {
                c = p_src[i];
                p_dest[0] = c;
                p_dest[1] = c;
                p_dest += 2;
            }
            break;
        default:
            for (i = 0; i < count; i++)  {
                c = p_src[i];
                for (s = 0; s < G_scale; s++)
                    *(p_dest++) = c;
            }
            break;
    }
}

/*--------------------------------------------------------------------------*/
/* Compose one logical row: replicate the logical pixels in                 */
/* [xStart, xEnd) into all G_scale native rows for logical row y.           */
static void IComposeRowSpan(
        const unsigned char *screen320,
        int y,
        int xStart,
        int xEnd)
{
    unsigned char *p_row;
    int count;
    int dup;

    count = xEnd - xStart;
    if (count <= 0)
        return;

    /* HIGHRES_MAX_STRIDE, not G_frameW -- see HighResViewPtr's own
       comment; every row-to-row pointer step in this file must use the
       buffer's real (fixed) stride, not the active/runtime width. */
    p_row = G_frame
            + ((size_t)y * (size_t)G_scale) * (size_t)HIGHRES_MAX_STRIDE
            + ((size_t)xStart * (size_t)G_scale);

    /* Build the first native row of this logical row... */
    IReplicateSegment(
        p_row,
        screen320 + ((size_t)y * HIGHRES_BASE_WIDTH) + (size_t)xStart,
        count);

    /* ...then duplicate it downward for the remaining native rows. */
    for (dup = 1; dup < G_scale; dup++)
        memcpy(
            p_row + (size_t)dup * (size_t)HIGHRES_MAX_STRIDE,
            p_row,
            (size_t)count * (size_t)G_scale);
}

/*--------------------------------------------------------------------------*/
void HighResCompose(const unsigned char *screen320)
{
    int y;

    if ((!G_frame) || (!screen320))
        return;

    if (!G_winEnabled)  {
        HighResComposeAll(screen320);
        return;
    }

    for (y = 0; y < HIGHRES_BASE_HEIGHT; y++)  {
        if ((y >= G_winY) && (y < (G_winY + G_winH)))  {
            /* Row crosses the view window: fill left and right of it. */
            IComposeRowSpan(screen320, y, 0, G_winX);
            IComposeRowSpan(screen320, y, G_winX + G_winW,
                            HIGHRES_BASE_WIDTH);
        } else {
            IComposeRowSpan(screen320, y, 0, HIGHRES_BASE_WIDTH);
        }
    }
}

/*--------------------------------------------------------------------------*/
void HighResComposeAll(const unsigned char *screen320)
{
    int y;

    if ((!G_frame) || (!screen320))
        return;

    for (y = 0; y < HIGHRES_BASE_HEIGHT; y++)
        IComposeRowSpan(screen320, y, 0, HIGHRES_BASE_WIDTH);
}

/*--------------------------------------------------------------------------*/
void HighResComposeMaskedOver(const unsigned char *screen320)
{
    int y;
    int x;
    int runStart;
    const unsigned char *p_src;

    if ((!G_frame) || (!screen320) || (!G_winEnabled))
        return;

    for (y = G_winY; y < (G_winY + G_winH); y++)  {
        p_src = screen320 + ((size_t)y * HIGHRES_BASE_WIDTH);

        /* Find runs of non-zero pixels and replicate each run. */
        x = G_winX;
        while (x < (G_winX + G_winW))  {
            if (p_src[x] == 0)  {
                x++;
                continue;
            }
            runStart = x;
            while ((x < (G_winX + G_winW)) && (p_src[x] != 0))
                x++;
            IComposeRowSpan(screen320, y, runStart, x);
        }
    }
}

/*--------------------------------------------------------------------------*/
void HighResClearViewRect320(unsigned char *screen320, unsigned char color)
{
    int y;

    if ((!screen320) || (!G_winEnabled))
        return;

    for (y = G_winY; y < (G_winY + G_winH); y++)
        memset(
            screen320 + ((size_t)y * HIGHRES_BASE_WIDTH) + (size_t)G_winX,
            color,
            (size_t)G_winW);
}

/*--------------------------------------------------------------------------*/
void HighResExtractViewTo320(unsigned char *screen320)
{
    int lx;
    int ly;
    const unsigned char *p_srcRow;
    unsigned char *p_dest;

    if ((!G_frame) || (!screen320) || (!G_winEnabled))
        return;

    for (ly = G_winY; ly < (G_winY + G_winH); ly++)  {
        /* HIGHRES_MAX_STRIDE, not G_frameW -- see HighResViewPtr's own
           comment. */
        p_srcRow = G_frame
                   + ((size_t)ly * (size_t)G_scale) * (size_t)HIGHRES_MAX_STRIDE;
        p_dest = screen320 + ((size_t)ly * HIGHRES_BASE_WIDTH);
        for (lx = G_winX; lx < (G_winX + G_winW); lx++)
            p_dest[lx] = p_srcRow[(size_t)lx * (size_t)G_scale];
    }
}

/****************************************************************************/
/*    END OF FILE:  HIGHRES.C                                               */
/****************************************************************************/
