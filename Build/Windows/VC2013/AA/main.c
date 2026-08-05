#include "WINDIRECT.H"
#include <time.h>
#ifdef TARGET_UNIX
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#else
/* Not a raw #include <Windows.h>: this project's global 1-byte struct
   packing corrupts several of windows.h's own internal compile-time
   size checks (see WINPACKFIX.H's own comment for the full story). */
#include "WINPACKFIX.H"
#endif
#include "DITALK.H"
#if defined(_DEBUG) && defined(_MSC_VER)
   #include <crtdbg.h>
#endif
#if defined(AA_GENERIC_UNIX_MAIN)
/* On a "plain" Unix/X11 target (see the AA_GENERIC_UNIX_MAIN block below),
   this codebase's own -DWIN32=1 (needed by real WIN32-guarded branches
   elsewhere in Source/*.C, unrelated to SDL) leaks into SDL_platform.h's
   own `#if defined(WIN32) ...` sniffing and makes it define __WIN32__,
   which makes SDL_main.h #define main SDL_main -- fine on real
   Windows/macOS (a real SDLmain provides the actual main() that calls
   SDL_main()), but on Linux there is no such shim, so the binary links
   with no main() at all. Hide WIN32 from SDL's own headers only; restore
   it immediately after for the rest of this file and everything else. */
#undef WIN32
#include <SDL.h>
#define WIN32 1
#else
#include <SDL.h>
#endif
#include "resource.h"

/* TRUE-HIGHRES: native high-res 3D view compositing. Not just TARGET_UNIX:
   the real (mingw/GCC-built) Windows 9x target uses this same pipeline
   too -- only the original MSVC-built Windows client keeps the older,
   fixed-640x400 path further down in this file. */
#if defined(TARGET_UNIX) || defined(__GNUC__)
#include "HIGHRES.H"
#include "3D_VIEW.H"
#include "RESSCALE.H"
#endif

#define CAP_SPEED_TO_FPS       0 // 70 // 0

/* Shorthand for "uses the RESSCALE/HighRes pipeline" (TARGET_UNIX, or the
   real mingw/GCC-built Windows 9x target) vs. the old fixed-640x400 path
   (the original MSVC-built Windows client only). See the include block
   above for why this is broader than just TARGET_UNIX. */
#if defined(TARGET_UNIX) || defined(__GNUC__)
#define AA_USE_RESSCALE_PIPELINE 1
#endif

static int G_done = FALSE;
static SDL_Surface* screen;
static SDL_Surface* surface;
#ifndef AA_USE_RESSCALE_PIPELINE
static SDL_Surface* largesurface;
#endif
#ifdef AA_USE_RESSCALE_PIPELINE
/* TRUE-HIGHRES: the window itself is managed by RESSCALE (resolution.ini:
   any window size incl. 1024x768 / 1920x1080, fullscreen, letterboxing,
   F11 + Alt+Enter hotkeys).  "screen" is the fixed-size logical frame
   RESSCALE scales from; the old largesurface middle-man is gone. */
#define LOGICAL_W (320 * VIEW3D_SCALE)
#define LOGICAL_H (200 * VIEW3D_SCALE)
#else
static SDL_Rect srcrect = {
        0, 0,
        320, 240
    };
static SDL_Rect largesrcrect = {
        0, 0,
        640, 400
    };
static SDL_Rect destrect = {
        0, 0,
        640, 400
    };
#endif
extern T_void KeyboardUpdate(E_Boolean updateBuffers);

#ifdef TARGET_UNIX
static void HandleUnixSignal(int sig)
{
    (void)sig;
    G_done = TRUE;
    /* exit() runs atexit-registered cleanup (including SDL_Quit), which is
       not async-signal-safe -- calling it here can reenter SDL mid-frame
       from signal context and crash (observed: EXC_BAD_ACCESS in
       SDL_VideoQuit while the main thread was inside ResScaleUpdate/
       WindowsUpdate). _exit() terminates immediately without running that
       cleanup, which is safe from a signal handler. */
    _exit(0);
}
#endif

void SleepMS(T_word32 aMS)
{
#ifdef TARGET_UNIX
    SDL_Delay(aMS);
#else
    Sleep(aMS);
#endif
}

