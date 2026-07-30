/*-------------------------------------------------------------------------*
 * File:  PICS.C
 *-------------------------------------------------------------------------*/
/**
 * All graphics are stored in the PICS resource file.  This is the accessor
 * code to all those pictures.  Pictures can be locked as a picture or
 * locked as just a raw data file.
 *
 * @addtogroup PICS
 * @brief Picture Resource File
 * @see http://www.amuletsandarmor.com/AALicense.txt
 * @{
 *
 *<!-----------------------------------------------------------------------*/
#include "PICS.H"
#include "ENDIAN_AA.H"
#include <string.h>

static T_byte8 G_lastMissingPictureName[14] = "" ;

static E_Boolean IPictureNameIsPrintable(T_byte8 *name)
{
    T_word16 i ;

    if ((name == NULL) || (name[0] == '\0'))
        return FALSE ;

    for (i=0; i<13; i++) {
        if (name[i] == '\0')
            return TRUE ;
        if ((name[i] < ' ') || (name[i] > '~'))
            return FALSE ;
    }

    return TRUE ;
}

static T_void IPictureReportMissing(T_byte8 *name)
{
    if (IPictureNameIsPrintable(name) == FALSE)
        return ;

    if (strcmp((char *)G_lastMissingPictureName, (char *)name) != 0) {
        printf("Cannot find picture named '%s'\n", name) ;
        strncpy((char *)G_lastMissingPictureName, (char *)name, 13) ;
        G_lastMissingPictureName[13] = '\0' ;
    }
}

static T_resourceFile G_pictureResFile ;
static E_Boolean G_picturesActive = FALSE ;

/*
 * Keep exact lookup semantics first, then try basename-only as a
 * compatibility path for resource names embedded with directory
 * components in FRM files (e.g. "COLORIZE/COLORT01") that PICS.RES
 * itself stores as flat, undirectoried entries ("COLORT01"). Was
 * TARGET_UNIX-only; native Windows hit the exact same mismatch (real
 * Windows 95 hardware: DebugCheck(PictureExist("COLORIZE/COLORT01"))
 * failing and DebugFail() exit(3)-ing the client) -- the only reason it
 * went unnoticed on the mainline Windows release is that DebugCheck
 * compiles to a no-op under NDEBUG there, silently leaving colorize
 * tables unset rather than asserting. Not platform-specific, so no
 * longer gated behind an ifdef.
 */
