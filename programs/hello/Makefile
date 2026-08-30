# Builds hello.bin, the first standalone ring-3 program -- a flat binary
# fixed at 0x00340000 by hello.ld, loadable via kernel/kernel.c's
# program_load_and_run(). Same cross-toolchain and base flags as
# kernel/Makefile; requires $HOME/opt/cross/bin on PATH.

CC := i686-elf-gcc
LD := i686-elf-ld
OBJCOPY := i686-elf-objcopy

CFLAGS := -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -Wall -Wextra

.PHONY: all clean

all: hello.bin

hello.o: hello.c
	$(CC) $(CFLAGS) -c hello.c -o hello.o

hello.elf: hello.o hello.ld
	$(LD) -m elf_i386 -T hello.ld -nostdlib -o hello.elf hello.o

hello.bin: hello.elf
	$(OBJCOPY) -O binary hello.elf hello.bin

clean:
	rm -f hello.o hello.elf hello.bin
