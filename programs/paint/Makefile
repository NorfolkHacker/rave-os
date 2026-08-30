# Builds paint.bin, the standalone RAVE-OS paint program -- a flat
# binary fixed at 0x00340000 by paint.ld, loadable via
# kernel/kernel.c's program_load_and_run(). Same cross-toolchain and
# base flags as programs/hello/Makefile; requires $HOME/opt/cross/bin
# on PATH.
#
# Nothing keeps kernel/kernel.c's embedded `paint_bin[]` byte array in
# sync with paint.c -- there is no build-system link between this
# Makefile and the kernel's own. After editing paint.c (or font.c),
# re-embedding by hand is required:
#   1. `cd programs/paint && make` to rebuild this paint.bin.
#   2. Regenerate the array body by reading the new paint.bin as hex
#      bytes, 16 per line, comma-separated, matching kernel/kernel.c's
#      existing `paint_bin[]` formatting exactly (a short one-off
#      script beats hand-transcribing -- e.g.
#      `python3 -c "d=open('paint.bin','rb').read(); \
#      print(',\n'.join(', '.join('0x%02x' % b for b in d[i:i+16]) \
#      for i in range(0, len(d), 16)))"`), then paste the result in as
#      the new `paint_bin[]` initializer, replacing the old bytes.
#   3. `rm boot/fs.img` before the next `make fs.img` -- `/BIN/PAINT.BIN`
#      is seeded onto disk with a write-once `fs_create_file()` call in
#      kmain(), so an existing fs.img keeps serving the stale binary
#      forever otherwise, even after the kernel itself is rebuilt.
# The same gap exists for programs/hello/'s `hello_bin[]`; fixing it
# project-wide (a real build-time embed step) is a separate, future
# slice, not done here.

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
