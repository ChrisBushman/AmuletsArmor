# Native Makefile for IRIX 6.5 / SGI O2 (IP32, MIPS R12000).
# Run this ON the O2 (TGCware's GCC 4.5.2 is the toolchain -- see README.md),
# or via remote-build.sh from a modern dev machine over SSH.
#
# See README.md for toolchain/dependency setup (SDL 1.2.15 / SDL_net 1.2.8
# via Nekoware, at /usr/nekoware).
#
# Login shell on IRIX is csh, and IRIX's own /bin/sh has no `~` expansion --
# this Makefile only assumes POSIX make behavior, not the login shell, so
# that doesn't matter here, but it bit the SSH setup (see README.md).

# $(abspath) needs GNU Make 3.81+ (this box only has 3.74 -- see the
# order-only-prerequisite comment below); $(shell cd .. && pwd) is the
# portable equivalent.
REPO_ROOT   := $(shell cd ../.. && pwd)
SRC_DIR     := $(REPO_ROOT)/Source
INC_DIR     := $(REPO_ROOT)/Include
WIN_MAIN_DIR:= $(REPO_ROOT)/Build/Windows/VC2013/AA
UTILS_DIR   := $(REPO_ROOT)/Utils

BUILD_DIR   := build

# TGCware's GCC 4.5.2 -- not on PATH by default, and IRIX's own /usr/bin/cc
# is MIPSPro, not GCC (this codebase targets GCC's extensions/behavior, per
# the Mac PPC port).
CC          := /usr/tgcware/gcc45/bin/gcc
CXX         := /usr/tgcware/gcc45/bin/g++
SDL_CONFIG  := /usr/nekoware/bin/sdl-config

SDL_CFLAGS  := $(shell $(SDL_CONFIG) --cflags)
# No -lSDLmain: unlike Mac/Windows, IRIX's SDL_main.h doesn't #define main
# SDL_main (that only fires for __WIN32__/__MACOSX__/etc -- see the header
# itself), and libSDLmain.a provides no real main() here either (confirmed:
# ld32 reports it "not used for resolving any symbol"). main.c provides its
# own main() -> SDL_main() wrapper instead, gated on AA_GENERIC_UNIX_MAIN
# below.
SDL_LIBS    := $(shell $(SDL_CONFIG) --libs) -L/usr/nekoware/lib -lSDL_net

# Mirrors AA_CORE_SOURCES / AA_PLATFORM_SOURCES from the top-level CMakeLists.txt
# and Build/MacOSX-PPC/Makefile. UIBUTTON.C excluded for the same reason as
# the PPC build: DEPRECATED (see its own header comment), and its
# UIBUTTON.H's T_buttonHandler typedef conflicts with BUTTON.H's under GCC.
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

# Same defines as the Mac PPC build's Apple branch, minus anything Mac
# specific, plus AA_REAL_SDL12: this build links a genuine SDL 1.2
# (Nekoware's sdl/sdl_net), not a compat shim -- ipx_client.cpp needs to
# know which, since real SDL_net headers live at a bare path, not under
# SDL2/.
DEFINES := -DWIN32=1 -DNO_ASSEMBLY=1 -DTARGET_UNIX=1 -DWIN_IPX=1 \
           -DAA_REAL_SDL12=1 -DAA_GENERIC_UNIX_MAIN=1 \
           -D_CRT_SECURE_NO_WARNINGS=1 -D_MBCS=1 \
           -Dcdecl= -D_cdecl= -D__cdecl= -D__fastcall= \
           -DSCREEN_WIDTH=320 -DSCREEN_HEIGHT=200

INCLUDES := -I$(INC_DIR) -I$(SRC_DIR) -I$(WIN_MAIN_DIR)

OPT_FLAGS   := -g -O2
STD_FLAGS   := -std=gnu99

# -MMD -MP: track header dependencies so editing a shared header (e.g.
# TICKER.H) correctly triggers rebuilding everything that includes it.
# Learned the hard way on the Mac PPC build (see its Makefile's comment) --
# baked in here from the start instead.
DEPFLAGS     := -MMD -MP

# SDL_CFLAGS before INCLUDES: this repo's own Include/SDL_net.h is a
# do-nothing compatibility shim (for builds without real SDL_net); real
# SDL_net -- what this build actually wants -- must win the <SDL_net.h>
# search over it whenever it's genuinely installed (Nekoware's sdl_net
# here, providing full functionality where the shim only stubs
# SDLNet_Init/GetError).
ALL_CFLAGS   := $(OPT_FLAGS) $(STD_FLAGS) $(DEFINES) $(SDL_CFLAGS) $(INCLUDES) $(DEPFLAGS)

