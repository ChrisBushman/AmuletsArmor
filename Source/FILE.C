/*-------------------------------------------------------------------------*
 * File:  FILE.C
 *-------------------------------------------------------------------------*/
/**
 * Routines for loading/saving files.
 *
 * @addtogroup FILE
 * @brief File IO
 * @see http://www.amuletsandarmor.com/AALicense.txt
 * @{
 *
 *<!-----------------------------------------------------------------------*/
#include <fcntl.h>
#include <sys/stat.h>
#if !defined(macintosh)
#include <sys/types.h>          /* classic Mac MSL has no <sys/types.h> */
#endif
/* Classic Mac MSL's <sys/stat.h> uses only the POSIX S_IRWXU-style mode
   bits, not the old BSD/DOS S_IREAD/S_IWRITE spellings AA passes to open(). */
#ifndef S_IREAD
#define S_IREAD  0000400
#endif
#ifndef S_IWRITE
#define S_IWRITE 0000200
#endif
/* <io.h> is a DOS/Windows-only CRT header (_dos_findfirst, filelength,
   ...); Include/UNIXIO_SHIM.H (formerly named io.h -- renamed to stop
   it silently shadowing the real system header, the same class of bug
   as the DIRECT.H/WINDIRECT.H collision) is the Unix substitute. */
#ifdef TARGET_UNIX
#include "UNIXIO_SHIM.H"
#else
#include <io.h>
#endif
#include "FILE.H"
#include "MEMORY.H"
#include "SOUND.H"

#ifdef TARGET_UNIX
#include <dirent.h>
#include <strings.h>
#endif

#define MAX_FILES 20

/* Number of files currently open: */
static T_word16 G_numberOpenFiles = 0 ;

#ifdef TARGET_UNIX
/* Every on-disk resource/asset filename in this codebase (SOUNDS.RES,
   picture names, level files, ...) was authored assuming a case-
   insensitive filesystem -- true of the original DOS/Windows target and
   of Mac OS X's default HFS+/APFS, but not of IRIX's, so a request like
   "sounds.res" fails outright when the real file is SOUNDS.RES. Rather
   than fix each mismatched name as it's discovered (there's no reason to
   expect this is the only one), fall back to a case-insensitive directory
   scan whenever the exact-case open fails on a case-sensitive filesystem.
   Read-only, deliberately: a case-insensitive fallback for
   write/append/create would risk silently writing to an unrelated
   existing file instead of the one actually requested. */
static T_file IFileOpenCaseInsensitive(T_byte8 *p_filename, T_word32 openMode)
{
    char dirPart[512] ;
    char pathCopy[512] ;
    const char *baseFilename ;
    char *lastSlash ;
    DIR *dir ;
    struct dirent *entry ;
    char candidatePath[1024] ;
    T_file file = FILE_BAD ;

    strncpy(pathCopy, (char *)p_filename, sizeof(pathCopy)-1) ;
    pathCopy[sizeof(pathCopy)-1] = 0 ;

    lastSlash = strrchr(pathCopy, '/') ;
    if (lastSlash != NULL)  {
        *lastSlash = 0 ;
        strncpy(dirPart, pathCopy, sizeof(dirPart)-1) ;
        dirPart[sizeof(dirPart)-1] = 0 ;
        baseFilename = lastSlash + 1 ;
    } else {
        strcpy(dirPart, ".") ;
        baseFilename = (char *)p_filename ;
    }

    dir = opendir(dirPart) ;
    if (dir == NULL)
        return FILE_BAD ;

    while ((entry = readdir(dir)) != NULL)  {
        if (strcasecmp(entry->d_name, baseFilename) == 0)  {
            snprintf(candidatePath, sizeof(candidatePath), "%s/%s",
                     dirPart, entry->d_name) ;
            file = open(candidatePath, openMode, S_IREAD|S_IWRITE) ;
            break ;
        }
    }
    closedir(dir) ;

    return file ;
}
#endif

/*-------------------------------------------------------------------------*
 * Routine:  FileOpen
 *-------------------------------------------------------------------------*/
