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

C_OBJS := kernel.o keyboard.o mouse.o graphics.o font.o text.o idt.o pic.o isr.o window.o button.o textfield.o checkbox.o taskbar.o desktop_icon.o console_input.o console_history.o console_output.o forth.o ata.o

all: kernel.bin

kernel_entry.o: kernel_entry.asm
	$(AS) -f elf32 kernel_entry.asm -o kernel_entry.o

kernel.o: kernel.c graphics.h text.h mouse.h keyboard.h interrupts.h window.h button.h textfield.h checkbox.h taskbar.h desktop_icon.h console_input.h console_history.h console_output.h forth.h ata.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

keyboard.o: keyboard.c keyboard.h
	$(CC) $(CFLAGS) -c keyboard.c -o keyboard.o

mouse.o: mouse.c mouse.h io.h
	$(CC) $(CFLAGS) -c mouse.c -o mouse.o

idt.o: idt.c idt.h
	$(CC) $(CFLAGS) -c idt.c -o idt.o

pic.o: pic.c pic.h io.h
	$(CC) $(CFLAGS) -c pic.c -o pic.o

isr.o: isr.c idt.h pic.h io.h interrupts.h keyboard.h mouse.h graphics.h text.h
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

textfield.o: textfield.c textfield.h graphics.h text.h
	$(CC) $(CFLAGS) -c textfield.c -o textfield.o

checkbox.o: checkbox.c checkbox.h graphics.h text.h
	$(CC) $(CFLAGS) -c checkbox.c -o checkbox.o

taskbar.o: taskbar.c taskbar.h window.h graphics.h text.h
	$(CC) $(CFLAGS) -c taskbar.c -o taskbar.o

desktop_icon.o: desktop_icon.c desktop_icon.h window.h graphics.h text.h
	$(CC) $(CFLAGS) -c desktop_icon.c -o desktop_icon.o

console_input.o: console_input.c console_input.h graphics.h text.h
	$(CC) $(CFLAGS) -c console_input.c -o console_input.o

console_history.o: console_history.c console_history.h console_input.h
	$(CC) $(CFLAGS) -c console_history.c -o console_history.o

console_output.o: console_output.c console_output.h text.h
	$(CC) $(CFLAGS) -c console_output.c -o console_output.o

forth.o: forth.c forth.h
	$(CC) $(CFLAGS) -c forth.c -o forth.o

ata.o: ata.c ata.h io.h
	$(CC) $(CFLAGS) -c ata.c -o ata.o

kernel.elf: kernel_entry.o $(C_OBJS) linker.ld
	$(LD) -m elf_i386 -T linker.ld -nostdlib -o kernel.elf kernel_entry.o $(C_OBJS)

# No sector-boundary padding or size budget here anymore -- just the raw
# compiled bytes. boot/Makefile is the sole place that now computes how
# many disk sectors this needs and pads it accordingly, since it's also
# the place that has to know where on disk the kernel starts (previously
# these were manually-synced constants duplicated across three files;
# see docs/BUILD_LOG.md's consolidation entry).
kernel.bin: kernel.elf
	$(OBJCOPY) -O binary kernel.elf kernel.bin

clean:
	rm -f kernel_entry.o $(C_OBJS) kernel.elf kernel.bin

.PHONY: all clean
