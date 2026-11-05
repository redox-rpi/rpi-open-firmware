# 64-bit test payload for lk-overlay's linux-bootloader: /boot/Image-aarch64
CROSS_COMPILE ?= aarch64-linux-gnu-

Image-aarch64: hello64.S
	$(CROSS_COMPILE)gcc -nostdlib -nostartfiles -static -Wl,--build-id=none \
		-Wl,-Ttext=0x1000000 -o hello64.elf hello64.S
	$(CROSS_COMPILE)objcopy -O binary hello64.elf $@

clean:
	rm -f hello64.elf Image-aarch64
