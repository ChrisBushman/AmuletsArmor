# Native Makefile for PowerPC / Mac OS X 10.4 "Tiger".
# Run this ON the Tiger box (Tigerbrew's gcc is the primary supported
# toolchain -- see README.md; Xcode Tools 2.5's gcc-4.0 also works, override
# CC/CXX for that), or via remote-build.sh from a modern dev machine over
# SSH.
#
# See README.md for toolchain/dependency setup (SDL 1.2.15 / SDL_net 1.2.8,
# preferably via `brew install sdl sdl_net` under Tigerbrew, not the
# vendored Windows libs).

REPO_ROOT   := $(abspath ../..)
SRC_DIR     := $(REPO_ROOT)/Source
INC_DIR     := $(REPO_ROOT)/Include
WIN_MAIN_DIR:= $(REPO_ROOT)/Build/Windows/VC2013/AA
UTILS_DIR   := $(REPO_ROOT)/Utils

BUILD_DIR   := build

CC          ?= gcc
CXX         ?= g++
ARCH        ?= ppc
MACOSX_VERSION_MIN ?= 10.4
SDKROOT     ?=
SDL_CONFIG  ?= sdl-config

SDL_CFLAGS  := $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS    := $(shell $(SDL_CONFIG) --libs) -lSDL_net

# Mirrors AA_CORE_SOURCES / AA_PLATFORM_SOURCES from the top-level CMakeLists.txt.
# UIBUTTON.C is excluded too: its own file header says "DEPRECATED!", nothing
# else in the codebase calls or includes it, and its UIBUTTON.H's
# T_buttonHandler typedef genuinely conflicts with BUTTON.H's (different
# function-pointer signature) once both land in the same translation unit --
# AppleClang tolerates the redefinition, GCC 14 correctly rejects it.
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

# Same defines as the proven-working Intel/ARM macOS CMake build's Apple
# branch, minus anything CMake-specific, plus AA_REAL_SDL12: this build
# links a genuine SDL 1.2 (Tigerbrew's `sdl`/`sdl_net`), not the modern
# build's sdl12-compat + SDL2 combo -- ipx_client.cpp needs to know which,
# since Homebrew's SDL2_net headers live under SDL2/ and Tigerbrew's don't.
DEFINES := -DWIN32=1 -DNO_ASSEMBLY=1 -DTARGET_UNIX=1 -DWIN_IPX=1 \
           -DAA_REAL_SDL12=1 \
           -D_CRT_SECURE_NO_WARNINGS=1 -D_MBCS=1 \
           -Dcdecl= -D_cdecl= -D__cdecl= -D__fastcall= \
           -DSCREEN_WIDTH=320 -DSCREEN_HEIGHT=200

ARCHFLAGS := -arch $(ARCH) -mmacosx-version-min=$(MACOSX_VERSION_MIN)
ifneq ($(strip $(SDKROOT)),)
ARCHFLAGS += -isysroot $(SDKROOT)
endif

INCLUDES := -I$(INC_DIR) -I$(SRC_DIR) -I$(WIN_MAIN_DIR)

OPT_FLAGS   ?= -g -O2
STD_FLAGS   := -std=gnu99

# SDL_CFLAGS before INCLUDES: this repo's own Include/SDL_net.h is a
# do-nothing compatibility shim (for builds without real SDL_net); real
# SDL_net -- what this build actually wants -- must win the <SDL_net.h>
# search over it whenever it's genuinely installed (e.g. Tigerbrew's
# sdl_net here, providing full functionality where the shim only stubs
# SDLNet_Init/GetError).
#
# -MMD -MP: emit a per-object .d dependency file listing every header the
# translation unit actually pulled in, so editing a shared header (e.g.
# TICKER.H) correctly triggers a rebuild of everything that includes it.
# Without this, gmake's pattern rules below only depend on the .C/.cpp
# file itself, and a header-only change silently leaves stale .o files
# linked into the binary -- bit us once already (PERFPROF/TICKER.H wiring
# silently didn't reach most TICKER_TIME_ROUTINE call sites until a full
# `gmake clean`).
DEPFLAGS     := -MMD -MP
ALL_CFLAGS   := $(ARCHFLAGS) $(OPT_FLAGS) $(STD_FLAGS) $(DEFINES) $(SDL_CFLAGS) $(INCLUDES) $(DEPFLAGS)
ALL_CXXFLAGS := $(ARCHFLAGS) $(OPT_FLAGS) $(DEFINES) $(SDL_CFLAGS) $(INCLUDES) $(DEPFLAGS)

TARGET      := $(BUILD_DIR)/amulets-armor
DUMP_TARGET := $(BUILD_DIR)/aa-character-dump
# Standalone network reliability test harness (see Utils/aa_net_test.c and
# the top-level CMakeLists.txt's own aa-net-test target) -- reuses
# ipx_client.cpp's existing object/pattern rule below rather than
# duplicating the toolchain setup in a separate Makefile; doesn't touch
# the real game's own TARGET/OBJS at all.
NET_TEST_TARGET := $(BUILD_DIR)/aa-net-test

.PHONY: all clean run dump net-test

all: $(TARGET)

dump: $(DUMP_TARGET)

net-test: $(NET_TEST_TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(ARCHFLAGS) -o $@ $(OBJS) $(SDL_LIBS)

$(DUMP_TARGET): $(BUILD_DIR)/character_dump_json.c.o
	$(CC) $(ARCHFLAGS) -o $@ $^

$(BUILD_DIR)/character_dump_json.c.o: $(UTILS_DIR)/character_dump_json.c | $(BUILD_DIR)
	$(CC) $(ARCHFLAGS) $(OPT_FLAGS) $(STD_FLAGS) -DTARGET_UNIX=1 -I$(INC_DIR) $(DEPFLAGS) -c $< -o $@

$(NET_TEST_TARGET): $(BUILD_DIR)/aa_net_test.c.o $(BUILD_DIR)/ipx_client.cpp.o
	$(CXX) $(ARCHFLAGS) -o $@ $^ $(SDL_LIBS)

$(BUILD_DIR)/aa_net_test.c.o: $(UTILS_DIR)/aa_net_test.c | $(BUILD_DIR)
	$(CC) $(ARCHFLAGS) $(OPT_FLAGS) $(STD_FLAGS) -DTARGET_UNIX=1 -DAA_REAL_SDL12=1 \
	    $(SDL_CFLAGS) -I$(SRC_DIR)/Win32 $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/%.C.o: %.C | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/%.c.o: %.c | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) -x c -c $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(ALL_CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

-include $(OBJS:.o=.d)
-include $(BUILD_DIR)/character_dump_json.c.d
-include $(BUILD_DIR)/aa_net_test.c.d

clean:
	rm -rf $(BUILD_DIR)

run: all
	cd $(REPO_ROOT)/Exe && $(REPO_ROOT)/$(TARGET)
