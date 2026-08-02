AS := nasm
QEMU := qemu-system-i386
KERNEL_DIR := ../kernel
KERNEL_BIN := $(KERNEL_DIR)/kernel.bin

all: disk.img

stage1.bin: stage1.asm
	$(AS) -f bin stage1.asm -o stage1.bin

stage2.bin: stage2.asm
	$(AS) -f bin stage2.asm -o stage2.bin

$(KERNEL_BIN): FORCE
	$(MAKE) -C $(KERNEL_DIR)

disk.img: stage1.bin stage2.bin $(KERNEL_BIN)
	cat stage1.bin stage2.bin $(KERNEL_BIN) > disk.img

run: disk.img
	$(QEMU) -drive file=disk.img,format=raw

clean:
	rm -f stage1.bin stage2.bin boot.bin disk.img
	$(MAKE) -C $(KERNEL_DIR) clean

.PHONY: all run clean FORCE