# -U__c99: g++ 4.5.2 on this target predefines __c99 (a built-in, not
# something IRIX's own headers set -- `g++ -dM -E -x c++ </dev/null` shows
# it). IRIX's <internal/sgimacros.h> uses that to decide what __restrict
# expands to: `#ifdef __c99 -> restrict (bare C99 keyword) #else -> empty`.
# <internal/string_core.h> (pulled in transitively via SDL.h ->
# SDL_stdinc.h -> sys/types.h -> ...) then declares functions like
# memcpy/strcpy with unnamed `T * __restrict` parameters; g++'s C++
# frontend mis-parses bare `restrict` in that exact unnamed-parameter
# position as the parameter's *name* rather than a qualifier, so two such
# parameters in one prototype collide ("conflicting declaration"). A plain
# `-D__restrict=` doesn't survive this: sgimacros.h's own #define
# overwrites it. Removing the __c99 built-in instead makes sgimacros.h
# take the harmless empty-__restrict branch itself, upstream of the parse
# issue, with no correctness cost (__restrict is only ever an optimisation
# hint).
ALL_CXXFLAGS := $(OPT_FLAGS) $(DEFINES) -U__c99 $(SDL_CFLAGS) $(INCLUDES) $(DEPFLAGS)

TARGET      := $(BUILD_DIR)/amulets-armor
DUMP_TARGET := $(BUILD_DIR)/aa-character-dump

.PHONY: all clean run dump

# Order-only prerequisites (`%.o: %.C | $(BUILD_DIR)`) need GNU Make 3.80+;
# the only make actually available on this O2 is GNU Make 3.74 (1996, from
# a bundled cross-toolchain kit -- no Nekoware make package, and IRIX's own
# make is MIPSPro's, not GNU, so no vpath/pattern-rule support either).
# A parse-time `$(shell mkdir -p $(BUILD_DIR))` doesn't survive `make clean
# all` in one invocation (clean's `rm -rf` runs *after* it, since parse-time
# code only runs once, before any target's recipe) -- make BUILD_DIR a
# normal (not order-only) prerequisite of all/dump instead. That needs
# 3.80+ semantics too in general, but for a plain serial (non -j) build,
# GNU Make has always built prerequisites left-to-right, so this ends up
# reliable here even on 3.74.
all: $(BUILD_DIR) $(TARGET)

dump: $(BUILD_DIR) $(DUMP_TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Link with $(CC) (plain gcc), not $(CXX)/g++: linking as C++ pulls in
# libstdc++, and something about its interaction with pthread static
# initialization crashes with SIGSEGV in pthread_key_create before main()
# even runs on this specific TGCware GCC 4.5.2 / IRIX combination --
# confirmed with a two-line SDL_Init repro compiled identically both ways,
# only the *link* step's driver (g++ vs gcc) determines whether it crashes.
# Our two .cpp files are extern-"C"-wrapped, C-like code with no real C++
# runtime features (no new/delete, exceptions, RTTI, STL), so nothing here
# actually needs libstdc++.
$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(SDL_LIBS) -lm

$(DUMP_TARGET): $(BUILD_DIR)/character_dump_json.c.o
	$(CC) -o $@ $^

$(BUILD_DIR)/character_dump_json.c.o: $(UTILS_DIR)/character_dump_json.c
	$(CC) $(OPT_FLAGS) $(STD_FLAGS) -DTARGET_UNIX=1 -I$(INC_DIR) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.C.o: %.C
	$(CC) $(ALL_CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/%.c.o: %.c
	$(CC) $(ALL_CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)
-include $(BUILD_DIR)/character_dump_json.c.d

clean:
	rm -rf $(BUILD_DIR)

# N32 binaries on IRIX resolve shared libs via LD_LIBRARYN32_PATH, not
# LD_LIBRARY_PATH (see run.csh in the sibling GLQuake IRIX port for the
# same gotcha) -- set both here since `make run` runs under whatever shell
# invoked make, not necessarily csh.
run: all
	cd $(REPO_ROOT)/Exe && \
	LD_LIBRARYN32_PATH=".:/usr/nekoware/lib:/usr/tgcware/lib" \
	LD_LIBRARY_PATH=".:/usr/nekoware/lib:/usr/tgcware/lib" \
	$(REPO_ROOT)/$(TARGET)
