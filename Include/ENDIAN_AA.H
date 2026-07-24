/****************************************************************************/
/*    FILE:  ENDIAN_AA.H                                                    */
/****************************************************************************/
/**
 * Byte-swap helpers for on-disk/on-wire structures.
 *
 * Every resource file, save file, and network packet in this codebase was
 * authored as a raw little-endian struct dump (DOS/Windows x86 heritage).
 * On a little-endian build these helpers are all no-ops (and inline away to
 * nothing); on a big-endian build (e.g. PowerPC) they swap in place.
 *
 * Define AA_BIG_ENDIAN=1 on the compiler command line for big-endian
 * targets. If it isn't defined explicitly, it's inferred from common
 * compiler predefines so a plain `gcc -arch ppc` build still gets it right.
 *
 * @see http://www.amuletsandarmor.com/AALicense.txt
 *<!-----------------------------------------------------------------------*/
#ifndef _ENDIAN_AA_H_
#define _ENDIAN_AA_H_

#include <string.h>
#include "GENERAL.H"

#ifndef AA_BIG_ENDIAN
#  if defined(__BIG_ENDIAN__) || defined(__ppc__) || defined(__powerpc__) || \
      defined(__PPC__) || defined(__POWERPC__) || \
      defined(__MIPSEB__) || defined(MIPSEB) || defined(_MIPSEB)
#    define AA_BIG_ENDIAN 1
#  else
#    define AA_BIG_ENDIAN 0
#  endif
#endif

static inline T_word16 EndianSwap16(T_word16 v)
{
    return (T_word16)((v << 8) | (v >> 8)) ;
}

static inline T_word32 EndianSwap32(T_word32 v)
{
    return ((v << 24) & 0xFF000000UL) |
           ((v << 8)  & 0x00FF0000UL) |
           ((v >> 8)  & 0x0000FF00UL) |
           ((v >> 24) & 0x000000FFUL) ;
}

static inline T_sword16 EndianSwapS16(T_sword16 v)
{
    T_word16 u ;
    memcpy(&u, &v, sizeof(u)) ;
    u = EndianSwap16(u) ;
    memcpy(&v, &u, sizeof(v)) ;
    return v ;
}

static inline T_sword32 EndianSwapS32(T_sword32 v)
{
    T_word32 u ;
    memcpy(&u, &v, sizeof(u)) ;
    u = EndianSwap32(u) ;
    memcpy(&v, &u, sizeof(v)) ;
    return v ;
}

#if AA_BIG_ENDIAN
#  define EndianLE16(v)  EndianSwap16(v)
#  define EndianLE32(v)  EndianSwap32(v)
#  define EndianLES16(v) EndianSwapS16(v)
#  define EndianLES32(v) EndianSwapS32(v)
#else
#  define EndianLE16(v)  (v)
#  define EndianLE32(v)  (v)
#  define EndianLES16(v) (v)
#  define EndianLES32(v) (v)
#endif

/* Single-byte load/store, guaranteed to compile to exactly one
   byte-wide instruction with no possibility of being recombined with
   a neighboring byte access into a wider (alignment-requiring) one.
   Getting here took three attempts on the SGI O2's GCC 4.5.2 at -O2:
   a memcpy() of a fixed 2/4-byte size got pattern-matched into a
   single plain `lw`/`sw`; switching to explicit per-byte assignment
   fixed reads, but the separate byte *stores* then got store-merged
   back into a single unaligned `sh`/`sw` by the optimizer; marking the
   byte pointer `volatile` (which the standard says forbids merging,
   reordering, or eliding any such access) *still* didn't stop it from
   merging two adjacent volatile byte stores on this specific compiler
   version -- confirmed by disassembling three successive core dumps,
   each faulting on a different single merged sh/sw. Inline asm is the
   only remaining lever: it's opaque to the optimizer by definition, so
   there is nothing left to merge. MIPS-only (the actual strict-
   alignment target); every other architecture this codebase builds for
   tolerates unaligned scalar loads/stores, so a plain byte access is
   fine there and doesn't need the inline asm's constraints. */
#if defined(__GNUC__) && (defined(__mips__) || defined(__MIPSEB__) || defined(__MIPSEL__))
static inline T_byte8 IAlignedByteGet(const T_void *p)
{
    T_byte8 v ;
    __asm__ __volatile__ ("lbu %0, 0(%1)" : "=r"(v) : "r"(p) : "memory") ;
    return v ;
}

static inline T_void IAlignedByteSet(T_void *p, T_byte8 v)
{
    __asm__ __volatile__ ("sb %1, 0(%0)" : : "r"(p), "r"(v) : "memory") ;
}
#else
static inline T_byte8 IAlignedByteGet(const T_void *p)
{
    return *((const T_byte8 *)p) ;
}

static inline T_void IAlignedByteSet(T_void *p, T_byte8 v)
{
    *((T_byte8 *)p) = v ;
}
#endif

