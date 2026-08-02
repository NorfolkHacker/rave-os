AS := nasm
QEMU := qemu-system-i386

boot.bin: boot.asm
	$(AS) -f bin boot.asm -o boot.bin

run: boot.bin
	$(QEMU) -drive file=boot.bin,format=raw

clean:
	rm -f boot.bin

.PHONY: run clean
