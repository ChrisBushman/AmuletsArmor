/*-------------------------------------------------------------------------*
 * File:  test_endian.c
 *-------------------------------------------------------------------------*/
/**
 * Round-trip / known-value tests for Include/ENDIAN_AA.H, the byte-swap
 * helpers used everywhere resource files, save files, and network packets
 * are read or written (see Build/MacOSX-PPC/ENDIAN_AUDIT.md).
 *
 * These run on any host regardless of its own endianness: EndianSwap16/32
 * are unconditional bit manipulations, not host-dependent, so their
 * correctness can (and must) be verified here rather than only on real
 * big-endian hardware.
 *
 * A round-trip check alone (f(f(x)) == x) cannot distinguish a correct
 * swap from a no-op bug, since identity also round-trips. Every test below
 * pairs round-trip checks with explicit known-byte-pattern assertions.
 *<!-----------------------------------------------------------------------*/
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Include/ENDIAN_AA.H"

static void test_swap16_known_values(void)
{
    assert(EndianSwap16(0x1234) == 0x3412);
    assert(EndianSwap16(0x0000) == 0x0000);
    assert(EndianSwap16(0xFFFF) == 0xFFFF);
    assert(EndianSwap16(0x00FF) == 0xFF00);
    assert(EndianSwap16(0xFF00) == 0x00FF);
    assert(EndianSwap16(0x0001) == 0x0100);

    printf("test_swap16_known_values passed.\n");
}

static void test_swap32_known_values(void)
{
    assert(EndianSwap32(0x12345678UL) == 0x78563412UL);
    assert(EndianSwap32(0x00000000UL) == 0x00000000UL);
    assert(EndianSwap32(0xFFFFFFFFUL) == 0xFFFFFFFFUL);
    assert(EndianSwap32(0x000000FFUL) == 0xFF000000UL);
    assert(EndianSwap32(0xFF000000UL) == 0x000000FFUL);
    assert(EndianSwap32(0x0000FFFFUL) == 0xFFFF0000UL);

    printf("test_swap32_known_values passed.\n");
}

static void test_swap_signed_known_values(void)
{
    /* -1 is all-bits-set regardless of word size, so it must swap to
       itself; that alone wouldn't catch a broken swap, so also check a
       negative value whose byte pattern is NOT symmetric. */
    assert(EndianSwapS16((T_sword16)-1) == (T_sword16)-1);
    assert(EndianSwapS32((T_sword32)-1) == (T_sword32)-1);

    /* -2 = 0xFFFE (16-bit).  Swapped bytes: 0xFEFF = -257. */
    assert(EndianSwapS16((T_sword16)-2) == (T_sword16)0xFEFF);
    assert(EndianSwapS16((T_sword16)0xFEFF) == (T_sword16)-2);

    /* T_sword16 min (-32768 = 0x8000).  Swapped bytes: 0x0080 = 128. */
    assert(EndianSwapS16((T_sword16)0x8000) == (T_sword16)0x0080);

    /* -2 = 0xFFFFFFFE (32-bit).  Swapped bytes: 0xFEFFFFFF. */
    assert(EndianSwapS32((T_sword32)-2) == (T_sword32)0xFEFFFFFFUL);
    assert(EndianSwapS32((T_sword32)0xFEFFFFFFUL) == (T_sword32)-2);

    /* T_sword32 min (0x80000000).  Swapped bytes: 0x00000080. */
    assert(EndianSwapS32((T_sword32)0x80000000UL) == (T_sword32)0x00000080UL);

    printf("test_swap_signed_known_values passed.\n");
}

static void test_round_trip(void)
{
    static const T_word16 u16values[] = {
        0x0000, 0x0001, 0x00FF, 0xFF00, 0x1234, 0x8000, 0x7FFF, 0xFFFF
    };
    static const T_word32 u32values[] = {
        0x00000000UL, 0x00000001UL, 0x000000FFUL, 0xFF000000UL,
        0x12345678UL, 0x80000000UL, 0x7FFFFFFFUL, 0xFFFFFFFFUL
    };
    size_t i;

    for (i = 0; i < sizeof(u16values)/sizeof(u16values[0]); i++)
        assert(EndianSwap16(EndianSwap16(u16values[i])) == u16values[i]);

    for (i = 0; i < sizeof(u32values)/sizeof(u32values[0]); i++)
        assert(EndianSwap32(EndianSwap32(u32values[i])) == u32values[i]);

    for (i = 0; i < sizeof(u16values)/sizeof(u16values[0]); i++)  {
        T_sword16 v = (T_sword16)u16values[i];
        assert(EndianSwapS16(EndianSwapS16(v)) == v);
    }

    for (i = 0; i < sizeof(u32values)/sizeof(u32values[0]); i++)  {
        T_sword32 v = (T_sword32)u32values[i];
        assert(EndianSwapS32(EndianSwapS32(v)) == v);
    }

    printf("test_round_trip passed.\n");
}

/* EndianLE16/32/EndianLES16/32 are used throughout the Source tree in a
   swap-before-write-then-swap-back pattern (e.g. StatsSaveCharacter in
   STATS.C) and a swap-once-per-direction pattern (the network layer in
   CMDQUEUE.C/PACKETDT.C). Both patterns depend on these macros being
   involutions (self-inverse). This holds trivially on this little-endian
   test host, where they compile to the identity -- the meaningful
   guarantee is that EndianSwap16/32 (which back them on big-endian
   builds) are involutions too, already covered by test_round_trip. This
   test locks in the macro contract so it can't regress silently. */
