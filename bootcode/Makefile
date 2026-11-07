# redox-rpi bootcode: build with the VC4 toolchain from ../redox-rpi/build-toolchain.sh
#   make            -> build/bootcode.bin
#   make CROSS_COMPILE=/path/to/vc4-elf-

CROSS_COMPILE ?= ../toolchains/prefix/bin/vc4-elf-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump

BUILD_ID ?= $(shell git describe --always --dirty 2>/dev/null || echo unknown)
# MONITOR=1: serial monitor for test builds (monitor.c), never in a release
MONITOR ?= 0

# Broadcom's register headers (BSD) through upstream's glue, as system
# headers so their style does not trip our warnings
INCLUDES := -I. -isystem ../firmware -isystem ../firmware/vc4_include

CFLAGS := -std=c11 -Os -ffreestanding -fno-builtin -nostdlib \
	-Wall -Wextra -Werror -Wno-multichar \
	-D__VIDEOCORE4__ -DBUILD_ID='"$(BUILD_ID)"' -DMONITOR=$(MONITOR) $(INCLUDES)
ASFLAGS := -x assembler-with-cpp -D__VIDEOCORE4__ $(INCLUDES)
LDFLAGS := -nostdlib -nostartfiles -Wl,--build-id=none -T linker.ld

SRCS := start.S main.c clock.c timer.c uart.c printf.c sdram.c libc.c
ifeq ($(MONITOR),1)
SRCS += monitor.c
endif
OBJS := $(patsubst %,build/%.o,$(SRCS))

all: build/bootcode.bin

build/%.c.o: %.c hw.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.S.o: %.S
	@mkdir -p build
	$(CC) $(ASFLAGS) -c $< -o $@

build/bootcode.elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) $(OBJS) -lgcc -o $@

build/bootcode.bin: build/bootcode.elf
	$(OBJCOPY) -O binary $< $@
	$(OBJDUMP) -d $< > build/bootcode.dis
	@ls -l $@

# payload for the monitor's g command (see go-test.S)
build/go-test.bin: go-test.S
	@mkdir -p build
	$(CC) $(ASFLAGS) -c $< -o build/go-test.o
	$(OBJCOPY) -O binary build/go-test.o $@

clean:
	rm -rf build

.PHONY: all clean