void WindowsUpdateMouse(void)
{
    int flags = 0 ;
    int x, y;
    Uint8 state;

#ifdef AA_USE_RESSCALE_PIPELINE
    state = ResScaleGetMouseState(&x, &y);  /* logical LOGICAL_WxLOGICAL_H */
#else
    state = SDL_GetMouseState(&x, &y);
#endif
    DirectMouseSet(x, y);
    if (state & SDL_BUTTON_LMASK)
        flags |= MOUSE_BUTTON_LEFT;
    if (state & SDL_BUTTON_RMASK)
        flags |= MOUSE_BUTTON_RIGHT;
    if (state & SDL_BUTTON_MMASK)
        flags |= MOUSE_BUTTON_MIDDLE;
    DirectMouseSetButton(flags);
}

void WindowsUpdateEvents(void)
{
    int flags;
    SDL_Event event;
    static int altPressed = FALSE;

    while ( SDL_PollEvent(&event) ) {
#ifdef AA_USE_RESSCALE_PIPELINE
        ResScaleEvent(&event);   /* window->logical coords + hotkeys */
#endif
        switch (event.type) {
            case SDL_QUIT:
                G_done = TRUE;
#ifdef TARGET_UNIX
                exit(0);
#endif
                break;
            case SDL_KEYDOWN:
                if ((event.key.keysym.sym == SDLK_LALT) || (event.key.keysym.sym == SDLK_RALT)) {
                    // Left or right alt pressed?
                    altPressed = TRUE;
                } else if ((event.key.keysym.sym == SDLK_RETURN) && (altPressed)) {
                    // ALT-Enter toggles full screen
#ifndef AA_USE_RESSCALE_PIPELINE
                    /* RESSCALE's own ResScaleEvent (above) already handles
                       Alt+Enter itself (see IHandleHotkey in RESSCALE.C),
                       properly updating its own tracked window/shadow
                       surfaces -- this raw fallback would fight it by
                       reassigning `screen` (the RESSCALE-managed logical
                       frame here, not the real window) out from under
                       RESSCALE's own state, so it's for the plain
                       (non-RESSCALE) path only. */
                    flags = screen->flags; /* Save the current flags in case toggling fails */
                    screen = SDL_SetVideoMode(0, 0, 0, screen->flags ^ SDL_FULLSCREEN); /*Toggles FullScreen Mode */
                    if(screen == NULL) screen = SDL_SetVideoMode(0, 0, 0, flags); /* If toggle FullScreen failed, then switch back */
                    if(screen == NULL) exit(1); /* If you can't switch back for some reason, then epic fail */
#endif
                }
                break;
            case SDL_KEYUP:
                if ((event.key.keysym.sym == SDLK_LALT) || (event.key.keysym.sym == SDLK_RALT)) {
                    // Left or right alt released?
                    altPressed = FALSE;
                }
                break;
#if 0
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                DirectMouseSet(event.motion.x, event.motion.y);
                flags = 0;
                if (event.motion.state & SDL_BUTTON_LMASK)
                    flags |= MOUSE_BUTTON_LEFT;
                if (event.motion.state & SDL_BUTTON_RMASK)
                    flags |= MOUSE_BUTTON_RIGHT;
                if (event.motion.state & SDL_BUTTON_MMASK)
                    flags |= MOUSE_BUTTON_MIDDLE;
                DirectMouseSetButton(flags);
                break;
#endif
        }
        SDL_GetKeyState(NULL);
        //keys = SDL_GetKeyState(NULL);

//        if ( keys[SDLK_UP] )    ypos -= 1;
//        if ( keys[SDLK_DOWN] )  ypos += 1;
//        if ( keys[SDLK_LEFT] )  xpos -= 1;
//        if ( keys[SDLK_RIGHT] ) xpos += 1;

//        DrawScene();
    }
}

#define Copy2x(aDest, aSrc) \
        *(aDest++) = *aSrc; \
        *(aDest++) = *(aSrc++);

#define oldCopy2x_4times(aDest, aSrc) \
    Copy2x(aDest, aSrc) \
    Copy2x(aDest, aSrc) \
    Copy2x(aDest, aSrc) \
    Copy2x(aDest, aSrc)

#define Copy2x_4times(aDest, aSrc) \
    v = *((T_word32 *)aSrc); \
    aSrc += 4; \
    aDest[0] = ((T_byte8 *)&v)[0]; \
    aDest[1] = ((T_byte8 *)&v)[0]; \
    aDest[2] = ((T_byte8 *)&v)[1]; \
    aDest[3] = ((T_byte8 *)&v)[1]; \
    aDest[4] = ((T_byte8 *)&v)[2]; \
    aDest[5] = ((T_byte8 *)&v)[2]; \
    aDest[6] = ((T_byte8 *)&v)[3]; \
    aDest[7] = ((T_byte8 *)&v)[3]; \
    aDest += 8;

