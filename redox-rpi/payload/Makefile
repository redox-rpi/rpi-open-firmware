# Test payload for the ARM chainloader: installed as "zImage" on the card.
CROSS_COMPILE ?= arm-none-eabi-

zImage: hello.S
	$(CROSS_COMPILE)gcc -nostdlib -nostartfiles -Wl,--build-id=none \
		-Wl,-Ttext=0x2000000 -o hello.elf hello.S
	$(CROSS_COMPILE)objcopy -O binary hello.elf $@

clean:
	rm -f hello.elf zImage