/**
 *  Open a file for reading, writing, appending, etc.  All files are
 *  created unless you request to read.  A file handle is returned for
 *  all future accesses.  Note that a maximum of MAX_FILES is allowed to
 *  be opened at a time.
 *
 *  NOTE: 
 *  Obviously I can't check to see if someone does something stupid to
 *  a file they shouldn't be touching, but there is always the possibility.
 *
 *  @param p_filename -- pointer to the string that holds
 *      the real filename.  Note that we
 *      don't have any particular format
 *      in mind.  A path name can be included.
 *  @param mode -- Different read/write modes.  See .H
 *
 *  @return file handle for all future accesses.
 *
 *<!-----------------------------------------------------------------------*/
T_file FileOpen(T_byte8 *p_filename, E_fileMode mode)
{
    T_file file ;
    T_byte8 *p_open ;
    static T_word32 fileOpenModes[4] = {
         O_RDONLY|O_BINARY,
         O_WRONLY|O_CREAT|O_BINARY,
         O_RDWR|O_APPEND|O_CREAT|O_BINARY,
         O_RDWR|O_CREAT|O_BINARY
    } ;
#if defined(macintosh)
    /* Classic Mac OS uses ':' as its path separator; '/' is an ordinary
       filename character.  The game opens loose files in subdirectories with
       Unix-style names like "MAPDESC/DES00000", which the Mac file system
       would otherwise try to open as one literal (nonexistent) file -- this is
       why e.g. the guild's adventure list (MAPDESC/DES%05d) came up empty.
       Translate to a Mac relative path ":MAPDESC:DES00000" (a leading ':'
       means "relative to the current directory").  Root-level names with no
       '/' are passed through unchanged. */
    T_byte8 macPath[256] ;
#endif

    DebugRoutine("FileOpen") ;
    DebugCheck(p_filename != NULL) ;
    DebugCheck(mode < FILE_MODE_UNKNOWN) ;
    DebugCheck(G_numberOpenFiles < MAX_FILES) ;

    p_open = p_filename ;
#if defined(macintosh)
    if (strchr((char *)p_filename, '/') != NULL) {
        T_word16 i = 0, j = 0 ;
        macPath[j++] = ':' ;
        while (p_filename[i] != '\0' && j < (T_word16)(sizeof(macPath) - 1)) {
            macPath[j++] = (p_filename[i] == '/') ? ':' : p_filename[i] ;
            i++ ;
        }
        macPath[j] = '\0' ;
        p_open = macPath ;
    }
#endif

    file = open(p_open, fileOpenModes[mode], S_IREAD|S_IWRITE) ;
#ifdef TARGET_UNIX
    if ((file == FILE_BAD) && (mode == FILE_MODE_READ))
        file = IFileOpenCaseInsensitive(p_open, fileOpenModes[mode]) ;
#endif
    if (file != FILE_BAD)
        G_numberOpenFiles++ ;

    DebugEnd() ;

    return file ;
}

/*-------------------------------------------------------------------------*
 * Routine:  FileClose
 *-------------------------------------------------------------------------*/
/**
 *  Close a previously opened file.  Nothing really special here.
 *
 *  @param file -- file to close.
 *
 *<!-----------------------------------------------------------------------*/
T_void FileClose(T_file file)
{
    DebugRoutine("FileClose") ;
    DebugCheck(file != FILE_BAD) ;

    close(file) ;

    /* Decrement the number of open files. */
    G_numberOpenFiles-- ;

    DebugEnd();
}

/*-------------------------------------------------------------------------*
 * Routine:  FileSeek
 *-------------------------------------------------------------------------*/
/**
 *  Perhaps one of the most useful file routines is the file seek
 *  function.  Just provide the file to seek into and you will be
 *  position at the point you requested.
 *
 *  NOTE: 
 *  Doesn't check to see if you stayed inside the file bounds.  This is
 *  not really a problem for writing, but can be a big problem for reading.
 *
 *  @param file -- File to seek into
 *  @param position -- position to seek from the beginning.
 *      A position of 0 is the very first
 *      byte.
 *
 *<!-----------------------------------------------------------------------*/
