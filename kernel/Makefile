CC := gcc
AS := nasm
LD := ld
OBJCOPY := objcopy

CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -Wall -Wextra

# Must match KERNEL_SECTORS in ../boot/stage2.asm.
KERNEL_SECTORS := 8
KERNEL_BYTES := $(shell echo $$(($(KERNEL_SECTORS) * 512)))

all: kernel.bin

kernel_entry.o: kernel_entry.asm
	$(AS) -f elf32 kernel_entry.asm -o kernel_entry.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

kernel.elf: kernel_entry.o kernel.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib -o kernel.elf kernel_entry.o kernel.o

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary kernel.elf kernel.bin
	truncate -s $(KERNEL_BYTES) kernel.bin

clean:
	rm -f kernel_entry.o kernel.o kernel.elf kernel.bin

.PHONY: all clean