#define Copy2x_20times(aDest, aSrc) \
    Copy2x_4times(aDest, aSrc) \
    Copy2x_4times(aDest, aSrc) \
    Copy2x_4times(aDest, aSrc) \
    Copy2x_4times(aDest, aSrc) \
    Copy2x_4times(aDest, aSrc)

#define Copy2x_100times(aDest, aSrc) \
    Copy2x_20times(aDest, aSrc) \
    Copy2x_20times(aDest, aSrc) \
    Copy2x_20times(aDest, aSrc) \
    Copy2x_20times(aDest, aSrc) \
    Copy2x_20times(aDest, aSrc)

#define Copy2x_320times(aDest, aSrc) \
    Copy2x_100times(aDest, aSrc) \
    Copy2x_100times(aDest, aSrc) \
    Copy2x_100times(aDest, aSrc) \
    Copy2x_20times(aDest, aSrc)

void WindowsUpdate(char *p_screen, unsigned char *palette)
{
    SDL_Color colors[256];
    int i;
    unsigned char *src = (char *)surface->pixels;
#ifndef AA_USE_RESSCALE_PIPELINE
    unsigned char *dst = (unsigned char *)largesurface->pixels;
    unsigned char *line;
#endif
    static int lastFPS = 0;
    static int fps = 0;
    int y;
    T_word32 tick = clock();
    static T_word32 lastTick = 0xFFFFEEEE;
    static double movingAverage = 0;
#ifndef AA_USE_RESSCALE_PIPELINE
    T_word32 v;
#endif
    T_word32 frac;

#if CAP_SPEED_TO_FPS
        if ((tick-lastTick)<(1000/CAP_SPEED_TO_FPS)) {
SleepMS((1000/CAP_SPEED_TO_FPS) - (tick-lastTick));
        // 10 ms between frames (top out at 100 ms)
    } else
#endif
    {
        lastTick = tick;
//printf("Update: %d (%d)\n", clock(), TickerGet());

    // Setup the color palette for this update
    for (i=0; i<256; i++) {
        colors[i].r = ((((unsigned int)*(palette++))&0x3F)<<2);
        colors[i].g = ((((unsigned int)*(palette++))&0x3F)<<2);
        colors[i].b = ((((unsigned int)*(palette++))&0x3F)<<2);
    }
    //SDL_SetColors(surface, colors, 0, 256);
#ifdef AA_USE_RESSCALE_PIPELINE
    SDL_SetColors(screen, colors, 0, 256);

    /* TRUE-HIGHRES: compose the presentable frame -- natively rendered  */
    /* 3D view + pixel-doubled classic UI + masked overlay on top -- and  */
    /* hand it to RESSCALE, which scales it to the actual window.         */
    if ((HighResFrameWidth() == LOGICAL_W) &&
        (HighResFrameHeight() == LOGICAL_H))  {
        if (View3dHighResGrabFrameDrawn())  {
            HighResCompose((unsigned char *)surface->pixels);
            HighResComposeMaskedOver(
                (unsigned char *)View3dGetOverlayScreen());
        } else  {
            HighResComposeAll((unsigned char *)surface->pixels);
        }
        {
            unsigned char *fr = HighResFrame();
            /* HighResFrame()'s own row-to-row stride, NOT LOGICAL_W: the
               frame buffer is always allocated (and its rows always
               HIGHRES_MAX_STRIDE bytes apart) at HIGHRES_MAX_SCALE
               regardless of the active runtime scale -- see HIGHRES.C's
               own comment on HighResInit. Using LOGICAL_W here (correct
               back when the buffer's real stride matched it 1:1) reads
               every row from the wrong offset whenever the active scale
               is below the max, landing on a real row only 1 time in
               (HIGHRES_MAX_SCALE / scale) and on that row's unwritten
               (zeroed) padding otherwise -- confirmed on real Windows 95
               hardware as periodic black horizontal stripes through the
               entire UI/3D view. LOGICAL_W remains correct as the copy
               *length* -- screen->pixels is genuinely LOGICAL_W bytes
               wide per row -- only the source stride was wrong. */
            size_t frStride = (size_t)HighResFrameStride();
            int yy;

            for (yy = 0; yy < LOGICAL_H; yy++)
                memcpy((unsigned char *)screen->pixels
                           + (size_t)yy * screen->pitch,
                       fr + (size_t)yy * frStride,
                       LOGICAL_W);
        }
    } else  {
        /* Before View3dInitialize (loading screens) the HIGHRES frame    */
        /* does not exist yet: nearest-stretch the classic 320x200 screen */
        /* into the logical frame directly.                               */
        int xx;

        for (y = 0; y < LOGICAL_H; y++)  {
            unsigned char *srow = src
                + (size_t)((y * SCREEN_HEIGHT) / LOGICAL_H) * SCREEN_WIDTH;
            unsigned char *drow = (unsigned char *)screen->pixels
                + (size_t)y * screen->pitch;

            for (xx = 0; xx < LOGICAL_W; xx++)
                drow[xx] = srow[(xx * SCREEN_WIDTH) / LOGICAL_W];
        }
    }

    ResScaleUpdate();
#else
    SDL_SetColors(largesurface, colors, 0, 256);

    // Blit the current surface from 320x200 to 640x400
    line = src;
    for (y=0, frac=0; y<200; y++, line+=320) {
//        for (x=0; x<320; x++) {
//            *(dst++) = *src;
//            *(dst++) = *(src++);
//        }
        while (frac < 400) {
            src = line;
            Copy2x_320times(dst, src);
            frac += 200;
        }
        frac -= 400;
//        for (x=0; x<320; x++) {
//            *(dst++) = *src;
//            *(dst++) = *(src++);
//        }
//        Copy2x_320times(dst, src);
    }

    if (SDL_BlitSurface(largesurface, &largesrcrect, screen, &destrect)) {
        printf("Failed blit: %s\n", SDL_GetError());
    }
    SDL_UpdateRect(screen, 0, 0, 0, 0);
#endif
    fps++;

    if ((tick-lastFPS) >= 1000) {
        if (movingAverage < 1.0)
            movingAverage = fps;
        movingAverage = ((double)fps)*0.05+movingAverage*0.95;
        lastFPS += 1000;
        //printf("%02d:%02d:%02d.%03d FPS: %d, %f\n", tick/3600000, (tick/60000) % 60, (tick/1000) % 60, tick%1000, fps, movingAverage);
        fps = 0;
    }
    WindowsUpdateEvents();
    WindowsUpdateMouse();
    KeyboardUpdate(TRUE) ;
#if CAP_SPEED_TO_100_FPS
    SleepMS(1);
#endif
    }
}


