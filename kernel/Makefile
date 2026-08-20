CC := gcc
AS := nasm
LD := ld
OBJCOPY := objcopy

# Quoted #include "foo.h" lines throughout the tree stay bare filenames
# (no "../gfx/" style relative paths) -- these -I flags are what resolve
# them across the arch/drivers/sched/fs/forth/gfx/gui split below, the
# same way a real toolchain's module layout usually works. Object files
# stay flat in kernel/ regardless of which subdirectory their source
# lives in -- one build directory, not one per subsystem.
INCLUDES := -I. -Iarch -Idrivers -Isched -Ifs -Iforth -Igfx -Igui

# -mgeneral-regs-only: GCC's __attribute__((interrupt)) handlers (isr.c)
# must never touch FPU/MMX/SSE registers -- there's no FPU state
# save/restore around interrupt entry/exit, so any incidental use (even
# compiler-generated) would corrupt whatever the interrupted code was
# doing with it. This also just suits a kernel with no FPU context
# switching at all yet.
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mgeneral-regs-only -nostdlib -Wall -Wextra $(INCLUDES)

C_OBJS := kernel.o keyboard.o mouse.o graphics.o font.o text.o idt.o pic.o isr.o window.o button.o textfield.o checkbox.o taskbar.o desktop_icon.o startmenu.o shell.o console_input.o console_history.o console_output.o forth.o ata.o fs.o editor.o serial.o scheduler.o

all: kernel.bin

kernel_entry.o: arch/kernel_entry.asm
	$(AS) -f elf32 arch/kernel_entry.asm -o kernel_entry.o

context_switch.o: sched/context_switch.asm
	$(AS) -f elf32 sched/context_switch.asm -o context_switch.o

kernel.o: kernel.c gfx/graphics.h gfx/text.h drivers/mouse.h drivers/keyboard.h arch/interrupts.h gui/window.h gui/button.h gui/taskbar.h gui/startmenu.h gui/console_input.h gui/console_history.h gui/console_output.h forth/forth.h gui/shell.h arch/io.h drivers/ata.h fs/fs.h gui/editor.h forth/forth_hooks.h drivers/serial.h sched/scheduler.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

keyboard.o: drivers/keyboard.c drivers/keyboard.h
	$(CC) $(CFLAGS) -c drivers/keyboard.c -o keyboard.o

mouse.o: drivers/mouse.c drivers/mouse.h arch/io.h
	$(CC) $(CFLAGS) -c drivers/mouse.c -o mouse.o

idt.o: arch/idt.c arch/idt.h
	$(CC) $(CFLAGS) -c arch/idt.c -o idt.o

pic.o: arch/pic.c arch/pic.h arch/io.h
	$(CC) $(CFLAGS) -c arch/pic.c -o pic.o

isr.o: arch/isr.c arch/idt.h arch/pic.h arch/io.h arch/interrupts.h drivers/keyboard.h drivers/mouse.h gfx/graphics.h gfx/text.h
	$(CC) $(CFLAGS) -c arch/isr.c -o isr.o

graphics.o: gfx/graphics.c gfx/graphics.h boot_info.h
	$(CC) $(CFLAGS) -c gfx/graphics.c -o graphics.o

font.o: gfx/font.c gfx/font.h
	$(CC) $(CFLAGS) -c gfx/font.c -o font.o

text.o: gfx/text.c gfx/text.h gfx/font.h gfx/graphics.h
	$(CC) $(CFLAGS) -c gfx/text.c -o text.o

window.o: gui/window.c gui/window.h gfx/graphics.h gfx/text.h
	$(CC) $(CFLAGS) -c gui/window.c -o window.o

button.o: gui/button.c gui/button.h gfx/graphics.h gfx/text.h
	$(CC) $(CFLAGS) -c gui/button.c -o button.o

textfield.o: gui/textfield.c gui/textfield.h gfx/graphics.h gfx/text.h
	$(CC) $(CFLAGS) -c gui/textfield.c -o textfield.o

checkbox.o: gui/checkbox.c gui/checkbox.h gfx/graphics.h gfx/text.h
	$(CC) $(CFLAGS) -c gui/checkbox.c -o checkbox.o

taskbar.o: gui/taskbar.c gui/taskbar.h gui/window.h gfx/graphics.h gfx/text.h
	$(CC) $(CFLAGS) -c gui/taskbar.c -o taskbar.o

desktop_icon.o: gui/desktop_icon.c gui/desktop_icon.h gui/window.h gfx/graphics.h gfx/text.h
	$(CC) $(CFLAGS) -c gui/desktop_icon.c -o desktop_icon.o

startmenu.o: gui/startmenu.c gui/startmenu.h gfx/graphics.h gfx/text.h
	$(CC) $(CFLAGS) -c gui/startmenu.c -o startmenu.o

shell.o: gui/shell.c gui/shell.h fs/fs.h
	$(CC) $(CFLAGS) -c gui/shell.c -o shell.o

console_input.o: gui/console_input.c gui/console_input.h gfx/graphics.h gfx/text.h
	$(CC) $(CFLAGS) -c gui/console_input.c -o console_input.o

console_history.o: gui/console_history.c gui/console_history.h gui/console_input.h
	$(CC) $(CFLAGS) -c gui/console_history.c -o console_history.o

console_output.o: gui/console_output.c gui/console_output.h gfx/text.h
	$(CC) $(CFLAGS) -c gui/console_output.c -o console_output.o

editor.o: gui/editor.c gui/editor.h gfx/graphics.h gfx/text.h
	$(CC) $(CFLAGS) -c gui/editor.c -o editor.o

forth.o: forth/forth.c forth/forth.h forth/forth_hooks.h
	$(CC) $(CFLAGS) -c forth/forth.c -o forth.o

ata.o: drivers/ata.c drivers/ata.h arch/io.h
	$(CC) $(CFLAGS) -c drivers/ata.c -o ata.o

fs.o: fs/fs.c fs/fs.h drivers/ata.h
	$(CC) $(CFLAGS) -c fs/fs.c -o fs.o

serial.o: drivers/serial.c drivers/serial.h arch/io.h
	$(CC) $(CFLAGS) -c drivers/serial.c -o serial.o

scheduler.o: sched/scheduler.c sched/scheduler.h
	$(CC) $(CFLAGS) -c sched/scheduler.c -o scheduler.o

kernel.elf: kernel_entry.o context_switch.o $(C_OBJS) arch/linker.ld
	$(LD) -m elf_i386 -T arch/linker.ld -nostdlib -o kernel.elf kernel_entry.o context_switch.o $(C_OBJS)

# No sector-boundary padding or size budget here anymore -- just the raw
# compiled bytes. boot/Makefile is the sole place that now computes how
# many disk sectors this needs and pads it accordingly, since it's also
# the place that has to know where on disk the kernel starts (previously
# these were manually-synced constants duplicated across three files;
# see docs/BUILD_LOG.md's consolidation entry).
kernel.bin: kernel.elf
	$(OBJCOPY) -O binary kernel.elf kernel.bin

clean:
	rm -f kernel_entry.o context_switch.o $(C_OBJS) kernel.elf kernel.bin

.PHONY: all clean