/* All seeks are from the beginning of the file. */
T_void FileSeek(T_file file, T_word32 position)
{
    DebugRoutine("FileSeek") ;
    DebugCheck(file != FILE_BAD) ;

    lseek(file, position, SEEK_SET) ;

    DebugEnd() ;
}

/*-------------------------------------------------------------------------*
 * Routine:  FileRead
 *-------------------------------------------------------------------------*/
/**
 *  FileRead is used to retrieve bytes from a file from the current
 *  file position.
 *
 *  NOTE: 
 *  There is no way to check if the buffer pointer that is passed has
 *  enough room for the data is about to be read and may overwrite a bunch
 *  of stuff that is valuable (including the OS).
 *
 *  @param file -- handle of file to read from.
 *  @param p_buffer -- Pointer to buffer to read bytes into.
 *  @param size -- number of bytes to read.
 *
 *  @return number of bytes read, or -1 for error.
 *
 *<!-----------------------------------------------------------------------*/
T_sword32 FileRead(T_file file, T_void *p_buffer, T_word32 size)
{
    T_sword32 result ;

    DebugRoutine("FileRead") ;
    DebugCheck(file != FILE_BAD) ;
//    DebugCheck(size > 0) ;
    DebugCheck(p_buffer != NULL) ;

    SoundUpdateOften() ;
    /* Loop to handle short reads.  POSIX read() -- and in particular the
       classic Mac MSL implementation -- is permitted to return fewer bytes
       than requested for a large request.  The resource system reads whole
       index tables and data blocks in one call and assumes the full count,
       so a short read here silently truncates the .RES index and makes every
       ResourceFind() fail.  Keep reading until we have all bytes, hit EOF, or
       error. */
    {
        T_byte8 *p_pos = (T_byte8 *)p_buffer ;
        T_word32 remaining = size ;
        T_sword32 n ;

        result = 0 ;
        while (remaining > 0) {
            n = read(file, p_pos, remaining) ;
            if (n <= 0) {
                if (result == 0)
                    result = n ;   /* propagate EOF(0)/error(<0) on first read */
                break ;
            }
            p_pos += n ;
            remaining -= (T_word32)n ;
            result += n ;
        }
    }
    SoundUpdateOften() ;

    DebugEnd() ;

    return result ;
}

/*-------------------------------------------------------------------------*
 * Routine:  FileWrite
 *-------------------------------------------------------------------------*/
/**
 *  Use FileWrite to store bytes at the current file position.  When the
 *  writing is done, the current file position will be at the next byte
 *  after all of the writing.
 *
 *  NOTE: 
 *  This routine doesn't check to see if we have a file handle that is
 *  for writing.  You could get some weird errors if this happens.
 *
 *  @param file -- handle of file to write to.
 *  @param p_buffer -- Pointer to buffer to write bytes from.
 *  @param size -- number of bytes to write.
 *
 *  @return number of bytes written, or else -1.
 *
 *<!-----------------------------------------------------------------------*/
T_sword32 FileWrite(T_file file, T_void *p_buffer, T_word32 size)
{
    T_sword32 result ;

    DebugRoutine("FileWrite") ;
    DebugCheck(file != FILE_BAD) ;
    DebugCheck(size > 0) ;
    DebugCheck(p_buffer != NULL) ;

    result = write(file, p_buffer, size) ;

    DebugEnd() ;

    return result ;
}

/*-------------------------------------------------------------------------*
 * Routine:  FileLoad
 *-------------------------------------------------------------------------*/
/**
 *  FileLoad allocates and reads in a file in one swipe so that the
 *  calling routine can just use the file like a memory allocation.
 *
 *  @param p_filename -- File to load
 *  @param p_size -- Indirect reference to the size of the
 *      file.
 *
 *<!-----------------------------------------------------------------------*/
