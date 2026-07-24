/****************************************************************************/
/*    FILE:  RESOURCE.H                                                     */
/****************************************************************************/

#ifndef _RESOURCE_H_
#define _RESOURCE_H_

#include <stdio.h>
#include "GENERAL.H"

#define RESOURCE_BAD NULL
#define RESOURCE_FILE_BAD ((T_word16)0xFFFF)

typedef T_word16 T_resourceFile ;

typedef T_void *T_resource ;

T_resourceFile ResourceOpen(T_byte8 *p_filename) ;

T_void ResourceClose(T_resourceFile resourceFile) ;

T_resource ResourceFind(T_resourceFile resourceFile, T_byte8 *p_resourceName) ;

T_void ResourceUnfind(T_resource res) ;

T_void *ResourceLock(T_resource resource) ;

T_void ResourceUnlock(T_resource resource) ;

/* TRUE if the next ResourceLock() call on this resource will actually
   read fresh bytes off disk (as opposed to returning an already-cached
   block).  Callers that need to fix up an on-disk struct's byte order
   exactly once -- not on every lock, since later locks are cache hits on
   already-fixed-up memory -- must check this *before* calling
   ResourceLock(), which flips the resource to "in memory" as a side
   effect of loading it. */
E_Boolean ResourceIsFreshLoad(T_resource resource) ;

T_word32 ResourceGetSize(T_resource resource) ;

T_byte8 *ResourceGetName(T_void *p_data) ;

#ifndef NDEBUG
T_void ResourceDumpIndex(T_resourceFile resourceFile) ;
T_void ResourcePrint(FILE *fp, T_resource res) ;
T_void ResourceCheckByPtr(T_byte8 *p_resData) ;
#else
#define ResourceDumpIndex()
#define ResourcePrint(fp, res)
#define ResourceCheckByPtr(p_resData)
#endif

#endif

/****************************************************************************/
/*    END OF FILE:  RESOURCE.H                                              */
/****************************************************************************/
