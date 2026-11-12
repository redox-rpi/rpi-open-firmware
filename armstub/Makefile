# redox-rpi armstub8 and its test kernel.
# Toolchain: aarch64-none-elf GCC from ../redox-rpi/build-toolchain.sh.
#   make CROSS_COMPILE=/path/to/aarch64-none-elf-

CROSS_COMPILE ?= ../toolchains/prefix/bin/aarch64-none-elf-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
LDFLAGS := -nostdlib -nostartfiles -static -Wl,--build-id=none

all: build/armstub8.bin build/smp-test.bin

build/%.elf: %.S
	@mkdir -p build
	$(CC) $(LDFLAGS) -Wl,-Ttext=0 -o $@ $<
	$(OBJDUMP) -d $@ > build/$*.dis

build/%.bin: build/%.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf build

.PHONY: all clean
