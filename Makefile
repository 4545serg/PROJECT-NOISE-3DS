# PROJECT SLITHER - Nintendo 3DS MP3 player
#
#   make        -> project_slither.3dsx (+ .smdh)   (for the Homebrew Launcher)
#   make cia    -> project_slither.cia              (install with FBI)
#   make deps   -> download minimp3.h and stb_image.h into source/
#   make clean
#
# Requires devkitPro with the 3ds-dev group (libctru, citro2d, citro3d).
# `make cia` additionally needs `makerom` and `bannertool` on your PATH.

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO is not set. Install devkitPro and run: export DEVKITPRO=/opt/devkitpro")
endif

DEVKITARM ?= $(DEVKITPRO)/devkitARM
export PATH := $(DEVKITPRO)/tools/bin:$(DEVKITARM)/bin:$(PATH)

TARGET      := project_slither
BUILD       := build
APP_TITLE   := PROJECT SLITHER
APP_DESC    := Native MP3 player
APP_AUTHOR  := PROJECT SLITHER
ICON        := resources/icon.png
BANNER_IMG  := resources/banner.png
BANNER_WAV  := resources/banner.wav
RSF         := resources/app.rsf

CTRULIB  := $(DEVKITPRO)/libctru
PORTLIBS := $(DEVKITPRO)/portlibs/3ds

CC   := $(DEVKITARM)/bin/arm-none-eabi-gcc
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -std=gnu11 -mword-relocations -ffunction-sections -fdata-sections \
           $(ARCH) -D__3DS__ -Isource -I$(CTRULIB)/include -I$(PORTLIBS)/include
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(CTRULIB)/lib -L$(PORTLIBS)/lib
LIBS    := -lcitro2d -lcitro3d -lctru -lm

SRCS := $(wildcard source/*.c)
OBJS := $(patsubst source/%.c,$(BUILD)/%.o,$(SRCS))
HDRS := $(wildcard source/*.h)
DEPS := source/minimp3.h source/stb_image.h

.PHONY: all cia deps clean
all: $(TARGET).3dsx
cia: $(TARGET).cia

deps:
	sh tools/get_deps.sh

$(DEPS):
	sh tools/get_deps.sh

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: source/%.c $(HDRS) $(DEPS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(TARGET).smdh: $(ICON)
	smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" $(ICON) $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $(TARGET).elf $@ --smdh=$(TARGET).smdh

$(BUILD)/banner.bnr: $(BANNER_IMG) $(BANNER_WAV) | $(BUILD)
	bannertool makebanner -i $(BANNER_IMG) -a $(BANNER_WAV) -o $@

$(BUILD)/icon.icn: $(ICON) | $(BUILD)
	bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESC)" -p "$(APP_AUTHOR)" -i $(ICON) -o $@

$(TARGET).cia: $(TARGET).elf $(RSF) $(BUILD)/banner.bnr $(BUILD)/icon.icn
	makerom -f cia -o $@ -elf $(TARGET).elf -rsf $(RSF) -icon $(BUILD)/icon.icn \
	        -banner $(BUILD)/banner.bnr -exefslogo -target t

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh $(TARGET).cia
