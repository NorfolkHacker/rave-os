CC := gcc
AS := nasm
LD := ld
OBJCOPY := objcopy

# -mgeneral-regs-only: GCC's __attribute__((interrupt)) handlers (isr.c)
# must never touch FPU/MMX/SSE registers -- there's no FPU state
# save/restore around interrupt entry/exit, so any incidental use (even
# compiler-generated) would corrupt whatever the interrupted code was
# doing with it. This also just suits a kernel with no FPU context
# switching at all yet.
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mgeneral-regs-only -nostdlib -Wall -Wextra

# Must match KERNEL_SECTORS in ../boot/stage2.asm.
KERNEL_SECTORS := 20
KERNEL_BYTES := $(shell echo $$(($(KERNEL_SECTORS) * 512)))

C_OBJS := kernel.o vga.o keyboard.o mouse.o graphics.o font.o text.o idt.o pic.o isr.o window.o button.o

all: kernel.bin

kernel_entry.o: kernel_entry.asm
	$(AS) -f elf32 kernel_entry.asm -o kernel_entry.o

kernel.o: kernel.c graphics.h text.h mouse.h keyboard.h interrupts.h window.h button.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

vga.o: vga.c vga.h io.h
	$(CC) $(CFLAGS) -c vga.c -o vga.o

keyboard.o: keyboard.c keyboard.h
	$(CC) $(CFLAGS) -c keyboard.c -o keyboard.o

mouse.o: mouse.c mouse.h io.h
	$(CC) $(CFLAGS) -c mouse.c -o mouse.o

idt.o: idt.c idt.h
	$(CC) $(CFLAGS) -c idt.c -o idt.o

pic.o: pic.c pic.h io.h
	$(CC) $(CFLAGS) -c pic.c -o pic.o

isr.o: isr.c idt.h pic.h io.h interrupts.h keyboard.h mouse.h
	$(CC) $(CFLAGS) -c isr.c -o isr.o

graphics.o: graphics.c graphics.h boot_info.h
	$(CC) $(CFLAGS) -c graphics.c -o graphics.o

font.o: font.c font.h
	$(CC) $(CFLAGS) -c font.c -o font.o

text.o: text.c text.h font.h graphics.h
	$(CC) $(CFLAGS) -c text.c -o text.o

window.o: window.c window.h graphics.h text.h
	$(CC) $(CFLAGS) -c window.c -o window.o

button.o: button.c button.h graphics.h text.h
	$(CC) $(CFLAGS) -c button.c -o button.o

kernel.elf: kernel_entry.o $(C_OBJS) linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib -o kernel.elf kernel_entry.o $(C_OBJS)

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary kernel.elf kernel.bin
	@actual=$$(stat -c%s kernel.bin); \
	if [ $$actual -gt $(KERNEL_BYTES) ]; then \
		echo "error: kernel.bin is $$actual bytes, exceeds KERNEL_SECTORS budget of $(KERNEL_BYTES) bytes -- bump KERNEL_SECTORS here and in ../boot/stage2.asm"; \
		exit 1; \
	fi
	truncate -s $(KERNEL_BYTES) kernel.bin

clean:
	rm -f kernel_entry.o $(C_OBJS) kernel.elf kernel.bin

.PHONY: all clean