T_void *FileLoad(T_byte8 *p_filename, T_word32 *p_size)
{
    T_byte8 *p_data ;
    T_file file ;

    DebugRoutine("FileLoad") ;
    DebugCheck(p_filename != NULL) ;
    DebugCheck(p_size != NULL) ;

    /* See how big the file is so we know how much memory to allocate. */
    *p_size = FileGetSize(p_filename) ;
#ifdef COMPILE_OPTION_FILE_OUTPUT
printf("!A 1 file_%s\n", p_filename) ;
printf("!A 1 file_r_%s\n", DebugGetCallerName()) ;
#endif
    if (*p_size)  {
        /* Allocate the memory for the file. */
        p_data = MemAlloc(*p_size) ;

        DebugCheck(p_data != NULL) ;

        /* Make sure we got the memory. */
        if (p_data != NULL)  {
            /* If memory was allocated, read in the file into this memory. */
            file = FileOpen(p_filename, FILE_MODE_READ) ;
            FileRead(file, p_data, *p_size) ;
            FileClose(file) ;
        } else {
            /* If memory was not allocated, return with a zero length. */
            *p_size = 0 ;
        }
    } else {
        *p_size = 0 ;
        p_data = NULL ;
    }

    DebugEnd() ;

    /* Return the pointer to the data. */
    return p_data ;
}

/*-------------------------------------------------------------------------*
 * Routine:  FileGetSize
 *-------------------------------------------------------------------------*/
/**
 *  FileGetSize looks at a given file name and returns the size of that
 *  file.
 *
 *  NOTE:
 *  This is the WATCOM C/C++ v10.0 specific version.
 *
 *  @param p_filename -- File to get size of
 *
 *  @return Size of file
 *
 *<!-----------------------------------------------------------------------*/
T_word32 FileGetSize(T_byte8 *p_filename)
{
    T_word32 size ;
#if defined(TARGET_UNIX)
    /* Not a raw fopen() here: FileExist()/FileOpen() both resolve a
       case-mismatched name via IFileOpenCaseInsensitive on this
       case-sensitive filesystem, but a bare fopen() (the WIN32 branch
       below, which this codebase's -DWIN32=1 compatibility define also
       takes on this Unix target) does not -- confirmed causing
       FileExist() to report TRUE while FileGetSize() silently returned
       0 for the exact same mismatched name, which then made FileLoad()
       hand back a NULL buffer that was stored and only crashed much
       later, in an unrelated-looking MemFree(NULL). Going through our
       own FileOpen()/FileSeek()-equivalent keeps this in sync with
       FileExist(). */
    T_file file ;

    DebugRoutine("FileGetSize") ;

    file = FileOpen(p_filename, FILE_MODE_READ) ;
    if (file != FILE_BAD)  {
        size = (T_word32)lseek(file, 0, SEEK_END) ;
        FileClose(file) ;
    } else {
        size = 0 ;
    }

    DebugEnd() ;
#elif defined(WIN32)
    FILE *fp;

    DebugRoutine("FileGetSize");
    fp = fopen(p_filename, "rb");
    if (fp) {
        size = filelength(fileno(fp));
        fclose(fp);
    } else {
        size = 0;
    }
    DebugEnd() ;
#else
    struct find_t fileinfo ;

    DebugRoutine("FileGetSize") ;

    /* Get information about the file. */
    if (_dos_findfirst(p_filename, _A_NORMAL, &fileinfo) == 0)  {
        /* If we found the file, return the file size. */
        size = fileinfo.size ;
    } else {
        /* If we didn't find the file, return a zero. */
        size = 0 ;
    }

    DebugEnd() ;
#endif

    return size ;
}

/*-------------------------------------------------------------------------*
 * Routine:  FileExist
 *-------------------------------------------------------------------------*/
/**
 *  FileExist checks to see if a file exists and returns TRUE if it does.
 *
 *  @param p_filename -- File to check size of
 *
 *  @return TRUE=file exists, else FALSE
 *
 *<!-----------------------------------------------------------------------*/
E_Boolean FileExist(T_byte8 *p_filename)
{
    E_Boolean fileFound = FALSE ;
    T_file file ;

    DebugRoutine("FileExist") ;
    DebugCheck(p_filename != NULL) ;

    file = FileOpen(p_filename, FILE_MODE_READ) ;
    if (file != FILE_BAD)  {
        fileFound = TRUE ;
        FileClose(file) ;
    }

    DebugEnd() ;

    return fileFound ;
}

/** @} */
/*-------------------------------------------------------------------------*
 * End of File:  FILE.C
 *-------------------------------------------------------------------------*/