extern T_void game_main(T_word16 argc, char *argv[]);

int SDL_main(int argc, char *argv[])
{
    char *pixels;
    int x, y;
    SDL_Color black = { 0, 0, 0, 0 };
    SDL_Color white = { 255, 255, 255, 0 };
    //SDL_Surface* icon;

#ifndef TARGET_UNIX
    {
        HINSTANCE hLib = LoadLibrary("BlackBox.dll");
        printf("BlackBox hLib = %08X\n", hLib);
    }
#endif
#ifdef TARGET_UNIX
    /* macOS flicker fix (see RESSCALE.C / RESSCALE_MAC.m): RESSCALE creates a
       borderless desktop-sized WINDOW for "fullscreen" on macOS to stay on the
       compositor path (real fullscreen makes the display's content-adaptive
       backlight pulse dark scenes).  sdl12-compat would, by default, promote a
       borderless window that covers the desktop back into a real fullscreen
       surface -- reintroducing the flicker -- so disable that promotion here.
       Read during SDL_SetVideoMode, so set it before then; overwrite=0 lets a
       user override from the environment.  Harmless on non-sdl12-compat SDL. */
    setenv("SDL12COMPAT_FIX_BORDERLESS_FS_WIN", "0", 0) ;
#endif
    if( SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) < 0)
    {
          printf ("Could not initialize SDL: %s\n",SDL_GetError());
          return 1;
    }

    atexit(SDL_Quit);

#ifdef AA_USE_RESSCALE_PIPELINE
    /* RESSCALE opens the real window per resolution.ini (any size,
       fullscreen, letterboxing) and hands back the fixed logical frame
       the game/HIGHRES pipeline fills each frame.                       */
    ResScaleInit();
    /* Must happen before LOGICAL_W/LOGICAL_H are evaluated just below --
       both expand to 320/200 * VIEW3D_SCALE, which is now a runtime
       variable (see 3D_VIEW.H) driven by resolution.ini's detail=
       setting instead of a hardcoded constant. */
    View3dSetActiveScale((T_word16)ResScaleGetDetail());
    screen = ResScaleSetVideoMode(LOGICAL_W, LOGICAL_H, 8, SDL_SWSURFACE);
