AS := nasm
QEMU := qemu-system-i386

all: disk.img

stage1.bin: stage1.asm
	$(AS) -f bin stage1.asm -o stage1.bin

stage2.bin: stage2.asm
	$(AS) -f bin stage2.asm -o stage2.bin

disk.img: stage1.bin stage2.bin
	cat stage1.bin stage2.bin > disk.img

run: disk.img
	$(QEMU) -drive file=disk.img,format=raw

clean:
	rm -f stage1.bin stage2.bin boot.bin disk.img

.PHONY: all run clean