/* Alignment-safe access to a value that may not be naturally aligned
   (e.g. a field inside a packed on-disk struct instance reached by
   array indexing, or a raw resource/packet buffer offset), composed
   by hand from IAlignedByteGet/Set to match this host's own native
   byte order. Every other helper in this header (the EndianLE*P/R
   swap accessors, and the plain AlignedGet/Set accessors) is built on
   top of these four. */
static inline T_word16 AlignedGetW16(const T_void *p)
{
    const T_byte8 *b = (const T_byte8 *)p ;
    T_byte8 b0 = IAlignedByteGet(b + 0) ;
    T_byte8 b1 = IAlignedByteGet(b + 1) ;
#if AA_BIG_ENDIAN
    return (T_word16)(((T_word16)b0 << 8) | (T_word16)b1) ;
#else
    return (T_word16)((T_word16)b0 | ((T_word16)b1 << 8)) ;
#endif
}

static inline T_void AlignedSetW16(T_void *p, T_word16 v)
{
    T_byte8 *b = (T_byte8 *)p ;
#if AA_BIG_ENDIAN
    IAlignedByteSet(b + 0, (T_byte8)(v >> 8)) ;
    IAlignedByteSet(b + 1, (T_byte8)(v)) ;
#else
    IAlignedByteSet(b + 0, (T_byte8)(v)) ;
    IAlignedByteSet(b + 1, (T_byte8)(v >> 8)) ;
#endif
}

static inline T_word32 AlignedGetW32(const T_void *p)
{
    const T_byte8 *b = (const T_byte8 *)p ;
    T_byte8 b0 = IAlignedByteGet(b + 0) ;
    T_byte8 b1 = IAlignedByteGet(b + 1) ;
    T_byte8 b2 = IAlignedByteGet(b + 2) ;
    T_byte8 b3 = IAlignedByteGet(b + 3) ;
#if AA_BIG_ENDIAN
    return ((T_word32)b0 << 24) | ((T_word32)b1 << 16) |
           ((T_word32)b2 << 8)  | (T_word32)b3 ;
#else
    return (T_word32)b0 | ((T_word32)b1 << 8) |
           ((T_word32)b2 << 16) | ((T_word32)b3 << 24) ;
#endif
}

static inline T_void AlignedSetW32(T_void *p, T_word32 v)
{
    T_byte8 *b = (T_byte8 *)p ;
#if AA_BIG_ENDIAN
    IAlignedByteSet(b + 0, (T_byte8)(v >> 24)) ;
    IAlignedByteSet(b + 1, (T_byte8)(v >> 16)) ;
    IAlignedByteSet(b + 2, (T_byte8)(v >> 8)) ;
    IAlignedByteSet(b + 3, (T_byte8)(v)) ;
#else
    IAlignedByteSet(b + 0, (T_byte8)(v)) ;
    IAlignedByteSet(b + 1, (T_byte8)(v >> 8)) ;
    IAlignedByteSet(b + 2, (T_byte8)(v >> 16)) ;
    IAlignedByteSet(b + 3, (T_byte8)(v >> 24)) ;
#endif
}

/* In-place variants, for swapping a field that lives in a struct or a raw
   resource/packet buffer. Built on the byte-wise Aligned accessors above
   (not memcpy -- see the comment there for why that isn't safe on every
   compiler this codebase targets). */
static inline T_void EndianLE16P(T_void *p)
{
    AlignedSetW16(p, EndianLE16(AlignedGetW16(p))) ;
}

static inline T_void EndianLE32P(T_void *p)
{
    AlignedSetW32(p, EndianLE32(AlignedGetW32(p))) ;
}

static inline T_void EndianLES16P(T_void *p)
{
    AlignedSetW16(p, (T_word16)EndianLES16((T_sword16)AlignedGetW16(p))) ;
}

static inline T_void EndianLES32P(T_void *p)
{
    AlignedSetW32(p, (T_word32)EndianLES32((T_sword32)AlignedGetW32(p))) ;
}

/* Alignment-safe read of a scalar that may not be naturally aligned,
   for read-only call sites that don't want to (or can't) write the
   swapped value back. */
static inline T_word16 EndianLE16R(const T_void *p)
{
    return EndianLE16(AlignedGetW16(p)) ;
}

static inline T_word32 EndianLE32R(const T_void *p)
{
    return EndianLE32(AlignedGetW32(p)) ;
}

static inline T_sword16 EndianLES16R(const T_void *p)
{
    return (T_sword16)EndianLES16((T_sword16)AlignedGetW16(p)) ;
}

static inline T_sword32 EndianLES32R(const T_void *p)
{
    return (T_sword32)EndianLES32((T_sword32)AlignedGetW32(p)) ;
}

#endif // _ENDIAN_AA_H_

/****************************************************************************/
/*    END OF FILE:  ENDIAN_AA.H                                             */
/****************************************************************************/
