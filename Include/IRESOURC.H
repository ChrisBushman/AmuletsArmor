/****************************************************************************/
/*    FILE:  IRESOURC.H                                                     */
/****************************************************************************/
#ifndef _IRESOURC_H_
#define _IRESOURC_H_

#include "FILE.H"
#include "GENERAL.H"
#include "ENDIAN_AA.H"

#define MAX_RESOURCE_FILES 10

#define RESOURCE_ENTRY_TYPE_MEMORY     0x00
#define RESOURCE_ENTRY_TYPE_DISK       0x01
#define RESOURCE_ENTRY_TYPE_DISCARDED  0x02

#ifndef NDEBUG
#define RESOURCE_ENTRY_TYPE_FILE_LOADED 0x80
#endif

#define RESOURCE_ENTRY_TYPE_FILE       0x00
#define RESOURCE_ENTRY_TYPE_DIRECTORY  0x10
#define RESOURCE_ENTRY_TYPE_LINK       0x20
#define RESOURCE_ENTRY_TYPE_APPEND     0x30

#define RESOURCE_ENTRY_TYPE_UNKNOWN    0xC3

#define RESOURCE_ENTRY_TYPE_MASK_WHERE    0x0F
#define RESOURCE_ENTRY_TYPE_MASK_TYPE     0xF0


/* Composed as a compile-time constant rather than a string-literal cast:
   string literals aren't guaranteed 4-byte aligned, and a raw T_word32*
   dereference of one faults with SIGBUS on strict-alignment targets
   (e.g. the SGI O2/MIPS port). Unlike the codebase's other in-memory-only
   TAG constants, this one is compared directly against raw disk bytes
   that are deliberately never byte-swapped (see RESOURCE.C) -- so unlike
   those, it must match whatever a native reinterpret of "Res!"'s 4
   memory bytes would have produced on each endianness, not one fixed
   byte order. */
#if AA_BIG_ENDIAN
#define RESOURCE_FILE_UNIQUE_ID   ((T_word32)(((T_word32)'R' << 24) | \
                                              ((T_word32)'e' << 16) | \
                                              ((T_word32)'s' << 8)  | \
                                              ((T_word32)'!')))
#else
#define RESOURCE_FILE_UNIQUE_ID   ((T_word32)((T_word32)'R'         | \
                                              ((T_word32)'e' << 8)  | \
                                              ((T_word32)'s' << 16) | \
                                              ((T_word32)'!' << 24)))
#endif

/* Not PACK'd (unlike the codebase's genuine on-disk/on-wire structs):
   this is a purely in-memory, heap-allocated bookkeeping struct -- every
   FileRead of the actual on-disk index goes through a dedicated Disk32-
   style struct (see T_resourceEntryDisk32 in RESOURCE.C) that gets
   converted into this one field-by-field, never a raw fread/memcpy into
   T_resourceEntry itself. PACK is a GCC-only attribute (a no-op under
   MSVC, where Windows achieves the same on-disk packing via a project-
   wide StructMemberAlignment setting instead), so it was only ever taking
   effect on GCC/Clang -- where letting the compiler apply natural
   alignment to a struct containing pointers and mixed int widths matters:
   packed pointer-sized fields land array-indexed instances (p_entries[i])
   at addresses a strict-alignment CPU (MIPS, e.g. the SGI O2 port) faults
   on with SIGBUS; x86/PPC just tolerated the same unaligned accesses
   silently. */
typedef struct {
    T_byte8 resID[4] ; /* Should contain "ReS"+'\0' id */
    T_byte8 p_resourceName[14] ; /* Case sensitive, 13 characters + '\0' */
    T_word32 fileOffset ;
    T_word32 size ; /* Size in bytes. */
    T_word16 lockCount ; /* 0 = unlocked. */
    T_byte8 resourceType ;
    T_byte8 *p_data ;
    T_resourceFile resourceFile ; /* Resource file this is from. */
    T_void *ownerDir ; /* Locked in owner directory (or NULL) */
} T_resourceEntry ;

/* General information stored for each resource directory in the system.
   Also purely in-memory -- see T_resourceEntry's comment above. */
typedef struct {
    T_file fileHandle ;
    T_word16 numberEntries ;
    T_resourceEntry *p_entries ;
    T_resource ownerRes ;
    T_sword16 nextResource ;
} T_resourceDirInfo ;

/* Header that is placed at the beginning of the file to locate */
/* the resource index. */
#if defined(__MWERKS__)
#pragma options align=packed
#endif
typedef struct {
    T_word32 uniqueID           PACK;
    T_word32 indexOffset        PACK;
    T_word32 indexSize          PACK; /* Size of index in bytes. */
    T_word16 numEntries         PACK; /* Number of entries in the index. */
} T_resourceFileHeader ;
#if defined(__MWERKS__)
#pragma options align=reset
#endif

#endif // _IRESOURC_H_

/****************************************************************************/
/*    End of FILE:  IRESOURC.H                                              */
/****************************************************************************/

