/****************************************************************************/
/*    FILE:  3D_GEO.H                                                       */
/****************************************************************************/
/*
 * Shared 2D geometry / spatial-query helpers used by BOTH the renderer
 * (3D_VIEW.C) and collision (3D_COLLI.C).  These routines are pure
 * geometry over the map model owned by 3D_IO.C (lines, vertices, sides,
 * sectors, block map) -- they perform NO rendering, screen-space, or
 * view-region work.  Extracted out of 3D_VIEW.C so that collision no
 * longer has to include the renderer's header to reach them, and so a
 * future alternate renderer can share the same point-location math.
 */
#ifndef _3D_GEO_H_
#define _3D_GEO_H_

#include "GENERAL.H"
#include "VIEWFILE.H"

/* Map-line semantic flags (bits in T_3dLine.flags).  These describe line
   passability/visibility and are read by both collision and rendering, so
   they live in this shared layer rather than the renderer's header.  Moved
   here out of 3D_VIEW.H; that header re-includes this one, so existing
   includers still see them. */
#define LINE_IS_IMPASSIBLE           0x0001
#define LINE_IS_CREATURE_IMPASSIBLE  0x0002
#define LINE_IS_TWO_SIDED            0x0004
#define LINE_IS_TRANSLUCENT          0x0020
#define LINE_IS_ALWAYS_SOLID         0x0040
#define LINE_IS_INVISIBLE            0x0080
#define LINE_IS_AUTOMAPPED           0x0100
#define LINE_HAS_BEEN_SEEN           0x8000

T_word16  IQuickSquareRoot(T_word32 value) ;

T_word16  CalculateDistance(
              T_sword32 x1,
              T_sword32 y1,
              T_sword32 x2,
              T_sword32 y2) ;

T_word16  CalculateEstimateDistance(
              T_sword16 x1,
              T_sword16 y1,
              T_sword16 x2,
              T_sword16 y2) ;

/* Block-map based point-in-sector location (returns 0xFFFF if outside). */
T_word16  IFindSectorNum(T_sword16 x, T_sword16 y) ;

/* Which side of a map line a point falls on: 0=front, 1=back, 2=on line. */
T_byte8   IOnRightOfLine(T_sword16 x, T_sword16 y, T_word16 line) ;

#define View3dFindSectorNum(x, y) IFindSectorNum(x, y)

#endif

/****************************************************************************/
/*    END OF FILE:  3D_GEO.H                                                */
/****************************************************************************/
