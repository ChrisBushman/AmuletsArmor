/****************************************************************************/
/*    FILE:  TICKER.H                                                       */
/****************************************************************************/
#ifndef _TICKER_H_
#define _TICKER_H_

#include "GENERAL.H"
#include "PERFPROF.H"

#define TICKS_PER_SECOND 70

T_void TickerOn(T_void) ;

T_void TickerOff(T_void) ;

T_word32 TickerGet(T_void) ;

T_word32 TickerGetAccurate(T_void) ;

T_void TickerPause(T_void) ;

T_void TickerContinue(T_void) ;

T_void TickerInc(T_void) ;

/* These call sites were originally a compile-time-only (HISPEED_TESTING)
   profiling feature; they now route into PERFPROF.[CH], which is enabled
   at runtime via the --profile command-line flag (see TESTME.C's main())
   and reports a single consolidated, sorted table instead of each site
   printing independently. The f/n parameters (output stream / sample
   count before the old per-site print) are no longer used but are kept in
   the macro signatures so none of the existing call sites need to change.
   Overhead when profiling is off is one branch (PerfProfRecord's early
   return) per call site. */
#  define TICKER_TIME_ROUTINE_PREPARE()         \
            static T_perfProfSlot *__ppSlot = NULL ; \
            unsigned long long __ppStart

#  define TICKER_TIME_ROUTINE_START()     \
            __ppStart = PerfProfNow()

#  define TICKER_TIME_ROUTINE_END(f, s, n) \
            PerfProfRecord(&__ppSlot, (s), __ppStart)

#  define TICKER_TIME_ROUTINE_ENDM(s, n) \
            PerfProfRecord(&__ppSlot, (s), __ppStart)

#endif

/****************************************************************************/
/*    END OF FILE:  TICKER.H                                                */
/****************************************************************************/
