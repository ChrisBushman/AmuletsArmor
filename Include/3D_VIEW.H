/****************************************************************************/
/*    FILE:  3D_VIEW.H                                                      */
/****************************************************************************/

#ifndef _3D_VIEW_H_
#define _3D_VIEW_H_

#include "GENERAL.H"
#include "VIEWFILE.H"
#include "GRAPHICS.H"   /* TRUE-HIGHRES: T_screen */
#include "3D_GEO.H"     /* CalculateDistance/EstimateDistance, IFindSectorNum,
                           IOnRightOfLine + View3dFindSectorNum, moved out here */

/*
#define VIEW3D_WIDTH 300
#define VIEW3D_HEIGHT 140
#define VIEW3D_HALF_WIDTH (VIEW3D_WIDTH/2)
#define VIEW3D_HALF_HEIGHT (VIEW3D_HEIGHT/2)
*/
/* Upper bound for the runtime-selectable "Level of Detail" below. Fixed
   render buffers (this file's own MAX_VIEW3D_WIDTH/HEIGHT, plus
   MAX_WALL_RUNS/MAX_FLOOR_INFO/MAX_OBJECT_COLUMN_RUNS in 3D_VIEW.C) are
   always sized for this MAX, regardless of what's actually selected at
   runtime -- same pattern already used for VIEW3D_WIDTH being a runtime
   value smaller than its own MAX_VIEW3D_WIDTH buffer bound.

   MUST equal HIGHRES_MAX_SCALE (HIGHRES.H): HighResInit() always
   allocates its frame at HIGHRES_MAX_SCALE's fixed stride now (see that
   file's own comment), and View3dInitialize() below walks that same
   buffer using MAX_VIEW3D_WIDTH/HEIGHT -- if the two ceilings ever
   disagree, the buffer's real stride and the stride the 3D renderer
   assumes diverge again, exactly the bug this pairing was introduced to
   fix (confirmed on real Windows 95 hardware: a silent crash, access
   violation not a DebugCheck, no error.log, on the very first 3D frame
   after a level/quest load -- and confirmed to reproduce even at the
   *lowest* detail setting, ruling out a buffer-too-big explanation and
   pointing squarely at a stride mismatch instead). Was temporarily
   lowered to 3 chasing a stack/.bss-growth theory before the real stride
   bug was found; restored to match HIGHRES_MAX_SCALE now that the actual
   cause is fixed at the allocation site instead. */
#ifndef VIEW3D_SCALE_MAX
#define VIEW3D_SCALE_MAX 6
#endif
#define MAX_VIEW3D_WIDTH  (320*VIEW3D_SCALE_MAX)
#define MAX_VIEW3D_HEIGHT (200*VIEW3D_SCALE_MAX)

/* Runtime "Level of Detail" (1..VIEW3D_SCALE_MAX): how many times larger
   than classic 320x200 the 3D view is actually rendered/composited at.
   Was a hardcoded compile-time constant (2); every existing VIEW3D_SCALE
   reference across the codebase (coordinate-space conversion between
   logical 320-space and scaled pixel space, in 3D_VIEW.C, VIEW.C,
   CONTROL.C, CLIENT.C, BANNER.C, INVENTOR.C) now reads this variable
   instead, via the macro just below -- no call site needed to change. */
extern T_word16 G_view3dActiveScale ;
#define VIEW3D_SCALE G_view3dActiveScale

/* Sets the active detail level, clamped to 1..VIEW3D_SCALE_MAX. Must be
   called before ViewInitialize() (which is what actually consumes it,
   via View3dSetSize) -- changing it afterward has no effect until the
   view is torn down and reinitialized. */
T_void View3dSetActiveScale(T_word16 scale) ;

#define VIEW3D_PASSABLE_BIT 0x1000

extern T_sword16 VIEW3D_WIDTH ;
extern T_sword16 VIEW3D_HEIGHT ;
extern T_sword16 VIEW3D_HALF_WIDTH ;
extern T_sword16 VIEW3D_HALF_HEIGHT ;
extern T_sword16 VIEW3D_CLIP_LEFT ;
extern T_sword16 VIEW3D_CLIP_RIGHT ;

extern T_byte8 P_shadeIndex[16384] ;

T_void View3dInitialize(T_void) ;

T_void View3dSetView(
           T_sword16 x,
           T_sword16 y,
           T_sword32 height,
           T_word16 angle) ;

T_void View3dGetView(
           T_sword16 *p_x,
           T_sword16 *p_y,
           T_sword32 *p_height,
           T_word16 *p_angle) ;

T_void View3dDrawView(T_void) ;

T_void View3dDisplayView(T_void) ;

/* CalculateDistance, CalculateEstimateDistance, IFindSectorNum,
   IOnRightOfLine and the View3dFindSectorNum macro now live in 3D_GEO.H
   (included above), shared by the renderer and collision. */

T_void View3dSetSize(T_word16 width, T_word16 height) ;

T_void View3dClipCenter(T_word16 centerWidth) ;

T_3dObject *View3dAllocateObject(T_void) ;

T_void View3dFreeObject(T_3dObject *p_obj) ;

T_void View3dSetHeight(T_sword32 height) ;

T_word16 View3dGetObjectAtColumn(
             T_word16 objPos,
             T_3dObject **p_obj,
             T_word16 column) ;

T_word16 View3dGetObjectAtXY(
             T_word16 objPos,
             T_3dObject **p_obj,
             T_word16 x,
             T_word16 y) ;

T_sword32 View3dGetUpDownAngle(T_void) ;

T_void View3dSetUpDownAngle(T_sword32 alpha) ;

T_void View3dAddObject(T_3dObject *p_obj) ;

T_void View3dRemoveObject(T_3dObject *p_obj) ;

T_void View3dUpdateSectorLightAnimation(T_void) ;

#define View3dGetSectorEnterSound(sector) \
            (G_3dSectorInfoArray[sector].enterSound)

#define View3dGetSectorEnterSoundRadius(sector) \
            (G_3dSectorInfoArray[sector].enterSoundRadius)

#define LINE_IS_IMPASSIBLE           0x0001
#define LINE_IS_CREATURE_IMPASSIBLE  0x0002
#define LINE_IS_TWO_SIDED            0x0004
#define LINE_IS_TRANSLUCENT          0x0020
#define LINE_IS_ALWAYS_SOLID         0x0040
#define LINE_IS_INVISIBLE            0x0080
#define LINE_IS_AUTOMAPPED           0x0100
#define LINE_HAS_BEEN_SEEN           0x8000

T_word16 View3dFindClosestLine(T_sword16 x, T_sword16 y) ;

#ifdef COMPILE_OPTION_ALLOW_SHIFT_TEXTURES
T_void View3dTellMouseAt(T_sword16 x, T_sword16 y) ;

T_word16 View3dGetTextureSideNum(T_void) ;
#endif

T_word16 View3dGetSectorSide(T_word16 lineNum, T_sword16 x, T_sword16 y) ;

T_word16 View3dFindSide(T_sword16 x, T_sword16 y) ;

T_void View3dCheckObjectListEmpty(T_void) ;

T_void View3dFinish(T_void);

T_void View3dSetDarknessAdjustment(T_sbyte8 darkAdjust);

T_void View3dRemapSectors(T_void);

T_void View3dUnmapSectors(T_void);

/* TRUE-HIGHRES additions */
T_screen View3dGetOverlayScreen(T_void) ;
E_Boolean View3dHighResGrabFrameDrawn(T_void) ;


#endif

/****************************************************************************/
/*    END OF FILE:  3D_VIEW.H                                                 */
/****************************************************************************/
