
#ifndef _GENERAL_H_
#define _GENERAL_H_

#include "OPTIONS.H"

/* General typedefs declared  */
/** This file now supports either Watcom C32 OR Linux GCC. -- AMT, 8/2/95 **/

/** If I'm on a unix box, grab some system typedefs as well. **/
#ifdef TARGET_UNIX
#  include <stdio.h>
#  include <sys/types.h>
#  include <strings.h>
#  include <limits.h>
#  include <unistd.h>

typedef time_t T_time;

#  ifndef O_BINARY
#    define O_BINARY 0
#  endif

#  ifndef stricmp
#    define stricmp strcasecmp
#  endif

#  ifndef strcmpi
#    define strcmpi strcasecmp
#  endif

#  ifndef strnicmp
#    define strnicmp strncasecmp
#  endif

#  ifndef inp
#    define inp(port) (0)
#  endif

#  ifndef outp
#    define outp(port, value) ((void)0)
#  endif

#  ifndef itoa
static inline char *aa_itoa(int value, char *buffer, int base)
{
    if (base == 16)
        sprintf(buffer, "%x", value);
    else
        sprintf(buffer, "%d", value);
    return buffer;
}
#    define itoa(v, b, base) aa_itoa((v), (b), (base))
#  endif

#endif

#if DOS32
/** DOS includes **/
/** C library and system call interface includes. **/
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#  include <mem.h>
#  include <i86.h>
#  include <io.h>
#  include <dos.h>
#  include <conio.h>
#  include <math.h>
#endif

/** If under GCC, we need to specify that our packet structures are **/
/** packed. **/
#ifdef __GNUC__
#  define PACK __attribute__ ((packed))
#else
#  define PACK
#endif

/* Per-field PACK (above) only removes *inter-field padding* -- it does not
   lower the struct TYPE's own alignment, so the compiler still assumes a
   pointer of this type is naturally aligned (e.g. 2 or 4 bytes) when
   generating code, which is exactly wrong for array-indexed instances of
   an odd-sized packed struct (each element's start address alternates
   parity). Go through PACK_STRUCT (applied once, right before the
   typedef'd name, after the closing brace) for any on-disk/on-wire struct
   that is walked as an array or reached via variable byte-offset
   arithmetic: __attribute__((aligned(1))) makes the compiler treat EVERY
   pointer of that type as unaligned and emit unaligned-safe field
   accesses, instead of memcpy'ing every field at every call site. This
   is a no-op under MSVC (which achieves the on-disk packing via a
   project-wide StructMemberAlignment setting instead, same as PACK). */
#ifdef __GNUC__
#  define PACK_STRUCT __attribute__ ((packed, aligned(1)))
#else
#  define PACK_STRUCT
#endif

/* Because many other #includes define these in bad ways. */
#undef TRUE
#undef FALSE

#ifdef __cplusplus
/* GCC 4.5.2's C++ frontend (the only compiler available for the IRIX/O2
   port) mis-parses a typedef'd void used as a function's sole parameter
   type -- "T_void foo(T_void);" fails with "'<anonymous>' has incomplete
   type" / "invalid use of 'T_void'", confirmed with a two-line repro
   (typedef void T_void; T_void foo(T_void);) independent of anything else
   in this codebase. A macro sidesteps it entirely: "T_void" is gone by
   the time the parser sees the token stream, same as writing "void"
   directly. Plain C (every other platform, and IRIX's own C files) has
   no such bug, so it keeps the typedef -- macros are best avoided where
   a typedef works, and this file is also included from C translation
   units. */
#define T_void void
#else
typedef void T_void ;
#endif

typedef unsigned char T_bit1 ;

typedef unsigned char T_nibble4 ;
typedef signed char T_snibble4 ;

typedef unsigned char T_byte8 ;
typedef signed char T_sbyte8 ;

typedef unsigned short T_word16 ;
typedef signed short T_sword16 ;

typedef unsigned int T_word32 ;
typedef signed int T_sword32 ;

#ifdef TARGET_UNIX
#  include <stdint.h>
typedef uintptr_t T_uintptr_t ;
#else
typedef T_word32 T_uintptr_t ;
#endif

typedef unsigned long T_word64 ;
typedef signed long T_sword64 ;

typedef T_byte8 E_Boolean ;
#define FALSE 0
#define TRUE 1
#define BOOLEAN_UNKNOWN 2

typedef union {
    T_word32 w32 ;
    struct {
        T_word16 low ;
        T_word16 high ;
    } w1616 ;
} T_word32as1616 ;

typedef union {
    T_word32 w32 ;
    struct {
        T_byte8 lowest ;
        T_byte8 low ;
        T_byte8 high ;
        T_byte8 highest ;
    } w8888 ;
} T_word32as8888 ;

#ifndef NULL
#define NULL 0
#endif

#include "DEBUG.H"

#endif
