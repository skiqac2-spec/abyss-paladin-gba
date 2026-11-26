# ABYSS PALADIN - builds one ROM per act: act1.gba ... act5.gba
# Requires an arm-none-eabi toolchain (devkitARM on macOS or gcc-arm-none-eabi on Linux) and Python 3 (numpy, Pillow).
PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
PYTHON  ?= python3
LOGO    ?=
CFLAGS  := -mthumb -mthumb-interwork -mcpu=arm7tdmi -O2 -Wall -Wno-unused-function -ffreestanding -fno-strict-aliasing -Ibuild -Isrc
LDFLAGS := -mthumb -mthumb-interwork -mcpu=arm7tdmi -nostartfiles -nostdlib -T src/gba.ld
SRCS    := src/main.c src/world.c src/play.c src/sound.c src/libc.c build/assets.c

ACTS := 1 2 3 4 5
ROMS := $(foreach a,$(ACTS),out/act$(a).gba)

all: $(ROMS)

build/assets.c build/assets.h: tools/gen_assets.py
	@mkdir -p build
	$(PYTHON) tools/gen_assets.py build

out/act%.elf: $(SRCS) src/*.h src/crt0.s build/assets.h
	@mkdir -p out
	$(CC) $(CFLAGS) -DACT=$* $(LDFLAGS) -o $@ src/crt0.s $(SRCS) -lgcc

out/act%.gba: out/act%.elf
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/fixrom.py $@ "ABYSSPAL ACT$*" "ABP$*" $(LOGO)

clean:
	rm -rf build out

.SECONDARY:
