/****************************************************************************/
/*    FILE:  RESSCALE_MAC.m                                                  */
/****************************************************************************/
/*
 * macOS-only helper for RESSCALE's "borderless windowed-fullscreen".
 *
 * True fullscreen on macOS (a fullscreen Space / direct scanout) makes the
 * display's content-adaptive backlight pulse the brightness of dark scenes
 * -- a whole-screen flicker that no rendering setting (vsync, colorspace,
 * EDR, renderer) fixes; only staying on the normal compositor path avoids
 * it.  So on macOS RESSCALE creates a borderless *window* the size of the
 * desktop instead of a real fullscreen surface, and this helper makes that
 * window actually cover everything: hide the menu bar + dock, raise the
 * window above the menu-bar level, and size it to the full screen frame.
 * The window is never put into a fullscreen Space, so the flicker never
 * engages.  (Confirmed with standalone SDL2 prototypes.)
 */

/* The build defines WIN32=1 project-wide (this is a Windows-SDL port); that
   makes the macOS SDK's own headers (Security/cssmtype.h via AppKit) take a
   Windows code path and reference Win32-only types like FARPROC.  This file
   is pure Cocoa and does not need WIN32 -- undefine it before importing. */
#undef WIN32
#undef _WIN32
#import <AppKit/AppKit.h>

void ResScaleMacBorderlessFullscreen(void)
{
    NSWindow *w;

    /* App-level: hide the menu bar and dock.  HideMenuBar requires HideDock. */
    [NSApp setPresentationOptions:
        (NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar)];

    /* Window-level: raise above the (now-hidden) menu bar and cover the full
       screen frame.  Reached via NSApp so we don't depend on SDL exposing the
       native window handle. */
    w = [NSApp keyWindow];
    if (w == nil)
        w = [[NSApp windows] firstObject];
    if (w != nil)  {
        NSScreen *scr = [w screen];
        if (scr == nil)
            scr = [NSScreen mainScreen];
        [w setLevel:(NSMainMenuWindowLevel + 1)];
        if (scr != nil)
            [w setFrame:[scr frame] display:YES];
    }
}

/* Undo the presentation-option changes (menu bar / dock) on shutdown or when
   leaving borderless-fullscreen, so a clean quit restores the desktop. */
void ResScaleMacRestore(void)
{
    [NSApp setPresentationOptions:NSApplicationPresentationDefault];
}

/****************************************************************************/
/*    END OF FILE:  RESSCALE_MAC.m                                           */
/****************************************************************************/
