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

static unsigned char *G_frame = 0;      /* the scaled output frame        */
static int G_scale = 0;                 /* 0 = not initialized            */
static int G_frameW = 0;
static int G_frameH = 0;

static int G_winEnabled = 0;            /* logical 320x200 view rect      */
static int G_winX = 0;
static int G_winY = 0;
static int G_winW = 0;
static int G_winH = 0;

/*--------------------------------------------------------------------------*/
int HighResInit(int scale)
{
    if ((scale < 1) || (scale > HIGHRES_MAX_SCALE))
        return 0;

    HighResFinish();

    G_frameW = HIGHRES_BASE_WIDTH * scale;
    G_frameH = HIGHRES_BASE_HEIGHT * scale;
    G_frame = (unsigned char *)malloc((size_t)G_frameW * (size_t)G_frameH);
    if (!G_frame)  {
        G_frameW = 0;
        G_frameH = 0;
        return 0;
    }
    memset(G_frame, 0, (size_t)G_frameW * (size_t)G_frameH);
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
int HighResFrameStride(void)            {  return G_frameW;  }

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

/*--------------------------------------------------------------------------*/
unsigned char *HighResViewPtr(void)
{
    if ((!G_frame) || (!G_winEnabled))
        return 0;
    return G_frame
           + ((size_t)G_winY * (size_t)G_scale) * (size_t)G_frameW
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

    p_row = G_frame
            + ((size_t)y * (size_t)G_scale) * (size_t)G_frameW
            + ((size_t)xStart * (size_t)G_scale);

    /* Build the first native row of this logical row... */
    IReplicateSegment(
        p_row,
        screen320 + ((size_t)y * HIGHRES_BASE_WIDTH) + (size_t)xStart,
        count);

    /* ...then duplicate it downward for the remaining native rows. */
    for (dup = 1; dup < G_scale; dup++)
        memcpy(
            p_row + (size_t)dup * (size_t)G_frameW,
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
        p_srcRow = G_frame
                   + ((size_t)ly * (size_t)G_scale) * (size_t)G_frameW;
        p_dest = screen320 + ((size_t)ly * HIGHRES_BASE_WIDTH);
        for (lx = G_winX; lx < (G_winX + G_winW); lx++)
            p_dest[lx] = p_srcRow[(size_t)lx * (size_t)G_scale];
    }
}

/****************************************************************************/
/*    END OF FILE:  HIGHRES.C                                               */
/****************************************************************************/
