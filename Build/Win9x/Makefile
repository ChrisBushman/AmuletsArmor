# Cross-compiled Makefile for Windows 95/98/ME (i686, real native Windows
# build -- not TARGET_UNIX). Run on Linux/macOS with a mingw-w64 cross
# toolchain; there is no way to build (or run) this target on the box
# it's actually for. See ../../Lib/SDL_net-1.2.8-src/README-win9x.md and
# ../../Lib/SDL-1.2.15-src/README-win9x.md for why both SDL and SDL_net
# need to be rebuilt from source here rather than reusing the prebuilt
# DLLs in ../../Lib/SDL-1.2.15/ and ../../Lib/SDL_net-1.2.8/ (real
# Windows 95 hardware crashed *inside* the prebuilt SDL.dll).
#
# Toolchain: i686-w64-mingw32-gcc, specifically Ubuntu/Debian's
# gcc-mingw-w64-i686 package (GCC 13, "13-win32" variant). Homebrew's
# mingw-w64 (14.0.0 as of this writing) is UCRT-locked -- even with
# -mcrtdll=msvcrt it still links api-ms-win-crt-*.dll (Windows 10-era
# Universal CRT forwarders, absent on 9x) -- confirmed by compiling a
# trivial program and checking its import table. Ubuntu's package
# defaults to real msvcrt.dll, verified the same way. If you're not
# running this from the project's Docker-based CI/dev flow, make sure
# whatever `i686-w64-mingw32-gcc` is on your PATH actually targets
# msvcrt.dll -- `objdump -p` the output and check for api-ms-win-crt
# imports if anything here seems to silently target the wrong CRT.

REPO_ROOT    := $(shell cd ../.. && pwd)
SRC_DIR      := $(REPO_ROOT)/Source
INC_DIR      := $(REPO_ROOT)/Include
WIN_MAIN_DIR := $(REPO_ROOT)/Build/Windows/VC2013/AA
SDL_SRC_DIR  := $(REPO_ROOT)/Lib/SDL-1.2.15-src
SDL_WINMAIN_DIR := $(REPO_ROOT)/Lib/SDL-1.2.15-winmain
SDLNET_SRC_DIR := $(REPO_ROOT)/Lib/SDL_net-1.2.8-src

BUILD_DIR    := build

CC  := i686-w64-mingw32-gcc
CXX := i686-w64-mingw32-g++

# WINVER/_WIN32_WINNT/_WIN32_WINDOWS = 0x0410: gates mingw's own headers
# down to the Windows 98 API surface (0x0400 covers 95/NT4; 0x0410 is the
# conventional "Windows 98" value and is what the user's real hardware
# target is), so anything genuinely newer fails to *compile* here rather
# than silently linking an import that would fail to *load* on the real
# machine.
#
# __USE_MINGW_ANSI_STDIO=0: modern mingw-w64's "enhanced" ANSI-mode stdio
# replacement generates references to MSVCRT.DLL:_strtoi64/_strtoui64,
# which don't exist on Windows 9x/NT4's msvcrt.dll at all -- confirmed on
# real Windows 95 hardware ("linked to missing export MSVCRT.DLL:
# _strtoui64", AA.exe refusing to even load). Documented mingw-w64 bug
# (sourceforge.net/p/mingw-w64/bugs/991/), fixed upstream in 12.0.0 by
# statically linking mingw's own str* implementations instead of
# depending on the msvcrt export -- Ubuntu 24.04's gcc-mingw-w64-i686
# (GCC 13-win32) predates that fix, so this define is still needed here.
#
# -fno-stack-protector: defensive, kept even after finding the real cause
# below (cheap, no downside on this target).
#
# -march=i486 -mtune=i486: THE fix for a second, separate real-hardware
# bug: every mingw-built binary tested (AA.exe, AAServer.exe, and
# AALauncher.exe -- which shares zero code with the other two) crashed
# with "executed an invalid instruction", all three showing the *same*
# bytes at the crash site (0f 44 f2 eb d7 ...) -- proof it's shared
# compiler-generated startup code, not application logic. 0f 44 is CMOVE,
# a Pentium Pro/P6-family instruction (1995+) absent on 486/Pentium/
# Pentium MMX. i686-w64-mingw32-gcc defaults to -march=pentiumpro with no
# explicit -march (confirmed via `gcc -Q --help=target`), so every build
# here was silently emitting P6-only code regardless of WINVER/
# _WIN32_WINNT. i486 is a conservative, period-correct floor for
# Windows 95-era hardware in general; if you know the test machine's
# exact CPU, a less conservative -march (e.g. pentium, if it's a real
# Pentium/MMX without Pro/II) would perform better -- ask before
# tightening this, since going the other way (assuming too new) is
# exactly the bug this comment describes.
WIN9X_VER_DEFINES := -DWINVER=0x0410 -D_WIN32_WINNT=0x0400 -D_WIN32_WINDOWS=0x0410 \
                      -D__USE_MINGW_ANSI_STDIO=0 -fno-stack-protector \
                      -march=i486 -mtune=i486

