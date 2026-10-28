# A PX-42F app or game. Change these two for your own:
NAME := BASIC# the file: build/BASIC.bin, shown in the menu as BASIC
KIND := APPS# GAMES or APPS: the folder it goes in, /BIN/GAMES or /BIN/APPS

# The simulator's key script (sim/sim.c): keys, waits in ms, shot:NAME.
SIM_SCRIPT := shot:start serial:10_FOR_I=1_TO_5 'serial:20_PRINT_I,SQR(I)' serial:30_NEXT serial:RUN 100 shot:run \
              serial:PRINT_0.1+0.2,1/3 'serial:DEG:PRINT_SIN(30)' 100 shot:math serial:LIST 100 shot:list \
              R5C0 DOWN shot:keywords R0C0 R5C0 R2C0 R5C2 R6C4 R5C3 shot:typed ENTER 100 shot:result ON

PICO_TOOLCHAIN ?= $(HOME)/.pico-sdk/toolchain/15_2_Rel1
DRIVE ?= /Volumes/PX-42F

CC := $(PICO_TOOLCHAIN)/bin/arm-none-eabi-gcc
OBJCOPY := $(PICO_TOOLCHAIN)/bin/arm-none-eabi-objcopy
NM := $(PICO_TOOLCHAIN)/bin/arm-none-eabi-nm
SIZE := $(PICO_TOOLCHAIN)/bin/arm-none-eabi-size

SRC := $(wildcard src/*.c)
HEADERS := $(wildcard src/*.h) $(wildcard px42f/*.h)

# The firmware's: Cortex-M33, Thumb, floats in integer registers (softfp).
ARCH := -mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp
CFLAGS := $(ARCH) -std=c11 -Os -Wall -Wextra -ffunction-sections -fdata-sections \
          -fno-unwind-tables -fno-asynchronous-unwind-tables -Ipx42f -Isrc
LDFLAGS := $(ARCH) -T px42f/app.ld -nostartfiles --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,--orphan-handling=error -Wl,--no-warn-rwx-segments -Wl,-Map=build/$(NAME).map

.PHONY: all sim test install clean

all: build/$(NAME).bin

build/$(NAME).elf: $(SRC) px42f/runtime.c px42f/app.ld $(HEADERS) | build
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) px42f/runtime.c -o $@
	$(SIZE) $@

build/$(NAME).bin: build/$(NAME).elf px42f/pxbin.py
	$(OBJCOPY) -O binary -j .image $< build/$(NAME).img
	$(NM) $< > build/$(NAME).sym
	python3 px42f/pxbin.py build/$(NAME).img build/$(NAME).sym $@

# The simulator: the same app code on the Mac. Screenshots in build/sim-out;
# the drive is a copy of sim/drive, in build/sim-drive.
build/sim: $(SRC) $(HEADERS) $(wildcard sim/*.c sim/*.h sim/fonts/*) | build
	cc -std=c11 -g -Wall -Wextra -fsanitize=address,undefined -Ipx42f -Isrc -Isim \
	   -DSIM_APP_DIR='"/BIN/$(KIND)/$(NAME)"' \
	   $(SRC) sim/*.c sim/fonts/*.c -o $@

sim: build/sim
	rm -rf build/sim-out build/sim-drive
	mkdir -p build/sim-drive && cp -R sim/drive/. build/sim-drive/
	build/sim build/sim-out build/sim-drive $(SIM_SCRIPT)

# The language, on the computer (tests/test_basic.c).
test: | build
	cc -std=c11 -g -Wall -Wextra -fsanitize=address,undefined -Ipx42f -Isrc -Isim \
	   tests/test_basic.c src/basic.c sim/sim_dec.c -o build/test_basic
	build/test_basic

# Onto the calculator's drive: Settings -> USB Drive -> Connect to Computer.
install: build/$(NAME).bin
	@test -d "$(DRIVE)/BIN/$(KIND)" || { echo "$(DRIVE)/BIN/$(KIND) not found: connect the PX-42F as a USB drive"; exit 1; }
	cp build/$(NAME).bin "$(DRIVE)/BIN/$(KIND)/"
	@echo "Copied. Eject the drive, then Settings -> Games & Apps."

build:
	mkdir -p build

clean:
	rm -rf build
