/****************************************************************************/
/*    FILE:  3D_GEO.C                                                       */
/****************************************************************************/
/*
 * Shared 2D geometry / spatial-query helpers used by both the renderer
 * (3D_VIEW.C) and collision (3D_COLLI.C).  Moved verbatim out of
 * 3D_VIEW.C -- see 3D_GEO.H for rationale.  These read the map model
 * owned by 3D_IO.C; they do no rendering.
 */
#include "3D_GEO.H"
#include "3D_IO.H"
#include "GENERAL.H"

/* Cap on candidate lines tracked while locating a sector (from 3D_VIEW.C). */
#define MAX_FIND_SECTOR_LINES 20

/*-------------------------------------------------------------------------*
 * Routine:  IQuickSquareRoot
 *-------------------------------------------------------------------------*/
/**
 *  IQuickSquareRoot uses an approximation method to calculate the
 *  square root of a 32 bit number (into a 16 bit number).
 *
 *  @param value -- 32 bit number to take square root of
 *
 *  @return square root calculated
 *
 *<!-----------------------------------------------------------------------*/
T_word16 IQuickSquareRoot(T_word32 value)
{
    T_sword16 i;
    T_word16 result,tmp;
    T_word32 low,high;

    if (value <= 1L)
         return((T_word16)value);

    low = (T_word32)value;
    high = 0L;
    result = 0;

    for (i = 0; i < 16; i++)  {
        result += result;
        high = (high << 2) | ((low >>30) & 0x3);
        low <<= 2;

        tmp = result + result + 1;
        if (high >= tmp)  {
            result++;
            high -= tmp;
        }
    }

    return(result);
}

T_word16 CalculateDistance(
             T_sword32 x1,
             T_sword32 y1,
             T_sword32 x2,
             T_sword32 y2)
{
    T_sword32 dx ;
    T_sword32 dy ;
    T_word16 shift = 0 ;

    dx = x1-x2 ;
    if (dx < 0)
       dx = -dx ;

    dy = y2 - y1 ;
    if (dy < 0)
        dy = -dy ;

    while ((dx & 0xFFFF0000) || (dy & 0xFFFF0000))  {
        dx >>= 2 ;
        dy >>= 2 ;
        shift += 2 ;
    }

    /* If delta x is zero, then the distance is from y1 to y2 */
    if (dx == 0)
        return (dy<<shift) ;
    if (dy == 0)
        return (dx<<shift) ;

    return (IQuickSquareRoot((dx*dx) + (dy*dy)) << shift) ;
}

T_word16 IFindSectorNum(T_sword16 x, T_sword16 y)
{
    T_word16 lastLine=0xFFFF, lastSide ;   /* Last line found */
    T_word16 sideFound = 0xFFFE ;          /* Last side found (0xFFFE = none) */
    T_sword16 column, row ;                /* Row and column in block map */
    T_word32 index ;                       /* Index into block map */
    T_word16 sideOfLine ;                  /* Side of line x,y is on */
                                           /* 0 = front, 1 = back */
    T_word16 line ;                        /* Current line number */
    E_Boolean newLineFound ;               /* Flag to say, "this line is closer" */
    T_3dLine *p_line ;                     /* Quick pointer to line. */
    T_word16 numLines = 0 ;
    T_word16 lastLines[MAX_FIND_SECTOR_LINES] ;
    T_byte8 lastSides[MAX_FIND_SECTOR_LINES] ;
    T_word16 i ;

#   ifdef COMPILE_OPTION_DEBUG_FIND_SECTOR_NUM
    printf("\n\nIFSN: Point %d, %d (%d, %d)\n", x, y, x-G_3dBlockMapHeader->xOrigin, y-G_3dBlockMapHeader->yOrigin) ;
#   endif

    /* First, find the block map block that this point is located within. */
    column = (x - G_3dBlockMapHeader->xOrigin) >> 7 ;

    if ((column < 0) || (column >= G_3dBlockMapHeader->columns))  {
        /* Out of bounds, return a bad one. */
//        DebugCheck(FALSE) ;
        return 0xFFFF ;
    }

    row = (y - G_3dBlockMapHeader->yOrigin) >> 7 ;

    if ((row < 0) || (row >= G_3dBlockMapHeader->rows))  {
        /* Out of bounds, return a bad one. */
//        DebugCheck(FALSE) ;
        return 0xFFFF ;
    }

    while (sideFound == 0xFFFE)  {
#       ifdef COMPILE_OPTION_DEBUG_FIND_SECTOR_NUM
        printf("IFSN: Block c:%d, r:%d\n", column, row) ;
#       endif
        /* Inbounds, find the index. */
        index = (row * G_3dBlockMapHeader->columns) + column ;

        /* Now translate the index into a position in the list of lines. */
        index = 1+G_3dBlockMapHeader->blockIndexes[index] ;

        /* Loop until we end the list of lines in that block */
        while ((line = G_3dBlockMapArray[index]) != ((T_word16)-1))  {
            /* Get a quick pointer to the line. */
            p_line = G_3dLineArray+line ;

#           ifdef COMPILE_OPTION_DEBUG_FIND_SECTOR_NUM
            printf("IFSN: Check line %d\n", line) ;  fflush(stdout) ;
#           endif

            /* Which side of that line are we on? */
            sideOfLine = IOnRightOfLine(x, y, line) ;

            /* See if we are right on the line. */
            if (sideOfLine == 2)  {
                /* Yes, we need to choose a real side. */
                /* Equal is the same as worse. */
                /* Is there are previously found side. */
                if (sideFound == 0xFFFE)  {
                    /* Then we are in front. */
                    sideOfLine = 0 ;
                } else {
                    /* Otherwise, choose the opposite side of the */
                    /* last line. */
                    if (lastSide == 0)
                        sideOfLine = 1 ;
                    else
                        sideOfLine = 0 ;
                }
            }

            /* Not sure about this line, don't take it yet. */
            newLineFound = FALSE ;

            /* Only bother with lines that have a side facing the x, y */
            if (p_line->side[sideOfLine] != -1)  {
#                   ifdef COMPILE_OPTION_DEBUG_FIND_SECTOR_NUM
                printf("IFSN: Checking side of line %d (%d of line)\n", line, sideOfLine) ;  fflush(stdout) ;
#                   endif

                /* OK, is this a contending side? */
                if (sideFound != 0xFFFE)  {
                    /* A contender is better if either endpoint is in */
                    /* front of the last line. */
                    if (IOnRightOfLine(
                          G_3dVertexArray[p_line->from].x,
                          G_3dVertexArray[p_line->from].y,
                          lastLine) == lastSide)  {
#                           ifdef COMPILE_OPTION_DEBUG_FIND_SECTOR_NUM
                        printf("IFSN: from point %d, %d on same side\n",
                            G_3dVertexArray[p_line->from].x,
                            G_3dVertexArray[p_line->from].y) ;  fflush(stdout) ;
#                           endif

                        /* From point is closer */
                        newLineFound = TRUE ;
                    } else if (IOnRightOfLine(
                                  G_3dVertexArray[p_line->to].x,
                                  G_3dVertexArray[p_line->to].y,
                                  lastLine) == lastSide)  {
#                           ifdef COMPILE_OPTION_DEBUG_FIND_SECTOR_NUM
                        printf("IFSN: to point %d, %d on same side\n",
                            G_3dVertexArray[p_line->from].x,
                            G_3dVertexArray[p_line->from].y) ;  fflush(stdout) ;
#                           endif

                        /* to point is closer. */
                        newLineFound = TRUE ;
                    }
                } else {
                    /* No one else to contend.  This is the closest line. */
                    newLineFound = TRUE ;
                }
            }

            /* We think we have a better line.  Make sure in front */
            /* of all other lines. */
            for (i=0; i<numLines; i++)  {
#                   ifdef COMPILE_OPTION_DEBUG_FIND_SECTOR_NUM
                printf("IFSN: checking reject line %d side %d\n", lastLines[i], lastSides[i]) ;  fflush(stdout) ;
#                   endif
                /* Check to see if this new line is in front of */
                /* all the old lines. */
                if ((IOnRightOfLine(
                      G_3dVertexArray[p_line->from].x,
                      G_3dVertexArray[p_line->from].y,
                      lastLines[i]) != lastSides[i]) &&
                    (IOnRightOfLine(
                              G_3dVertexArray[p_line->to].x,
                              G_3dVertexArray[p_line->to].y,
                              lastLines[i]) != lastSides[i]))  {
#                       ifdef COMPILE_OPTION_DEBUG_FIND_SECTOR_NUM
                    printf("IFSN: rejected due to line %d side %d\n", lastLines[i], lastSides[i]) ;  fflush(stdout) ;
#                       endif
                    newLineFound = FALSE ;
                    break ;
                }
            }

            /* OK, got a better line.  Replace the old one and */
            /* get its coordinates for a little bit of speed. */
            if (newLineFound)  {
DebugCheck(numLines < MAX_FIND_SECTOR_LINES) ;
                /* Put the old one on the line history */
                if ((lastLine != 0xFFFF) &&
                        (numLines < MAX_FIND_SECTOR_LINES))  {
                    lastLines[numLines] = lastLine ;
                    lastSides[numLines] = (T_byte8)lastSide ;
                    numLines++ ;
                }
                sideFound = p_line->side[sideOfLine] ;
#                   ifdef COMPILE_OPTION_DEBUG_FIND_SECTOR_NUM
                printf("IFSN: better line is %d (on side %d aka %d)\n", line, sideOfLine, sideFound) ;
#                   endif
                lastLine = line ;
                lastSide = sideOfLine ;
            }
            index++ ;
        }

        /* If no side is found yet, then advance to the next row. */
        if (sideFound == 0xFFFE)  {
            row++ ;

            /* If past edge, then don't know.  We're out of here. */
            if (row >= G_3dBlockMapHeader->rows)  {
#               ifdef COMPILE_OPTION_DEBUG_FIND_SECTOR_NUM
                DebugCheck(FALSE) ;
#               endif
                return 0xFFFF ;
            }
        }
    }

    if (sideFound == 0xFFFF)
        return 0xFFFF ;

#   ifdef COMPILE_OPTION_DEBUG_FIND_SECTOR_NUM
    DebugCheck(G_3dSideArray[sideFound].sector != 0xFFFF) ;
#   endif

    return G_3dSideArray[sideFound].sector ;
}

