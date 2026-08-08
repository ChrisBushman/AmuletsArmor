/* aa_os9_compat.c -- tiny POSIX shims for the classic Mac OS (CFM) build.
 *
 * libSDL's env handling and the MSL C++ termination path reference a couple
 * of POSIX libc functions that the classic-Mac MSL runtime doesn't provide.
 * Satisfy them here:
 *   - putenv(): environment variables are always unset on classic Mac, and
 *     the only readers (SDL / sdl12-compat knobs) just fall back to defaults,
 *     so a no-op that reports success is correct.
 *   - _exit(): "immediate exit, skip atexit cleanup". On non-preemptive
 *     classic Mac there is no signal-context reentrancy to avoid, so mapping
 *     it to the normal exit() is fine.
 */
#include <stdlib.h>
#include <stdio.h>

/* Boot tracer for the OS 9 bring-up: each call appends+flushes one line to
 * aa_boot.log (in the app folder), so after a crash the last line names the
 * last startup step that completed. open+close per call so nothing is lost to
 * buffering when the app faults. Remove once the game runs. */
void AA_BootLog(const char *msg)
{
#ifdef AA_BOOT_TRACE
    /* Open ONCE and keep it open, flushing each line. Re-opening in append
       mode on every call (the obvious approach) produces garbled, interleaved
       writes on classic-Mac MSL -- and the repeated fopen/fclose churn was
       itself a suspect for the mid-startup crash. "w" truncates, so each run
       starts fresh (no need to delete the log between runs). */
    static FILE *f = (FILE *)0;
    if (f == (FILE *)0) {
        f = fopen("aa_boot.log", "w");
        if (f)
            setvbuf(f, (char *)0, _IONBF, 0);   /* unbuffered: each write goes straight out */
    }
    if (f) {
        fprintf(f, "%s\n", msg);
        fflush(f);
    }
#else
    (void)msg;
#endif
}

int putenv(char *string)
{
    (void)string;
    return 0;
}

void _exit(int status)
{
    exit(status);
}

/* SetDialogTimeout: libSDL references it (Appearance modal-dialog timeout).
 * At runtime classic OS 9 has it inside InterfaceLib, but linking CW's
 * DialogsLib stub to satisfy it makes the app import a *separate* shared
 * library "MacDialogsLib" that does NOT exist as a loadable fragment on
 * classic OS 9 -- so the CFM loader fails the whole app at launch. Provide a
 * harmless local definition instead and DON'T link DialogsLib. AA never opens
 * the modal error dialogs SDL would call this for, so a no-op is correct.
 * (Types come from the MacHeaders prefix that's force-included into every TU.)
 */
pascal OSStatus SetDialogTimeout(DialogRef inDialog, DialogItemIndex inButtonToPress,
                                 UInt32 inSecondsToWait)
{
    (void)inDialog;
    (void)inButtonToPress;
    (void)inSecondsToWait;
    return noErr;
}
