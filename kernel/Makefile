# i686-elf: a real "no OS" cross-compiler target (see
# toolchain/build-cross.sh), not the host's own gcc approximating
# freestanding output via -m32 -ffreestanding -nostdlib -- removes the
# host-toolchain-version-drift risk and the 32-bit-multilib requirement
# a hosted compiler needed. Must be on PATH (toolchain/build-cross.sh's
# own instructions cover this).
CC := i686-elf-gcc
AS := nasm
LD := i686-elf-ld
OBJCOPY := i686-elf-objcopy

# Quoted #include "foo.h" lines throughout the tree stay bare filenames
# (no "../gfx/" style relative paths) -- these -I flags are what resolve
# them across the arch/drivers/sched/fs/forth/gfx/gui split below, the
# same way a real toolchain's module layout usually works. Object files
# stay flat in kernel/ regardless of which subdirectory their source
# lives in -- one build directory, not one per subsystem.
INCLUDES := -I. -Iarch -Idrivers -Isched -Ifs -Iforth -Igfx -Igui -Iaudio

# No -m32 here anymore -- i686-elf-gcc has no other architecture to
# default to, so it's redundant with a real cross-compiler (it was only
# ever needed to make the host's own hosted gcc target 32-bit instead
# of its native 64-bit). -ffreestanding/-nostdlib stay for explicitness
# even though the target implies them too.
CFLAGS := -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -Wall -Wextra $(INCLUDES)

# -mgeneral-regs-only, isr.o only: GCC's __attribute__((interrupt))
# handlers (isr.c) must never touch FPU/MMX/SSE registers -- there's no
# FPU state save/restore around interrupt entry/exit, so any incidental
# use (even compiler-generated) would corrupt whatever the interrupted
# code was doing with it. Everywhere else this used to blanket-forbid
# float/double kernel-wide for no reason tied to interrupts at all --
# scoped down here (see docs/IDEAS.md's 2026-08-26 floating-point entry)
# once a spike confirmed the one real risk this seemed to guard against
# outside interrupts, FPU state surviving a context_switch() fiber
# swap, doesn't actually exist: the x87 register stack is fully
# call-clobbered per the standard ABI, and GCC always spills any live
# float to memory before a call (context_switch() included) and reloads
# it after -- confirmed by disassembly, not just ABI docs. No FPU
# save/restore was added to sched/context_switch.asm as a result; none
# is needed for compiler-generated code.
ISR_CFLAGS := $(CFLAGS) -mgeneral-regs-only

C_OBJS := kernel.o keyboard.o mouse.o graphics.o font.o text.o idt.o pic.o isr.o window.o button.o textfield.o checkbox.o taskbar.o startmenu.o shell.o console_input.o console_history.o console_output.o forth.o ata.o sb16.o fs.o editor.o serial.o scheduler.o synth.o boot_splash.o paging.o hexfmt.o gdt.o syscall.o syscall_fs.o syscall_gfx.o syscall_audio.o syscall_window.o

all: kernel.bin

kernel_entry.o: arch/kernel_entry.asm
	$(AS) -f elf32 arch/kernel_entry.asm -o kernel_entry.o

context_switch.o: sched/context_switch.asm
	$(AS) -f elf32 sched/context_switch.asm -o context_switch.o

ring3.o: arch/ring3.asm
	$(AS) -f elf32 arch/ring3.asm -o ring3.o

kernel.o: kernel.c gfx/graphics.h gfx/text.h drivers/mouse.h drivers/keyboard.h arch/interrupts.h gui/window.h gui/button.h gui/taskbar.h gui/startmenu.h gui/console_input.h gui/console_history.h gui/console_output.h forth/forth.h gui/shell.h arch/io.h drivers/ata.h fs/fs.h gui/editor.h forth/forth_hooks.h drivers/serial.h sched/scheduler.h drivers/sb16.h audio/synth.h gui/boot_splash.h arch/gdt.h arch/idt.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

keyboard.o: drivers/keyboard.c drivers/keyboard.h
	$(CC) $(CFLAGS) -c drivers/keyboard.c -o keyboard.o

