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
# BOOT=1 (release, no monitor): boot from the SD card at once, as boot64 does
BOOT ?= 0
# MBOX_TRACE=1: log every mailbox request and an "alive" line every 10 s on
# the serial line. On in the monitor build; the release build prints only
# mailbox errors unless built with BOOT=1 MBOX_TRACE=1 (a debug release)
MBOX_TRACE ?= $(MONITOR)
# KERNEL=name: the file a release build (BOOT=1) starts whatever config.txt
# says (kernel= there, default kernel8.img); tests on the selector card,
# which has U-BOOT.BIN, set it
KERNEL ?=
# CONFIG=name: read that file instead of config.txt (Pi tests that must not
# touch the test card's config.txt)
CONFIG ?= config.txt
# HDMI=1: 1080p on HDMI (#442) and the boot screen (#430); on in test
# builds, off in the release build unless asked for (BOOT=1 HDMI=1)
HDMI ?= $(MONITOR)
# CHAIN_IMAGE=path (with MONITOR=1): build that bootcode.bin image in and
# chain it when nobody opens the monitor (boot selector, chain-image.S)
CHAIN_IMAGE ?=
# LOAD_CRC=1: also the CRC-32 of the initramfs on the serial line (tests of
# the SD reads, #34; about 1 s per 10 MB on the VPU)
LOAD_CRC ?= 0

# Broadcom's register headers (BSD) through upstream's glue, as system
# headers so their style does not trip our warnings
FW_DIR ?= ..
INCLUDES := -I. -isystem $(FW_DIR) -isystem $(FW_DIR)/vc4_include

CFLAGS := -std=c11 -Os -ffreestanding -fno-builtin -nostdlib \
	-Wall -Wextra -Werror -Wno-multichar \
	-D__VIDEOCORE4__ -DBUILD_ID='"$(BUILD_ID)"' -DMONITOR=$(MONITOR) \
	-DMBOX_TRACE=$(MBOX_TRACE) -DLOAD_CRC=$(LOAD_CRC) -DBOOT_CONFIG='"$(CONFIG)"' $(INCLUDES)
ASFLAGS := -x assembler-with-cpp -D__VIDEOCORE4__ $(INCLUDES)
LDFLAGS := -nostdlib -nostartfiles -Wl,--build-id=none -T linker.ld

SRCS := start.S main.c clock.c timer.c uart.c printf.c sdram.c libc.c
ifeq ($(MONITOR),1)
SRCS += monitor.c arm.c sd.c fat.c dt.c config.c mbox.c power.c i2c.c boot64.c
ifneq ($(CHAIN_IMAGE),)
SRCS += chain-image.S
CFLAGS += -DCHAIN_IMAGE=1
endif
endif
ifeq ($(BOOT),1)
ifeq ($(MONITOR),1)
$(error BOOT=1 is the release build, without the monitor)
endif
SRCS += arm.c sd.c fat.c dt.c config.c mbox.c power.c i2c.c boot64.c
CFLAGS += -DBOOT=1
ifneq ($(KERNEL),)
CFLAGS += -DBOOT_KERNEL='"$(KERNEL)"'
endif
endif
ifeq ($(HDMI),1)
ifeq ($(MONITOR)$(BOOT),00)
$(error HDMI=1 needs MONITOR=1 or BOOT=1)
endif
SRCS += hdmi.c hvs.c bootscreen.c
CFLAGS += -DHDMI=1
endif
OBJS := $(patsubst %,build/%.o,$(SRCS))

all: build/bootcode.bin

build/%.c.o: %.c hw.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/bootscreen.c.o: font8x16.h
build/boot64.c.o: armstub8-builtin.h

# the built-in armstub8 from ../armstub (needs the aarch64 toolchain there)
armstub-header:
	$(MAKE) -C ../armstub build/armstub8.bin
	tests/armstub-header.sh ../armstub/build/armstub8.bin > armstub8-builtin.h

build/%.S.o: %.S
	@mkdir -p build
	$(CC) $(ASFLAGS) -c $< -o $@

build/chain-image.S.o: chain-image.S $(CHAIN_IMAGE)
	@mkdir -p build
	$(CC) $(ASFLAGS) -DCHAIN_IMAGE_PATH='"$(abspath $(CHAIN_IMAGE))"' -c $< -o $@

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
