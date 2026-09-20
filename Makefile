# NDSUI - a Nintendo DS styled launcher for the TrimUI Brick (stock OS)
#
#   make          device build (aarch64) with the local WSL cross toolchain
#   make host     native x86_64 build for desktop testing (WSLg)
#   make clean
#
# Toolchain (extracted from the builder image into WSL, no docker at build time):
#   /home/aradhya/toolchains/gcc16-aarch64
#   /home/aradhya/trimui-sysroot

OPT = -O3 -mcpu=cortex-a53
CXXSTD = -std=c++17
WARN = -Wall -Wextra

CROSS_COMPILE ?= /home/aradhya/toolchains/gcc16-aarch64/bin/aarch64-linux-gnu-
SYSROOT ?= /home/aradhya/trimui-sysroot
CXX = $(CROSS_COMPILE)g++

SRCS = $(shell find src -name '*.cpp' | sort)
OBJS = $(patsubst src/%.cpp,dist/obj/%.o,$(SRCS))
OUT = dist/ndsui

INC = -Isrc -I$(SYSROOT)/usr/include -I$(SYSROOT)/usr/include/SDL2
CFLAGS = $(OPT) $(CXXSTD) $(WARN) -fPIC -D_REENTRANT --sysroot=$(SYSROOT)
LIBS = -L$(SYSROOT)/usr/lib -lSDL2 -lSDL2_ttf -lSDL2_image -lasound -lGLESv2 -lEGL -ldl -lpthread

.PHONY: all strip host run clean

all: $(OUT)

$(OUT): $(OBJS)
	@mkdir -p dist
	$(CXX) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

# device binary for shipping: same build, symbols stripped
strip: $(OUT)
	$(CROSS_COMPILE)strip -s $(OUT)

dist/obj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CFLAGS) $(INC) -MMD -MP -c -o $@ $<

# ---- native host build (WSLg desktop testing) -------------------------------
HOST_CXX ?= g++
HOST_INC = -Isrc $(shell pkg-config --cflags sdl2 2>/dev/null)
HOST_LIBS = $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image alsa 2>/dev/null) -lGL -lEGL -ldl -lpthread
HOST_OBJS = $(patsubst src/%.cpp,dist/hobj/%.o,$(SRCS))

dist/hobj/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXSTD) -O2 -g $(WARN) -DHOST_BUILD $(HOST_INC) -MMD -MP -c -o $@ $<

host: $(HOST_OBJS)
	@mkdir -p dist
	$(HOST_CXX) -o dist/ndsui-host $(HOST_OBJS) $(HOST_LIBS)

run: host
	NDS_SDCARD=$(CURDIR)/testdata ./dist/ndsui-host

clean:
	rm -rf dist

# header dependency tracking (a mismatched object layout cost us a long
# debugging session: always rebuild objects whose headers changed)
-include $(OBJS:.o=.d) $(HOST_OBJS:.o=.d)