mouse.o: drivers/mouse.c drivers/mouse.h arch/io.h
	$(CC) $(CFLAGS) -c drivers/mouse.c -o mouse.o

idt.o: arch/idt.c arch/idt.h
	$(CC) $(CFLAGS) -c arch/idt.c -o idt.o

pic.o: arch/pic.c arch/pic.h arch/io.h
	$(CC) $(CFLAGS) -c arch/pic.c -o pic.o

isr.o: arch/isr.c arch/idt.h arch/pic.h arch/io.h arch/interrupts.h drivers/keyboard.h drivers/mouse.h gfx/graphics.h gfx/text.h drivers/sb16.h arch/hexfmt.h
	$(CC) $(ISR_CFLAGS) -c arch/isr.c -o isr.o

paging.o: arch/paging.c arch/paging.h
	$(CC) $(CFLAGS) -c arch/paging.c -o paging.o

hexfmt.o: arch/hexfmt.c arch/hexfmt.h
	$(CC) $(CFLAGS) -c arch/hexfmt.c -o hexfmt.o

gdt.o: arch/gdt.c arch/gdt.h
	$(CC) $(CFLAGS) -c arch/gdt.c -o gdt.o

syscall.o: arch/syscall.c arch/syscall.h
	$(CC) $(CFLAGS) -c arch/syscall.c -o syscall.o

syscall_fs.o: arch/syscall_fs.c arch/syscall.h fs/fs.h
	$(CC) $(CFLAGS) -c arch/syscall_fs.c -o syscall_fs.o

syscall_gfx.o: arch/syscall_gfx.c arch/syscall.h gfx/graphics.h
	$(CC) $(CFLAGS) -c arch/syscall_gfx.c -o syscall_gfx.o

syscall_audio.o: arch/syscall_audio.c arch/syscall.h audio/synth.h
	$(CC) $(CFLAGS) -c arch/syscall_audio.c -o syscall_audio.o

syscall_window.o: arch/syscall_window.c arch/syscall.h
	$(CC) $(CFLAGS) -c arch/syscall_window.c -o syscall_window.o

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

boot_splash.o: gui/boot_splash.c gui/boot_splash.h
	$(CC) $(CFLAGS) -c gui/boot_splash.c -o boot_splash.o

forth.o: forth/forth.c forth/forth.h forth/forth_hooks.h
	$(CC) $(CFLAGS) -c forth/forth.c -o forth.o

ata.o: drivers/ata.c drivers/ata.h arch/io.h
	$(CC) $(CFLAGS) -c drivers/ata.c -o ata.o

sb16.o: drivers/sb16.c drivers/sb16.h arch/io.h drivers/serial.h
	$(CC) $(CFLAGS) -c drivers/sb16.c -o sb16.o

synth.o: audio/synth.c audio/synth.h
	$(CC) $(CFLAGS) -c audio/synth.c -o synth.o

fs.o: fs/fs.c fs/fs.h drivers/ata.h
	$(CC) $(CFLAGS) -c fs/fs.c -o fs.o

serial.o: drivers/serial.c drivers/serial.h arch/io.h
	$(CC) $(CFLAGS) -c drivers/serial.c -o serial.o

scheduler.o: sched/scheduler.c sched/scheduler.h
	$(CC) $(CFLAGS) -c sched/scheduler.c -o scheduler.o

kernel.elf: kernel_entry.o context_switch.o ring3.o $(C_OBJS) arch/linker.ld
	$(LD) -m elf_i386 -T arch/linker.ld -nostdlib -o kernel.elf kernel_entry.o context_switch.o ring3.o $(C_OBJS)

# No sector-boundary padding or size budget here anymore -- just the raw
# compiled bytes. boot/Makefile is the sole place that now computes how
# many disk sectors this needs and pads it accordingly, since it's also
# the place that has to know where on disk the kernel starts (previously
# these were manually-synced constants duplicated across three files;
# see docs/BUILD_LOG.md's consolidation entry).
kernel.bin: kernel.elf
	$(OBJCOPY) -O binary kernel.elf kernel.bin

clean:
	rm -f kernel_entry.o context_switch.o ring3.o $(C_OBJS) kernel.elf kernel.bin

.PHONY: all clean
