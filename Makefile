# ABYSS PALADIN - builds one ROM per act: act1.gba ... act5.gba
# Requires an arm-none-eabi toolchain (devkitARM on macOS or gcc-arm-none-eabi on Linux) and Python 3 (numpy, Pillow).
PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
PYTHON  ?= python3
LOGO    ?=
CFLAGS  := -mthumb -mthumb-interwork -mcpu=arm7tdmi -O2 -Wall -Wno-unused-function -ffreestanding -fno-strict-aliasing -Ibuild -Isrc
LDFLAGS := -mthumb -mthumb-interwork -mcpu=arm7tdmi -nostartfiles -nostdlib -T src/gba.ld
SRCS    := src/main.c src/world.c src/play.c src/sound.c src/libc.c src/dialog.c build/assets.c build/story.c build/music.s

ACTS := 1 2 3 4 5
ROMS := $(foreach a,$(ACTS),out/act$(a).gba)

all: $(ROMS)

build/assets.c: tools/gen_assets.py tools/art/paladin_sheet.png
	@mkdir -p build
	$(PYTHON) tools/gen_assets.py build
build/assets.h: build/assets.c
	@true

build/story.c: tools/gen_story.py tools/story_ko.txt tools/fonts/Galmuri11-ko.ttf
	@mkdir -p build
	$(PYTHON) tools/gen_story.py build
build/story.h: build/story.c
	@true

build/music.s: tools/gen_music.py
	@mkdir -p build
	$(PYTHON) tools/gen_music.py build
build/music.h: build/music.s
	@true

out/act%.elf: $(SRCS) src/*.h src/crt0.s build/assets.h build/music.h build/story.h
	@mkdir -p out
	$(CC) $(CFLAGS) -DACT=$* $(LDFLAGS) -o $@ src/crt0.s $(SRCS) -lgcc

out/act%.gba: out/act%.elf
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/fixrom.py $@ "ABYSSPAL ACT$*" "ABP$*" $(LOGO)

# re-render the 3D paladin (slow: about a minute) after editing tools/paladin3d.py
render:
	$(PYTHON) tools/paladin3d.py tools/art/paladin_sheet.png

clean:
	rm -rf build out

.SECONDARY:
.PHONY: all clean render
