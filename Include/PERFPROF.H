/****************************************************************************/
/*    FILE:  PERFPROF.H                                                    */
/****************************************************************************/
/**
 * Runtime performance profiler, enabled by passing --profile on the
 * command line (see TESTME.C's main()).  Reuses the TICKER_TIME_ROUTINE_*
 * call sites already sprinkled through the codebase (originally a
 * compile-time HISPEED_TESTING feature) -- see TICKER.H.  When profiling
 * is off, PerfProfRecord's early-out keeps the overhead to one branch per
 * call site.
 *
 * Periodically (see PERFPROF_REPORT_SECONDS in PERFPROF.C) prints a table
 * of every named routine sorted by total time, to make the largest
 * contributors to frame time obvious at a glance.
 */
#ifndef _PERFPROF_H_
#define _PERFPROF_H_

#include "GENERAL.H"

typedef struct T_perfProfSlot_tag T_perfProfSlot ;

/* Call once at startup (safe to call even if profiling is off). */
T_void PerfProfInit(T_void) ;

/* Parsed from the --profile command-line flag. */
T_void PerfProfSetEnabled(E_Boolean on) ;
E_Boolean PerfProfIsEnabled(T_void) ;

/* Microsecond-resolution monotonic clock. */
unsigned long long PerfProfNow(T_void) ;

/* Accumulates (PerfProfNow() - startTime) into the named slot, creating it
   on first use and caching the pointer in *p_cachedSlot for later calls
   (each TICKER_TIME_ROUTINE_PREPARE() call site owns one such cache, so
   the name->slot lookup only happens once per call site, not once per
   frame). No-op if profiling is disabled. */
T_void PerfProfRecord(
           T_perfProfSlot **p_cachedSlot,
           const char *name,
           unsigned long long startTime) ;

/* Call once per main-loop iteration.  No-op unless profiling is enabled
   and the report interval has elapsed. */
T_void PerfProfMaybeReport(T_void) ;

#endif

/****************************************************************************/
/*    END OF FILE:  PERFPROF.H                                             */
/****************************************************************************/
