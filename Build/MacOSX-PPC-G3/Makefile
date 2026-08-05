# Native Makefile for PowerPC G3 / Mac OS X 10.3 "Panther".
#
# Separate G3 platform (distinct from Build/MacOSX-PPC, which targets a G4
# Tiger box with Tigerbrew SDL + gcc-14). It pins a Panther-native toolchain --
# Apple's own gcc 4.0 against the 10.3.9 SDK, generic-G3 codegen (-mcpu=750
# -mno-altivec) -- so the game links Panther's system libSystem/libstdc++
# (current version 71.x), NOT Tigerbrew gcc-14's 10.4 C++ runtime (whose
# $UNIX2003/xlocale imports and AltiVec don't exist / can't run on a G3), and
# links a G3-native SDL/SDL_net built from source with the same toolchain
# (see README.md; default prefix ~/aa-g3/prefix). Runs on a G3 as well as G4/G5.
#
# The main Build/MacOSX-PPC/Makefile already notes gcc-4.0 works for this
# codebase (NO_ASSEMBLY=1, no AltiVec assumptions); this platform just makes it
# the pinned default and adds the 10.3 SDK + G3 codegen. Build ON the Tiger box.

REPO_ROOT   := $(abspath ../..)
SRC_DIR     := $(REPO_ROOT)/Source
INC_DIR     := $(REPO_ROOT)/Include
WIN_MAIN_DIR:= $(REPO_ROOT)/Build/Windows/VC2013/AA
UTILS_DIR   := $(REPO_ROOT)/Utils

BUILD_DIR   := build

# := not ?=: CC/CXX are GNU-make *built-in* variables (cc / g++), so ?= would
# never override them and the build would silently use Tigerbrew gcc-14 (=g++ on
# PATH) instead of Apple gcc 4.0. := beats the built-in default but still loses
# to a command-line `make CC=...` override.
CC          := /usr/bin/gcc-4.0
CXX         := /usr/bin/g++-4.0
ARCH        ?= ppc
MACOSX_VERSION_MIN ?= 10.3
SDKROOT     ?= /Developer/SDKs/MacOSX10.3.9.sdk
G3FLAGS     ?= -mcpu=750 -mno-altivec

# G3-native SDL 1.2.15 + SDL_net 1.2.8, built from the vendored source with
# this same toolchain (AltiVec-free, Panther-linked). Point SDL_CONFIG there,
# NOT Tigerbrew's sdl-config on PATH.
G3_PREFIX   ?= $(HOME)/aa-g3/prefix
SDL_CONFIG  ?= $(G3_PREFIX)/bin/sdl-config

SDL_CFLAGS  := $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS    := $(shell $(SDL_CONFIG) --libs) -lSDL_net

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

DEFINES := -DWIN32=1 -DNO_ASSEMBLY=1 -DTARGET_UNIX=1 -DWIN_IPX=1 \
           -DAA_REAL_SDL12=1 \
           -D_CRT_SECURE_NO_WARNINGS=1 -D_MBCS=1 \
           -Dcdecl= -D_cdecl= -D__cdecl= -D__fastcall= \
           -DSCREEN_WIDTH=320 -DSCREEN_HEIGHT=200

ARCHFLAGS := -arch $(ARCH) -mmacosx-version-min=$(MACOSX_VERSION_MIN) $(G3FLAGS)
ifneq ($(strip $(SDKROOT)),)
ARCHFLAGS += -isysroot $(SDKROOT)
endif

INCLUDES := -I$(INC_DIR) -I$(SRC_DIR) -I$(WIN_MAIN_DIR)

OPT_FLAGS   ?= -g -O2
STD_FLAGS   := -std=gnu99

DEPFLAGS     := -MMD -MP
# -fpermissive on C++ only: the 10.3 SDK declares a few libSystem prototypes
# with pre-Tiger signatures (e.g. _NSGetExecutablePath's bufsize is
# `unsigned long *` on 10.3 vs `uint32_t *` on 10.4). The sources are written to
# the Tiger signatures; on 32-bit PPC the types are the same width, so it's
# benign -- C treats it as a warning, C++ as a hard error, which -fpermissive
# downgrades back to a warning. Scoped to this G3 platform.
ALL_CFLAGS   := $(ARCHFLAGS) $(OPT_FLAGS) $(STD_FLAGS) $(DEFINES) $(SDL_CFLAGS) $(INCLUDES) $(DEPFLAGS)
ALL_CXXFLAGS := $(ARCHFLAGS) $(OPT_FLAGS) -fpermissive $(DEFINES) $(SDL_CFLAGS) $(INCLUDES) $(DEPFLAGS)

TARGET      := $(BUILD_DIR)/amulets-armor
DUMP_TARGET := $(BUILD_DIR)/aa-character-dump
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