#else
#ifdef NDEBUG
    screen = SDL_SetVideoMode(640, 400, 32, SDL_HWSURFACE|SDL_DOUBLEBUF|SDL_FULLSCREEN);
#else
    screen = SDL_SetVideoMode(640, 400, 32, SDL_HWSURFACE|SDL_DOUBLEBUF);
#endif
#endif
    SDL_WM_SetCaption("Amulets & Armor", "Amulets & Armor");
    SDL_ShowCursor(SDL_DISABLE);
#ifdef AA_USE_RESSCALE_PIPELINE
    /* Confine the OS cursor to the game window for the whole session --
       previously this only happened while MouseRelativeModeOn (mouselook)
       was active, so the cursor was free to leave the window any time the
       player was in a menu/inventory screen (most of the time in this
       RPG). See MOUSEMOD.C: MouseRelativeModeOn/Off no longer touch grab
       state, since it's now permanent. */
    SDL_WM_GrabInput(SDL_GRAB_ON);
#endif

    if(screen == NULL)
    {
          printf("Could not set video mode: %s\n",SDL_GetError());
          return 1;
    }

#ifdef AA_USE_RESSCALE_PIPELINE
    surface = SDL_CreateRGBSurface(SDL_SWSURFACE, SCREEN_WIDTH, SCREEN_HEIGHT, 8, 0, 0, 0, 0);
#else
    surface = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240, 8, 0, 0, 0, 0);
#endif
    if (surface == NULL) {
        printf("Could not create overlay: %s\n", SDL_GetError());
        return 1;
    }
#ifndef AA_USE_RESSCALE_PIPELINE
    largesurface = SDL_CreateRGBSurface(SDL_SWSURFACE, 640, 400, 8, 0, 0, 0, 0);
    if (largesurface == NULL) {
        printf("Could not create overlay: %s\n", SDL_GetError());
        return 1;
    }
#endif
    SDL_SetColors(surface, &black, 0, 1);
    SDL_SetColors(surface, &white, 255, 1);
    pixels = (char *)surface->pixels;
    GRAPHICS_ACTUAL_SCREEN = (void *)pixels;
#ifdef TARGET_UNIX
    signal(SIGINT, HandleUnixSignal);
    signal(SIGTERM, HandleUnixSignal);
#endif
#ifdef AA_USE_RESSCALE_PIPELINE
    for (y=0; y<SCREEN_HEIGHT; y++) {
        for (x=0; x<SCREEN_WIDTH; x++, pixels++) {
            if ((x == 0) || (x == (SCREEN_WIDTH-1)) || (y == 0) || (y == (SCREEN_HEIGHT-1)))
                *pixels = 255;
            else
                *pixels = 0;
        }
    }
#else
    for (y=0; y<240; y++) {
        for (x=0; x<320; x++, pixels++) {
            if ((x == 0) || (x == 319) || (y == 0) || (y == 239))
                *pixels = 255;
            else
                *pixels = 0;
        }
    }
#endif

    {
#ifndef NDEBUG
#if defined(_MSC_VER)
    int tmpFlag = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );
        tmpFlag |= _CRTDBG_LEAK_CHECK_DF;
    _CrtSetDbgFlag( tmpFlag );
#endif
#endif

        game_main(argc, argv);
    }

    return 0;
}

#ifdef AA_GENERIC_UNIX_MAIN
/* SDL_main.h's own #define main SDL_main only fires on Win32/Mac/etc (see
   its source) -- on a "plain" Unix/X11 target like IRIX nothing renames
   main() for us, and there's no real main() in libSDLmain to provide it
   either (confirmed: ld32 reports libSDLmain.a "not used for resolving
   any symbol" when linked on IRIX). Every other platform this codebase
   targets (Mac/PPC) goes through the macro rename instead, so this must
   stay opt-in via AA_GENERIC_UNIX_MAIN (set only by Build/IRIX-O2's
   Makefile) -- defining a second, separately-named main() unconditionally
   would collide with the macro-renamed one on those platforms. */
int main(int argc, char *argv[])
{
    return SDL_main(argc, argv);
}
#endif
