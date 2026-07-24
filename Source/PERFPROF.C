/*-------------------------------------------------------------------------*
 * File:  PERFPROF.C
 *-------------------------------------------------------------------------*/
/**
 * See PERFPROF.H.
 *
 *<!-----------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "PERFPROF.H"

#ifdef TARGET_UNIX
#include <sys/time.h>
#else
#include "SDL.H"
#endif

#define PERFPROF_MAX_SLOTS      64
#define PERFPROF_REPORT_SECONDS 2

struct T_perfProfSlot_tag {
    char name[48] ;
    unsigned long long totalMicros ;
    unsigned long long maxMicros ;
    T_word32 callCount ;
} ;

static T_perfProfSlot G_slots[PERFPROF_MAX_SLOTS] ;
static T_word16 G_numSlots = 0 ;
static E_Boolean G_enabled = FALSE ;
static unsigned long long G_lastReportTime = 0 ;

T_void PerfProfInit(T_void)
{
    G_numSlots = 0 ;
    G_enabled = FALSE ;
    G_lastReportTime = PerfProfNow() ;
}

T_void PerfProfSetEnabled(E_Boolean on)
{
    G_enabled = on ;
    G_lastReportTime = PerfProfNow() ;
}

E_Boolean PerfProfIsEnabled(T_void)
{
    return G_enabled ;
}

unsigned long long PerfProfNow(T_void)
{
#ifdef TARGET_UNIX
    struct timeval tv ;
    gettimeofday(&tv, NULL) ;
    return ((unsigned long long)tv.tv_sec * 1000000ULL) + (unsigned long long)tv.tv_usec ;
#else
    return (unsigned long long)SDL_GetTicks() * 1000ULL ;
#endif
}

static T_perfProfSlot *IPerfProfFindOrCreateSlot(const char *name)
{
    T_word16 i ;
    T_perfProfSlot *p_slot ;

    for (i = 0; i < G_numSlots; i++)  {
        if (strcmp(G_slots[i].name, name) == 0)
            return &G_slots[i] ;
    }

    if (G_numSlots >= PERFPROF_MAX_SLOTS)
        return NULL ;

    p_slot = &G_slots[G_numSlots] ;
    memset(p_slot, 0, sizeof(*p_slot)) ;
    strncpy(p_slot->name, name, sizeof(p_slot->name)-1) ;
    G_numSlots++ ;

    return p_slot ;
}

T_void PerfProfRecord(
           T_perfProfSlot **p_cachedSlot,
           const char *name,
           unsigned long long startTime)
{
    unsigned long long elapsed ;

    if (!G_enabled)
        return ;

    if (*p_cachedSlot == NULL)  {
        *p_cachedSlot = IPerfProfFindOrCreateSlot(name) ;
        if (*p_cachedSlot == NULL)
            return ;
    }

    elapsed = PerfProfNow() - startTime ;

    (*p_cachedSlot)->totalMicros += elapsed ;
    (*p_cachedSlot)->callCount++ ;
    if (elapsed > (*p_cachedSlot)->maxMicros)
        (*p_cachedSlot)->maxMicros = elapsed ;
}

static int ICompareSlotsByTotal(const void *a, const void *b)
{
    const T_perfProfSlot *p_a = *(const T_perfProfSlot * const *)a ;
    const T_perfProfSlot *p_b = *(const T_perfProfSlot * const *)b ;

    if (p_a->totalMicros > p_b->totalMicros)
        return -1 ;
    if (p_a->totalMicros < p_b->totalMicros)
        return 1 ;
    return 0 ;
}

T_void PerfProfMaybeReport(T_void)
{
    unsigned long long now ;
    unsigned long long elapsedTotal ;
    T_perfProfSlot *p_sorted[PERFPROF_MAX_SLOTS] ;
    T_word16 i ;

    if (!G_enabled)
        return ;

    now = PerfProfNow() ;
    elapsedTotal = now - G_lastReportTime ;
    if (elapsedTotal < (PERFPROF_REPORT_SECONDS * 1000000ULL))
        return ;

    for (i = 0; i < G_numSlots; i++)
        p_sorted[i] = &G_slots[i] ;
    qsort(p_sorted, G_numSlots, sizeof(p_sorted[0]), ICompareSlotsByTotal) ;

    printf("[PERF] ---- %.2fs window ----\n", elapsedTotal / 1000000.0) ;
    printf("[PERF] %-32s %10s %8s %10s %7s\n",
           "routine", "total ms", "calls", "avg ms", "% wall") ;
    for (i = 0; i < G_numSlots; i++)  {
        T_perfProfSlot *p_slot = p_sorted[i] ;
        double totalMs = p_slot->totalMicros / 1000.0 ;
        double avgMs = (p_slot->callCount > 0)
                           ? (totalMs / (double)p_slot->callCount) : 0.0 ;
        double pctWall = (elapsedTotal > 0)
                              ? (100.0 * (double)p_slot->totalMicros / (double)elapsedTotal)
                              : 0.0 ;

        if (p_slot->callCount == 0)
            continue ;

        printf("[PERF] %-32s %10.2f %8u %10.3f %6.1f%%\n",
               p_slot->name, totalMs, p_slot->callCount, avgMs, pctWall) ;

        p_slot->totalMicros = 0 ;
        p_slot->maxMicros = 0 ;
        p_slot->callCount = 0 ;
    }

    G_lastReportTime = now ;
}

/*-------------------------------------------------------------------------*
 * End of File:  PERFPROF.C
 *-------------------------------------------------------------------------*/
