# Builds paint.bin, the standalone RAVE-OS paint program -- a flat
# binary fixed at 0x00200000 by paint.ld, loadable via
# kernel/kernel.c's program_load_and_run(). Same cross-toolchain and
# base flags as programs/hello/Makefile; requires $HOME/opt/cross/bin
# on PATH.

CC := i686-elf-gcc
LD := i686-elf-ld
OBJCOPY := i686-elf-objcopy

CFLAGS := -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -Wall -Wextra

.PHONY: all clean

all: paint.bin

paint.o: paint.c font.h
	$(CC) $(CFLAGS) -c paint.c -o paint.o

font.o: font.c font.h
	$(CC) $(CFLAGS) -c font.c -o font.o

paint.elf: paint.o font.o paint.ld
	$(LD) -m elf_i386 -T paint.ld -nostdlib -o paint.elf paint.o font.o

paint.bin: paint.elf
	$(OBJCOPY) -O binary paint.elf paint.bin

clean:
	rm -f paint.o font.o paint.elf paint.bin