T_byte8 IOnRightOfLine(T_sword16 x, T_sword16 y, T_word16 line)
{
    T_3dVertex *p_vertex ;
    T_sword32 x1 ;
    T_sword32 y1 ;
    T_sword32 x2 ;
    T_sword32 y2 ;
    T_word16 from, to ;
    T_sword32 calc ;

    from = G_3dLineArray[line].from ;
    to = G_3dLineArray[line].to ;

    p_vertex = &G_3dVertexArray[from] ;
    x1 = p_vertex->x ;
    y1 = p_vertex->y ;
    p_vertex = &G_3dVertexArray[to] ;
    x2 = p_vertex->x ;
    y2 = p_vertex->y ;

    calc = ((y1 - y2) * (x - x2)) - ((x1 - x2) * (y - y2)) ;

    if (calc < 0)
        return 0 ;
    if (calc > 0)
        return 1 ;

    return 2 ;
}

T_word16 CalculateEstimateDistance(
             T_sword16 x1,
             T_sword16 y1,
             T_sword16 x2,
             T_sword16 y2)
{
    x1 -= x2 ;
    if (x1 < 0)
        x1 = -x1 ;

    y1 -= y2 ;
    if (y1 < 0)
        y1 = -y1 ;

    if (x1 > y1)
       return x1 ;

    return y1 ;
/*
    T_sword16 deltaX, deltaY ;
    T_word16 shift = 0 ;

    deltaX = x1 - x2 ;
    if (deltaX < 0)
        deltaX = -deltaX ;

    deltaY = y1 - y2 ;
    if (deltaY < 0)
        deltaY = -deltaY ;

    if (deltaX > deltaY)  {
        if (deltaX > (deltaY << 1))
            return deltaX ;
        else
            return (deltaX + (deltaX >> 1)) ;
    }

    if (deltaY > (deltaX << 1))
        return deltaY ;

    return (deltaY + (deltaY >> 1)) ;
*/
}

/****************************************************************************/
/*    END OF FILE:  3D_GEO.C                                                */
/****************************************************************************/