static void test_endian_le_macros_self_inverse(void)
{
    T_word16 u16 = 0x1234;
    T_word32 u32 = 0x12345678UL;
    T_sword16 s16 = (T_sword16)0x8000;
    T_sword32 s32 = (T_sword32)0x80000000UL;

    assert(EndianLE16(EndianLE16(u16)) == u16);
    assert(EndianLE32(EndianLE32(u32)) == u32);
    assert(EndianLES16(EndianLES16(s16)) == s16);
    assert(EndianLES32(EndianLES32(s32)) == s32);

    printf("test_endian_le_macros_self_inverse passed.\n");
}

/* Representative on-disk struct shapes, mirroring real production structs
   without pulling in their headers (same approach as test_distance.c):
   a resource/map-style directory entry (IRESOURC.H's T_resourceFileHeader,
   VIEWFILE.H's T_directoryEntry) and the hot-path network sync record
   (SYNCPACK.H's T_syncronizePacket). Confirms the field-by-field swap
   pattern used at every load/save/send/receive site in the Source tree.
   These call EndianSwap16/32/S16 directly (not the EndianLE* macros)
   because that pattern's whole point is to be a no-op on this
   little-endian test host -- to actually exercise it, swap
   unconditionally, exactly as EndianLE* would on a big-endian target. */
typedef struct {
    T_word32 fileOffset ;
    T_word32 size ;
    T_word16 lockCount ;
    T_byte8  resID[4] ;      /* raw tag bytes -- must NOT be swapped */
} T_testDirEntry ;

static void ISwapTestDirEntry(T_testDirEntry *p)
{
    p->fileOffset = EndianSwap32(p->fileOffset) ;
    p->size = EndianSwap32(p->size) ;
    p->lockCount = EndianSwap16(p->lockCount) ;
}

typedef struct {
    T_byte8  syncNumber ;
    T_word16 playerObjectId ;
    T_sword16 x ;
    T_sword16 y ;
    T_word16 actionData[2] ;
} T_testSyncRecord ;

static void ISwapTestSyncRecord(T_testSyncRecord *p)
{
    p->playerObjectId = EndianSwap16(p->playerObjectId) ;
    p->x = EndianSwapS16(p->x) ;
    p->y = EndianSwapS16(p->y) ;
    p->actionData[0] = EndianSwap16(p->actionData[0]) ;
    p->actionData[1] = EndianSwap16(p->actionData[1]) ;
}

static void test_struct_roundtrip(void)
{
    T_testDirEntry dir ;
    T_testDirEntry dirOriginal ;
    T_testSyncRecord sync ;
    T_testSyncRecord syncOriginal ;

    dir.fileOffset = 0x00010000UL ;
    dir.size = 0x00001234UL ;
    dir.lockCount = 0x0007 ;
    memcpy(dir.resID, "ReS", 4) ;
    dirOriginal = dir ;

    /* One swap must actually move bytes -- guards against a no-op bug
       that a round-trip-only check would miss. */
    ISwapTestDirEntry(&dir) ;
    assert(dir.fileOffset == EndianSwap32(dirOriginal.fileOffset)) ;
    assert(dir.size == EndianSwap32(dirOriginal.size)) ;
    assert(dir.lockCount == EndianSwap16(dirOriginal.lockCount)) ;
    assert(memcmp(dir.resID, dirOriginal.resID, 4) == 0) ; /* untouched */

    /* Swapping back must restore the original. */
    ISwapTestDirEntry(&dir) ;
    assert(dir.fileOffset == dirOriginal.fileOffset) ;
    assert(dir.size == dirOriginal.size) ;
    assert(dir.lockCount == dirOriginal.lockCount) ;
    assert(memcmp(dir.resID, dirOriginal.resID, 4) == 0) ;

    sync.syncNumber = 42 ;
    sync.playerObjectId = 0xBEEF ;
    sync.x = (T_sword16)-1000 ;
    sync.y = (T_sword16)1000 ;
    sync.actionData[0] = 0x00FF ;
    sync.actionData[1] = 0xFF00 ;
    syncOriginal = sync ;

    ISwapTestSyncRecord(&sync) ;
    assert(sync.syncNumber == syncOriginal.syncNumber) ; /* byte, untouched */
    assert(sync.playerObjectId == EndianSwap16(syncOriginal.playerObjectId)) ;
    assert(sync.x == EndianSwapS16(syncOriginal.x)) ;
    assert(sync.y == EndianSwapS16(syncOriginal.y)) ;
    assert(sync.actionData[0] == EndianSwap16(syncOriginal.actionData[0])) ;
    assert(sync.actionData[1] == EndianSwap16(syncOriginal.actionData[1])) ;

    ISwapTestSyncRecord(&sync) ;
    assert(sync.syncNumber == syncOriginal.syncNumber) ;
    assert(sync.playerObjectId == syncOriginal.playerObjectId) ;
    assert(sync.x == syncOriginal.x) ;
    assert(sync.y == syncOriginal.y) ;
    assert(sync.actionData[0] == syncOriginal.actionData[0]) ;
    assert(sync.actionData[1] == syncOriginal.actionData[1]) ;

    printf("test_struct_roundtrip passed.\n");
}

int main(void)
{
    test_swap16_known_values();
    test_swap32_known_values();
    test_swap_signed_known_values();
    test_round_trip();
    test_endian_le_macros_self_inverse();
    test_struct_roundtrip();

    printf("All endian swap tests passed.\n");
    return 0;
}