# Same defines the real Build/Windows/VC2013/AA/AA.vcxproj uses
# (WIN32/NO_ASSEMBLY/_CRT_SECURE_NO_WARNINGS/_MBCS) -- this is a genuine
# native-Windows build of the same source, just a different compiler.
# Deliberately NOT blanking __cdecl/_cdecl/__stdcall/__fastcall like the
# Linux/macOS CMakeLists.txt does: those are portability shims for
# non-Windows targets where the keywords are meaningless; here, on real
# x86 Windows, mingw's GCC understands them as genuine MS-compatible
# calling-convention attributes and they need to keep their real meaning.
# Bare (no-underscore) "cdecl" is different: it's an old MSVC-only
# spelling (used throughout the legacy HMI Sound Operating System headers,
# Include/SOS*.H/PROFILE.H -- dead code on the SDL-audio path, but still
# textually compiled) that mingw's GCC has never recognized as a keyword
# at all, unlike __cdecl/_cdecl. Blanking it is semantically a no-op on
# i686 Windows either way: cdecl is already GCC's (and MSVC's) default
# calling convention on this target with no annotation at all.
DEFINES := -DWIN32=1 -DNO_ASSEMBLY=1 -D_CRT_SECURE_NO_WARNINGS=1 -D_MBCS=1 -Dcdecl= \
           $(WIN9X_VER_DEFINES) \
           -DSCREEN_WIDTH=320 -DSCREEN_HEIGHT=200