static T_resource IPictureFindCompat(T_byte8 *name)
{
    T_resource found ;
    T_byte8 *p_base ;

    found = ResourceFind(G_pictureResFile, name) ;
    if (found != RESOURCE_BAD)
        return found ;

    p_base = (T_byte8 *)strrchr((char *)name, '/') ;
    if (p_base != NULL && p_base[1] != '\0')  {
        p_base++ ;
        found = ResourceFind(G_pictureResFile, p_base) ;
        if (found != RESOURCE_BAD)
            return found ;
    }

    /* Legacy map tokens may reference MARBGRAY, which is absent in PICS.RES. */
    if (strcmp((char *)name, "MARBGRAY") == 0)
        found = ResourceFind(G_pictureResFile, "MARBLE3") ;

    return found ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PicturesInitialize
 *-------------------------------------------------------------------------*/
/**
 *  PicturesInitialize opens up the picture database in preparation for
 *  all future picture locking and unlocking.
 *
 *<!-----------------------------------------------------------------------*/
T_void PicturesInitialize(T_void)
{
    DebugRoutine("PicturesInitialize") ;
    DebugCheck(G_picturesActive == FALSE) ;

    /* Open up the resource file for future accesses. */
    G_pictureResFile = ResourceOpen(PICTURE_RESOURCE_FILENAME) ;

    /* Note that we are now active. */
    G_picturesActive = TRUE ;

    DebugEnd() ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PicturesFinish
 *-------------------------------------------------------------------------*/
/**
 *  PicturesFinish is called when the pictures resource file is no
 *  longer needed (typically when exiting the program).  When this occurs,
 *  the resource file is closed out.
 *
 *<!-----------------------------------------------------------------------*/
T_void PicturesFinish(T_void)
{
    DebugRoutine("PicturesFinish") ;
    DebugCheck(G_picturesActive == TRUE) ;

    /* Close the already open resource file. */
    ResourceClose(G_pictureResFile) ;

    /* Note that we are no longer active. */
    G_picturesActive = FALSE ;

    DebugEnd() ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureLock
 *-------------------------------------------------------------------------*/
/**
 *  PictureLock locks a picture out of the picture database into memory.
 *
 *  @param name -- Name of picture to load
 *  @param res -- Pointer to resource to record where
 *      the picture came from.  Is used
 *      by PictureUnlock.
 *
 *  @return Pointer to picture data.
 *
 *<!-----------------------------------------------------------------------*/
typedef struct {
    T_byte8 resID[4] ;         /* Should contain "ReS"+'\0' id */
    T_byte8 p_resourceName[14] ; /* Case sensitive, 13 characters + '\0' */
    T_word32 fileOffset ;
    T_word32 size ;              /* Size in bytes. */
    T_word16 lockCount ;         /* 0 = unlocked. */
    T_byte8 resourceType ;
    T_byte8 *p_data ;
    T_resourceFile resourceFile ;      /* Resource file this is from. */
    T_void *ownerDir ;        /* Locked in owner directory (or NULL) */
} T_resourceEntry ;

/* Every T_bitmap-formatted picture resource starts with a little-endian
   sizex/sizey T_word16 pair (see Include/GRAPHICS.H's T_bitmap and
   PictureGetXYSize below).  ResourceIsFreshLoad() must be checked
   *before* the ResourceLock() call, since ResourceLock() flips the
   resource to "in memory" as a side effect of loading; the swap must
   then be applied exactly once, to what ResourceLock() returns, or a
   later cache-hit lock would flip an already-fixed-up header back to
   wrong-endian. */
static T_void IPictureSwapHeader(T_byte8 *where)
{
    /* where is a raw pointer into a resource file's in-memory image, with
       no alignment guarantee (resource entries are packed back-to-back
       exactly as they appear on disk) -- EndianLE16P goes through memcpy
       rather than a direct T_word16* dereference so this doesn't fault on
       strict-alignment targets (confirmed crashing here on the SGI O2/
       MIPS port). */
    EndianLE16P(where) ;                        /* sizex */
    EndianLE16P(where + sizeof(T_word16)) ;      /* sizey */
}

T_byte8 *PictureLock(T_byte8 *name, T_resource *res)
{
    T_resource found ;
    T_byte8 *where = NULL ;

    DebugRoutine("PictureLock") ;
    DebugCheck(name != NULL) ;
    DebugCheck(res != NULL) ;
    DebugCheck(G_picturesActive == TRUE) ;

    /* Look up the picture in the index. */
//printf("> %s\n", name) ;
    found = IPictureFindCompat(name) ;
//printf("Locking pic %s (%p) for %s\n", name, found, DebugGetCallerName()) ;
    if (found == RESOURCE_BAD)  {
#ifndef NDEBUG
        IPictureReportMissing(name) ;
#endif
        found = ResourceFind(G_pictureResFile, "DRK42") ;
    }

DebugCheck(found != RESOURCE_BAD) ;

    /* If we found it, we need to lock it in memory. */
    if (found != RESOURCE_BAD)  {
        E_Boolean freshLoad = ResourceIsFreshLoad(found) ;
        where = ResourceLock(found) ;
        if (freshLoad)
            IPictureSwapHeader(where) ;
    }

    /* Record the resource we got the data from.  Needed for unlocking. */
    *res = found ;

    DebugEnd() ;

    /* Return a pointer to the data part. */
    return (where+(2*sizeof(T_word16))) ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureLockData
 *-------------------------------------------------------------------------*/
/**
 *  PictureLockData is the same as PictureLock except that it is used
 *  to lock non-pictures in the picture database.  Therefore, all of the
 *  data is available (pictures skip some bytes at the beginning).
 *
 *  @param name -- Name of resource to load
 *  @param res -- Pointer to resource to record where
 *      the resource came from.  Is used
 *      by PictureUnlock.
 *
 *  @return Pointer to picture data.
 *
 *<!-----------------------------------------------------------------------*/
T_byte8 *PictureLockData(T_byte8 *name, T_resource *res)
{
    T_resource found ;
    T_byte8 *where = NULL ;

    DebugRoutine("PictureLockData") ;
    DebugCheck(name != NULL) ;
    DebugCheck(res != NULL) ;
    DebugCheck(G_picturesActive == TRUE) ;

    /* Look up the picture in the index. */
    found = IPictureFindCompat(name) ;
#ifndef NDEBUG
    if (found == RESOURCE_BAD)  {
        IPictureReportMissing(name) ;
        found = ResourceFind(G_pictureResFile, "DRK42") ;
    }
#endif

DebugCheck(found != RESOURCE_BAD) ;
    /* If we found it, we need to lock it in memory. */
    if (found != RESOURCE_BAD)
        where = ResourceLock(found) ;

    /* Record the resource we got the data from.  Needed for unlocking. */
    *res = found ;

    DebugEnd() ;

    /* Return a pointer to the data part. */
    return where ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureLockDataAsBitmap
 *-------------------------------------------------------------------------*/
/**
 *  Same as PictureLockData, but for resources that are themselves a full
 *  T_bitmap (sizex/sizey header + pixel data) rather than a pointer past
 *  it (contrast with PictureLock/PictureToBitmap). Fixes up the
 *  sizex/sizey header's byte order exactly once per resource, matching
 *  PictureLock's own handling.
 *
 *  @param name -- Name of resource to load
 *  @param res -- Pointer to resource to record where
 *      the resource came from.  Is used
 *      by PictureUnlock.
 *
 *  @return Pointer to bitmap data.
 *
 *<!-----------------------------------------------------------------------*/
T_bitmap *PictureLockDataAsBitmap(T_byte8 *name, T_resource *res)
{
    T_resource found ;
    T_byte8 *where = NULL ;
    E_Boolean freshLoad ;

    DebugRoutine("PictureLockDataAsBitmap") ;
    DebugCheck(name != NULL) ;
    DebugCheck(res != NULL) ;
    DebugCheck(G_picturesActive == TRUE) ;

    /* Same lookup as PictureLockData -- duplicated (rather than calling
       PictureLockData and checking freshness after) because
       ResourceIsFreshLoad() must be read before ResourceLock() mutates
       the resource's state as a side effect of loading it. */
    found = IPictureFindCompat(name) ;
#ifndef NDEBUG
    if (found == RESOURCE_BAD)  {
        IPictureReportMissing(name) ;
        found = ResourceFind(G_pictureResFile, "DRK42") ;
    }
#endif

    DebugCheck(found != RESOURCE_BAD) ;
    if (found != RESOURCE_BAD)  {
        freshLoad = ResourceIsFreshLoad(found) ;
        where = ResourceLock(found) ;
        if (freshLoad)
            IPictureSwapHeader(where) ;
    }

    *res = found ;

    DebugEnd() ;

    return (T_bitmap *)where ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureUnlock
 *-------------------------------------------------------------------------*/
/**
 *  PictureUnlock removes a picture that was in memory.
 *
 *  @param res -- Resource to the picture.
 *
 *<!-----------------------------------------------------------------------*/
T_void PictureUnlock(T_resource res)
{
    DebugRoutine("PictureUnlock") ;
    DebugCheck(res != RESOURCE_BAD) ;
    DebugCheck(G_picturesActive == TRUE) ;

//printf("Unlock %s (%p) by %s\n", ((T_resourceEntry *)res)->p_resourceName, res, DebugGetCallerName()) ;
    /* All we need to do at this point is unlock the resource. */
    ResourceUnlock(res) ;

    DebugEnd() ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureExist
 *-------------------------------------------------------------------------*/
/**
 *  PictureExist determines if a the given name corresponds to a picture
 *  in the picture resource file.
 *
 *  @param name -- Name of picture to check for existance
 *
 *  @return TRUE = found, FALSE = not found
 *
 *<!-----------------------------------------------------------------------*/
E_Boolean PictureExist(T_byte8 *name)
{
    E_Boolean picExist ;
    T_resource res ;

    DebugRoutine("PictureExist") ;
    DebugCheck(name != NULL) ;
    DebugCheck(G_picturesActive == TRUE) ;

    /* Look up the picture in the index. */
    res = IPictureFindCompat(name) ;

    /* Check to see if it is a good resource. */
    picExist = (res == RESOURCE_BAD)?FALSE:TRUE ;

    /* Don't hold onto it, just wanted to know if it was there. */
    if (picExist)
        ResourceUnfind(res) ;

    DebugEnd() ;

    /* Return the boolean telling if the picture exists. */
    return (picExist) ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureGetXYSize
 *-------------------------------------------------------------------------*/
/**
 *  PictureGetXY size gets the size of the picture and returns it by
 *  reference.
 *
 *  @param p_picture -- Pointer to the picture to get size of
 *  @param sizeX -- Get the size of the picture in the X
 *  @param sizeY -- Get the size of the picture in the Y
 *
 *<!-----------------------------------------------------------------------*/
T_void PictureGetXYSize(T_void *p_picture, T_word16 *sizeX, T_word16 *sizeY)
{
    T_word16 *p_data ;

    DebugRoutine("PictureGetXYSize") ;
    DebugCheck(p_picture != NULL) ;
    DebugCheck(sizeX != NULL) ;
    DebugCheck(sizeY != NULL) ;

    /* Convert to 16 bit word pointer. */
    p_data = (T_word16 *)p_picture ;

    /* Get data behind this point. IPictureSwapHeader already corrected
       this header's byte order exactly once at load time, so it's
       already native here -- no EndianLE16 (that would double-swap on
       big-endian targets). AlignedGetW16 (ENDIAN_AA.H) rather than a
       direct T_word16* dereference or memcpy: nothing guarantees
       p_picture-4 is 2-byte-aligned, and both an unaligned dereference
       and a small fixed-size memcpy have been confirmed to fault with
       SIGBUS on strict-alignment targets (e.g. the SGI O2/MIPS port;
       the latter because GCC 4.5.2 at -O2 pattern-matches a memcpy of
       a fixed 2-byte size into a single plain `lh`). */
    *sizeX = AlignedGetW16(p_data - 2) ;
    *sizeY = AlignedGetW16(p_data - 1) ;

    DebugEnd() ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureFind
 *-------------------------------------------------------------------------*/
/**
 *  PictureFind finds the corresponding resource handle for the given
 *  resource name.
 *
 *  @param name -- Name of resource to find.
 *
 *  @return Corresponding resource handle or
 *      RESOURCE_BAD
 *
 *<!-----------------------------------------------------------------------*/
T_resource PictureFind(T_byte8 *name)
{
    T_resource res ;

    DebugRoutine("PictureFind") ;
    DebugCheck(name != NULL) ;

    /* Look up the picture in the index. */
    res = IPictureFindCompat(name) ;

    DebugEnd() ;

    return res ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureUnfind
 *-------------------------------------------------------------------------*/
/**
 *  PictureUnfind removes all references to the given resource picture.
 *
 *  @param res -- Resource to unfind
 *
 *<!-----------------------------------------------------------------------*/
T_void PictureUnfind(T_resource res)
{
    DebugRoutine("PictureUnfind") ;
    DebugCheck(res != RESOURCE_BAD) ;

    /* Get rid of that picture. */
    ResourceUnfind(res) ;

    DebugEnd() ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureUnlockAndUnfind
 *-------------------------------------------------------------------------*/
/**
 *  PictureUnlockAndUnfind does a PictureUnlock and then a PictureUnfind.
 *
 *  @param res -- Resource to unlock and unfind
 *
 *<!-----------------------------------------------------------------------*/
T_void PictureUnlockAndUnfind(T_resource res)
{
    DebugRoutine("PictureUnlockAndUnfind") ;
    DebugCheck(res != RESOURCE_BAD) ;

    /* Get rid of that picture. */
    ResourceUnlock(res) ;
    ResourceUnfind(res) ;

    DebugEnd() ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureLockQuick
 *-------------------------------------------------------------------------*/
/**
 *  PictureLockQuick is a simpler lock routine for a picture since it
 *  takes the resource handle of an already found picture (from
 *  PictureFind).
 *
 *  NOTE: 
 *  Do NOT call GrDrawBitmap (or similar) with the returned pointer
 *  from this routine.  Use PictureToBitmap to get the correct pointer.
 *
 *  @param res -- Resource of picture to lock.
 *
 *  @return Pointer to resource data.
 *
 *<!-----------------------------------------------------------------------*/
T_byte8 *PictureLockQuick(T_resource res)
{
    T_byte8 *p_where = NULL ;

    DebugRoutine("PictureLockQuick") ;
    DebugCheck(res != RESOURCE_BAD) ;

    /* If we found it, we need to lock it in memory. */
    if (res != RESOURCE_BAD)  {
        E_Boolean freshLoad = ResourceIsFreshLoad(res) ;
        p_where = ResourceLock(res) ;
        if (freshLoad)
            IPictureSwapHeader(p_where) ;
    }

    DebugEnd() ;

    /* Return a pointer to the data part. */
    return (p_where+(2*sizeof(T_word16))) ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureToBitmap
 *-------------------------------------------------------------------------*/
/**
 *  PictureToBitmap converts a picture pointer (from PictureLock) into
 *  a corresponding bitmap pointer for use by the graphic drawing
 *  routines.
 *
 *  NOTE: 
 *  Make sure you ONLY pass a pointer from PictureLock or PictureLockQuick
 *
 *  @param pic -- Pointer to picture
 *
 *  @return Pointer to bitmap
 *
 *<!-----------------------------------------------------------------------*/
T_bitmap *PictureToBitmap(T_byte8 *pic)
{
    T_bitmap *p_bitmap ;

    DebugRoutine("PictureToBitmap") ;
    DebugCheck(pic != NULL) ;

    /*
     * pic points at pixel data (after sizex/sizey). Always step back
     * by exactly the 4-byte header, not sizeof(T_bitmap), which differs
     * between Watcom and GCC/Clang builds.
     */
    p_bitmap = (T_bitmap *)(pic - (2*sizeof(T_word16))) ;

    DebugEnd() ;

    return p_bitmap ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureGetWidth
 *-------------------------------------------------------------------------*/
/**
 *  PictureGetWidth returns the pictures with.
 *
 *  @param p_picture -- Pointer to the picture to get size of
 *
 *  @return picture width
 *
 *<!-----------------------------------------------------------------------*/
T_word16 PictureGetWidth(T_void *p_picture)
{
    T_word16 width ;
    T_word16 *p_data ;

    DebugRoutine("PictureGetWidth") ;
    DebugCheck(p_picture != NULL) ;

    /* Convert to 16 bit word pointer. */
    p_data = (T_word16 *)p_picture ;

    /* Get data behind this point. Already native (see PictureGetXYSize);
       AlignedGetW16 since p_data-1 isn't guaranteed 2-byte aligned (and
       a memcpy of this fixed size isn't a safe substitute for a direct
       dereference here either -- see PictureGetXYSize's comment). */
    width = AlignedGetW16(p_data - 1) ;

    DebugEnd() ;

    return width ;
}


/*-------------------------------------------------------------------------*
 * Routine:  PictureGetHeight
 *-------------------------------------------------------------------------*/
/**
 *  PictureGetHeight returns the picture height
 *
 *  @param p_picture -- Pointer to the picture to get size of
 *
 *  @return picture height
 *
 *<!-----------------------------------------------------------------------*/
T_word16 PictureGetHeight(T_void *p_picture)
{
    T_word16 height ;
    T_word16 *p_data ;

    DebugRoutine("PictureGetHeight") ;
    DebugCheck(p_picture != NULL) ;

    /* Convert to 16 bit word pointer. */
    p_data = (T_word16 *)p_picture ;

    /* Get data behind this point. Already native (see PictureGetXYSize);
       AlignedGetW16 since p_data-2 isn't guaranteed 2-byte aligned (and
       a memcpy of this fixed size isn't a safe substitute for a direct
       dereference here either -- see PictureGetXYSize's comment). */
    height = AlignedGetW16(p_data - 2) ;

    DebugEnd() ;

    return height ;
}

#ifndef NDEBUG
/*-------------------------------------------------------------------------*
 * Routine:  PicturePrint
 *-------------------------------------------------------------------------*/
/**
 *  PicturePrint prints out the structure related to the given picture
 *  to the given output.
 *
 *  @param fp -- File to output picture info
 *  @param p_pic -- Picture to print
 *
 *<!-----------------------------------------------------------------------*/
T_void PicturePrint(FILE *fp, T_void *p_pic)
{
    T_word16 *p_data ;
    T_word16 width, height ;

    DebugRoutine("PicturePrint") ;

    p_data = p_pic ;

    /* Already native (see PictureGetXYSize); AlignedGetW16 since
       p_data-1/-2 aren't guaranteed 2-byte aligned (and a memcpy of
       this fixed size isn't a safe substitute for a direct dereference
       here either -- see PictureGetXYSize's comment). */
    width = AlignedGetW16(p_data - 1) ;
    height = AlignedGetW16(p_data - 2) ;

    fprintf(fp, "Picture: %p\n", p_pic) ;
    fprintf(fp, "  width: %d\n", width) ;
    fprintf(fp, "  heigh: %d\n", height) ;
    fflush(fp) ;

    DebugEnd() ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PicturesDump
 *-------------------------------------------------------------------------*/
/**
 *  PicturesDump outputs the picture index file.
 *
 *<!-----------------------------------------------------------------------*/
T_void PicturesDump(T_void)
{
    DebugRoutine("PicturesDump") ;

    ResourceDumpIndex(G_pictureResFile) ;

    DebugEnd() ;
}

T_void PictureCheck(T_void *p_picture)
{
#if 0
    T_byte8 *p_where ;

    DebugRoutine("PictureCheck") ;
    DebugCheck(p_picture != NULL) ;

    p_where = (((T_byte8 *)p_picture)-(2*sizeof(T_word16))) ;

    ResourceCheckByPtr(p_where) ;

    DebugEnd() ;
#endif
}

#endif

/*-------------------------------------------------------------------------*
 * Routine:  PictureLockDataQuick
 *-------------------------------------------------------------------------*/
/**
 *  PictureLockQuick is a simpler lock routine for a picture since it
 *  takes the resource handle of an already found picture (from
 *  PictureFind).
 *
 *  NOTE: 
 *  Do NOT call GrDrawBitmap (or similar) with the returned pointer
 *  from this routine.  Use PictureToBitmap to get the correct pointer.
 *
 *  @param res -- Resource of picture to lock.
 *
 *  @return Pointer to resource data.
 *
 *<!-----------------------------------------------------------------------*/
T_byte8 *PictureLockDataQuick(T_resource res)
{
    T_byte8 *p_where ;

    DebugRoutine("PictureDataLockQuick") ;
    DebugCheck(res != RESOURCE_BAD) ;

    /* If we found it, we need to lock it in memory. */
    if (res != RESOURCE_BAD)
        p_where = ResourceLock(res) ;

    DebugEnd() ;

    /* Return a pointer to the data part. */
    return (p_where) ;
}

/*-------------------------------------------------------------------------*
 * Routine:  PictureGetName
 *-------------------------------------------------------------------------*/
/**
 *  PictureGetName returns a pointer to the picture's stub name (without
 *  sub-directory info.)
 *
 *  @param p_picture -- Pointer to the picture
 *
 *  @return Found name
 *
 *<!-----------------------------------------------------------------------*/
T_byte8 *PictureGetName(T_void *p_picture)
{
    T_byte8 *p_name ;

    DebugRoutine("PictureGetName") ;
    DebugCheck(p_picture != NULL) ;

    /* Convert the picture into its basic data form */
    /* and the resource name. */
    p_name = ResourceGetName(PictureToBitmap((T_byte8 *)p_picture)) ;

    DebugEnd() ;

    return p_name ;
}

/** @} */
/*-------------------------------------------------------------------------*
 * End of File:  PICS.C
 *-------------------------------------------------------------------------*/