# Mirrors AA_CORE_SOURCES / AA_PLATFORM_SOURCES from the top-level
# CMakeLists.txt (the CMake build's own Windows-shaped source list).
CORE_SOURCES := $(filter-out $(SRC_DIR)/DEBUGBOR.C $(SRC_DIR)/TOSDATA.C $(SRC_DIR)/UIBUTTON.C,$(wildcard $(SRC_DIR)/*.C))
PLATFORM_UPPER_C_SOURCES := $(SRC_DIR)/Win32/RESSCALE.C
PLATFORM_LOWER_C_SOURCES := $(WIN_MAIN_DIR)/main.c
PLATFORM_CXX_SOURCES     := $(WIN_MAIN_DIR)/direct.cpp $(SRC_DIR)/Win32/ipx_client.cpp

UPPER_C_SOURCES := $(CORE_SOURCES) $(PLATFORM_UPPER_C_SOURCES)

UPPER_C_OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(UPPER_C_SOURCES:.C=.C.o)))
LOWER_C_OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(PLATFORM_LOWER_C_SOURCES:.c=.c.o)))
CXX_OBJS     := $(addprefix $(BUILD_DIR)/,$(notdir $(PLATFORM_CXX_SOURCES:.cpp=.cpp.o)))

OBJS := $(UPPER_C_OBJS) $(LOWER_C_OBJS) $(CXX_OBJS)

vpath %.C   $(SRC_DIR) $(SRC_DIR)/Win32
vpath %.c   $(WIN_MAIN_DIR)
vpath %.cpp $(WIN_MAIN_DIR) $(SRC_DIR)/Win32

# SDL_net dir before INC_DIR: this repo's own Include/SDL_net.h is a
# do-nothing compatibility shim (for builds without real SDL_net, same
# one the IRIX-O2 Makefile's own comment describes) -- the real SDL_net
# headers must win the <SDL_net.h>/"SDL_net.h" search over it.
INCLUDES := -I$(SDLNET_SRC_DIR) -I$(INC_DIR) -I$(SRC_DIR) -I$(WIN_MAIN_DIR) -I$(SDL_SRC_DIR)/include

OPT_FLAGS := -O2
DEPFLAGS  := -MMD -MP

ALL_CFLAGS   := $(OPT_FLAGS) $(DEFINES) $(INCLUDES) $(DEPFLAGS)
ALL_CXXFLAGS := $(OPT_FLAGS) $(DEFINES) $(INCLUDES) $(DEPFLAGS)

TARGET := $(BUILD_DIR)/AA.exe

SDL_IMPLIB    := $(BUILD_DIR)/libSDL.dll.a
SDLNET_IMPLIB := $(BUILD_DIR)/libSDL_net.dll.a
SDLNET_DLL    := $(BUILD_DIR)/SDL_net.dll

.PHONY: all clean package

all: $(BUILD_DIR) $(TARGET) $(BUILD_DIR)/SDL.dll

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# --- SDL: built from the vendored source (see
# Lib/SDL-1.2.15-src/README-win9x.md for why -- real Windows 95 hardware
# crashed *inside* the prebuilt SDL.dll). configure's own autoconf cache
# is forced to report strtoll/strtoull as unavailable so SDL compiles its
# own portable SDL_strtoll/SDL_strtoull instead of using mingw's
# strtoull(), which is itself a thin forwarder to
# MSVCRT.DLL:_strtoui64 -- doesn't exist on 9x, root-caused via a linker
# map file during development (see that README for the full story).
# --host=i686-w64-mingw32 alone is enough for configure to auto-detect
# the Windows-native backends (windib/directx video, waveout/dsound
# audio, win32 threads/timer/joystick/cdrom); no extra --enable-* flags
# needed.
$(SDL_SRC_DIR)/build/.libs/SDL.dll $(SDL_SRC_DIR)/build/.libs/libSDL.dll.a &:
	cd $(SDL_SRC_DIR) && \
	CC=i686-w64-mingw32-gcc CFLAGS="$(OPT_FLAGS) $(WIN9X_VER_DEFINES)" \
	./configure --host=i686-w64-mingw32 --enable-shared --disable-static \
	  ac_cv_func_strtoll=no ac_cv_func_strtoull=no \
	  ac_cv_func__strtoll=no ac_cv_func__strtoull=no && \
	$(MAKE) -j$$(nproc)

$(BUILD_DIR)/SDL.dll: $(SDL_SRC_DIR)/build/.libs/SDL.dll | $(BUILD_DIR)
	cp $< $@

$(SDL_IMPLIB): $(SDL_SRC_DIR)/build/.libs/libSDL.dll.a | $(BUILD_DIR)
	cp $< $@

# --- SDL_net: built from the patched vendored source (see
# Lib/SDL_net-1.2.8-src/README-win9x.md) -- deliberately not linking
# -liphlpapi; after the patch, nothing here calls GetAdaptersInfo
# directly anymore, so nothing should statically import IPHLPAPI.DLL
# (confirmed via objdump on the resulting DLL during development).
SDLNET_OBJS := $(addprefix $(BUILD_DIR)/sdlnet-,$(notdir $(patsubst %.c,%.o,$(wildcard $(SDLNET_SRC_DIR)/*.c))))

$(BUILD_DIR)/sdlnet-%.o: $(SDLNET_SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(OPT_FLAGS) $(WIN9X_VER_DEFINES) -I$(SDL_SRC_DIR)/include -I$(SDLNET_SRC_DIR) $(DEPFLAGS) -c $< -o $@

$(SDLNET_DLL) $(SDLNET_IMPLIB) &: $(SDLNET_OBJS) $(SDL_IMPLIB)
	$(CC) -shared -o $(SDLNET_DLL) $(SDLNET_OBJS) $(SDL_IMPLIB) -lws2_32 -Wl,--out-implib,$(SDLNET_IMPLIB)

# --- Real WinMain() wrapper (sets up argc/argv, calls SDL_main()) -- see
# Lib/SDL-1.2.15-winmain/README-win9x.md for why this is compiled from
# source instead of linking the vendored (MSVC-format) SDLmain.lib.
$(BUILD_DIR)/SDL_win32_main.c.o: $(SDL_WINMAIN_DIR)/SDL_win32_main.c | $(BUILD_DIR)
	$(CC) $(OPT_FLAGS) $(WIN9X_VER_DEFINES) -I$(SDL_SRC_DIR)/include $(DEPFLAGS) -c $< -o $@

# --- AA.exe itself
$(TARGET): $(OBJS) $(BUILD_DIR)/SDL_win32_main.c.o $(SDL_IMPLIB) $(SDLNET_IMPLIB)
	$(CC) -o $@ $(OBJS) $(BUILD_DIR)/SDL_win32_main.c.o $(SDL_IMPLIB) $(SDLNET_IMPLIB) -mwindows

$(BUILD_DIR)/%.C.o: %.C | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/%.c.o: %.c | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)
-include $(SDLNET_OBJS:.o=.d)
-include $(BUILD_DIR)/SDL_win32_main.c.d

clean:
	rm -rf $(BUILD_DIR)

# Everything needed to actually run on a real Windows 9x box: the exe plus
# its two runtime DLLs, ready to drop into a copy of Exe/.
package: all $(SDLNET_DLL)
	mkdir -p $(BUILD_DIR)/package
	cp $(TARGET) $(BUILD_DIR)/SDL.dll $(SDLNET_DLL) $(BUILD_DIR)/package/
