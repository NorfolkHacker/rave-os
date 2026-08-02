# Rave-OS Build Log

A running record of how Rave-OS gets built: decisions made, concepts learned, and what exists at each stage. This log exists because the project is also how the author is learning x86 assembly and C — code alone doesn't capture the reasoning.

## Vision

Rave-OS is a custom operating system in C, C++, and assembly. Visually it fuses 90s pixel art with rave/acid-graphics (demoscene, Amiga, plasma/hue-cycling palettes). It draws inspiration from TempleOS and the book *Build Your Own Operating System* by Lucas Darnell. Initial target is x86/x64 running as a VM guest.

Ambitions beyond the visuals:
- A mouse-driven GUI (own windowing system).
- Its own dialect of Forth as a core language.
- Self-hosting — Rave-OS eventually recompiles its own source from within itself.

These can't happen all at once, so the project is staged:

1. **Bootable groundwork** — bootloader, minimal kernel, framebuffer output on x86/x64-as-VM.
2. **Mouse-driven GUI / windowing layer.**
3. **Rave-OS's own Forth dialect.**
4. **Self-hosting** (OS recompiles its own source).

ACIDSTORM (`demos/acidstorm/`), a Robotron/Llamatron-inspired twin-stick shooter, is a demo game that ships bundled with Rave-OS — it established the acid pixel-art visual language early on and continues to exist as an included application, not a phase that gets discarded.

## 2026-08-02 — Project kickoff

- Named the project **Rave-OS** and committed to building it as a full OS, not just a series of demos.
- Confirmed ACIDSTORM's role as a bundled demo game rather than a throwaway prototyping stage.
- Chose *Build Your Own Operating System* (Lucas Darnell) and TempleOS as reference points.
- Established the 4-stage roadmap above.
- Started this build log.

Next up: set up an OS-dev toolchain (cross-compiler / freestanding gcc, NASM or GAS, QEMU) inside the `forth-os` distrobox container, separate from the SDL2 userspace toolchain used for ACIDSTORM, and scaffold the first bootable "hello world" stage.

## 2026-08-02 — Toolchain setup (Stage 1 begins)

Decisions made:
- **Compiler:** use the container's existing `gcc` with freestanding flags (`-ffreestanding -nostdlib -fno-pie -m32`) rather than building a dedicated `i686-elf` cross-compiler. Verified this host gcc can compile freestanding 32-bit object code without a separate cross-compiler build — good enough for now; revisit a real cross-compiler only if host-toolchain assumptions cause real problems later.
- **Bootloader:** hand-write our own 16-bit real-mode boot sector in NASM assembly rather than relying on GRUB/Multiboot. More work, but this is where a lot of the x86 assembly learning (BIOS interrupts, the A20 line, GDT setup, the real-mode → protected-mode switch) actually happens, and it fits the from-scratch spirit of the project.
- **Bit width:** start in 32-bit protected mode (the classic path — simpler segmentation/paging, smaller step up from 16-bit real mode) before attempting 64-bit long mode as a later stage.

Installed in the `forth-os` container: `nasm` (assembler), `qemu-full` (provides `qemu-system-i386`/`qemu-system-x86_64` for running the OS as a VM guest).

Next up: write the first boot sector — a 512-byte program BIOS loads at `0x7C00`, printed text via BIOS interrupt `int 0x10`, ending in the `0xAA55` boot-signature — and run it in QEMU.

## 2026-08-02 — First boot sector runs

`boot/boot.asm` is Rave-OS's first piece of real code: a hand-written x86 boot sector, assembled with NASM, that boots successfully in QEMU.

**How a BIOS boot sector works, and what the code does:**
- On power-on, the BIOS (here, QEMU's SeaBIOS) reads the first 512-byte sector off the boot disk into memory at physical address `0x7C00`, and — *only if* the last two bytes of that sector are the magic value `0xAA55` — jumps the CPU to `0x7C00` in **16-bit real mode**. Real mode is the same addressing/execution mode the original 8086 used: 16-bit registers, no memory protection, no paging. Every x86 CPU still starts here for backward compatibility, even 64-bit ones.
- `BITS 16` / `ORG 0x7C00` tell NASM to assemble 16-bit instructions and calculate label addresses as if the code is loaded at `0x7C00` (needed since we use labels like `msg` that must resolve to real addresses).
- `cli` disables interrupts while we point the segment registers (`ds`, `es`, `ss`) at segment 0 and set up a stack (`sp = 0x7C00`, growing downward into free memory below the boot sector) — doing this with interrupts on risks a hardware interrupt firing mid-setup with a half-configured stack. `sti` re-enables interrupts once that's done.
- The message-printing loop uses `lodsb` (load byte at `[ds:si]` into `al`, then auto-increment `si`) to walk the null-terminated string `msg`, calling BIOS interrupt `int 0x10` function `0x0E` ("teletype output") once per character — the simplest way to get text on screen without touching video memory or a driver directly.
- On hitting the string's `0` terminator, it jumps to an infinite `cli` / `hlt` / `jmp` loop — `hlt` stops the CPU until the next interrupt, and looping back to another `hlt` keeps it parked rather than falling through into whatever garbage bytes follow.
- `times 510 - ($ - $$) db 0` pads the file with zero bytes up to offset 510 (`$` is "here", `$$` is the section start, so `$-$$` is bytes emitted so far) — then `dw 0xAA55` writes the boot signature as the final two bytes, giving exactly 512 bytes total. BIOS refuses to boot a sector without this signature.

**Testing approach:** rather than relying on a visible QEMU window, the build uses QEMU's monitor (`-display none -monitor unix:/tmp/qemu-mon.sock,server,nowait`) and sends `screendump /tmp/shot.ppm` over that socket via `socat`, then converts the PPM to PNG (`python-pillow`) to inspect. This is fully scriptable/non-interactive, which matters for later stages too. `imagemagick`'s `convert` was tried first but is currently broken in this container (`GLIBC_2.44 not found` — a version mismatch in the container's package set); `python-pillow` worked as a substitute.

Files: `boot/boot.asm`, `boot/Makefile` (`make` assembles `boot.bin` via `nasm -f bin`; `make run` boots it in `qemu-system-i386`).

Next up: this boot sector currently just prints and halts. The next real milestone is switching from 16-bit real mode into 32-bit protected mode — setting up a Global Descriptor Table (GDT), enabling the A20 line, and making the jump — as the foundation for running actual C kernel code.

## 2026-08-02 — Two-stage bootloader + 32-bit protected mode

A single 512-byte sector isn't enough room for GDT setup, a protected-mode switch, and (eventually) a C kernel, so this stage splits the boot process in two:

- **`boot/stage1.asm`** (renamed from the old `boot.asm`, still the MBR boot sector at `0x7C00`): prints a message, then uses BIOS `int 0x13` function `0x02` ("read sectors", CHS addressing) to read the next `STAGE2_SECTORS` (2) sectors off the boot disk into memory at `0x0000:0x8000`, and far-jumps there. The boot drive number the BIOS passes in `dl` at entry is saved to a variable before the disk-read call clobbers `dl`, since we need it for `int 0x13`.
- **`boot/stage2.asm`** (loaded at `0x8000`, still starts in 16-bit real mode): does the actual real-mode → protected-mode transition:
  1. **Enable the A20 line** — on the original 8086, address bit 20 didn't exist, so addresses wrapped at 1MB; later chips added a gate (disabled by default at boot, for backward compatibility) that must be explicitly enabled to access memory above 1MB. Used the "fast A20" method: set bit 1 of I/O port `0x92`. (One of several historical methods — others go through the keyboard controller or BIOS `int 0x15` — this one is simplest and QEMU supports it.)
  2. **Build a flat GDT** (Global Descriptor Table) — protected mode requires a GDT describing memory segments before it can be entered. Built the minimal legal one: a required null descriptor, one code segment, one data segment, both base `0` / limit `0xFFFFF` with 4K granularity (→ 4GB) and 32-bit flag set, so segmentation is effectively a no-op and segment:offset addresses are just linear addresses.
  3. **Set `CR0.PE`** (bit 0 of control register 0, "Protection Enable") — this is the actual switch that turns on protected mode.
  4. **Far jump to the code segment selector** (`jmp CODE_SEG:protected_mode_entry`) — required immediately after setting `CR0.PE`: it's what flushes the CPU's prefetch queue (which may hold real-mode-decoded instructions) and loads `CS` with the new segment selector; falling through without a far jump leaves the CPU in a broken half-switched state.
  5. Once in 32-bit code (`BITS 32` from here on), load the other segment registers (`ds`/`es`/`fs`/`gs`/`ss`) from the data selector, set up a stack (`esp = 0x90000`), then **write directly to the VGA text-mode framebuffer at physical `0xB8000`** (2 bytes per character: ASCII byte + attribute byte) to prove protected mode is live — BIOS interrupts like `int 0x10` no longer work once real mode is left, since the BIOS's own interrupt handlers are 16-bit real-mode code.

Verified in QEMU via the same screendump technique as the first boot sector: all three expected messages appear (stage1's real-mode print, stage2's real-mode print, and the direct VGA write from 32-bit protected-mode code, visible overwriting SeaBIOS's own banner text at the top-left of the screen — the only way that text could appear there is if the write path used was the direct `0xB8000` framebuffer write, confirming the code executing it was really running in protected mode).

**Rough edge, noted for later:** `STAGE2_SECTORS` is a manually-synchronized constant in `stage1.asm` that must match stage2's actual padded size (currently hardcoded to exactly 1024 bytes via a `times` padding directive in `stage2.asm`) — if stage2 grows past that, both files need updating together. Fine for now; will likely need a more robust approach once stage2 needs to load a variably-sized C kernel.

Files: `boot/stage1.asm`, `boot/stage2.asm`, `boot/Makefile` (`make` builds `disk.img` = `stage1.bin` + `stage2.bin` concatenated; `make run` boots it in `qemu-system-i386`).

Next up: with protected mode working, the next milestone is bringing up a C kernel — compiling a freestanding C file, linking it with a custom linker script to a known address, and having stage2 jump into it instead of just printing and halting.

## 2026-08-02 — First C kernel boots

Added `kernel/` with Rave-OS's first C code, and wired stage2 up to load and jump into it instead of doing its own VGA print.

**Getting a C file to run with no OS underneath it:**
- `kernel/kernel_entry.asm` is a tiny 32-bit asm stub (`_start: call kmain` then halt-loop) that becomes the very first bytes of the kernel binary. A bare C file has no guaranteed "this is where execution starts at this exact address" property on its own — the linker script's `ENTRY(_start)` plus listing `kernel_entry.o` before `kernel.o` on the link line is what pins `_start` to the first byte.
- `kernel/kernel.c` is `kmain()`: writes `"Rave-OS kernel: hello from C!"` directly to the VGA text buffer at `0xB8000` (2 bytes/char: ASCII + color attribute), same technique used in raw asm for the protected-mode proof last stage, now from C. No `printf`, no libc — there isn't one linked in (`-nostdlib`).
- `kernel/linker.ld` places `.text`/`.rodata`/`.data`/`.bss` starting at `0x10000` — this has to match `KERNEL_LOAD_ADDR` in `boot/stage2.asm`, since stage2 jumps to that literal physical address. `.eh_frame`/`.comment`/`.note.*` are explicitly discarded since nothing loads/needs them and they'd otherwise bloat the flat binary.
- Compiled freestanding: `-m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib`. `-ffreestanding` tells gcc not to assume a hosted environment (no standard library, no `main` with argc/argv semantics); `-nostdlib` stops it linking against one; the rest suppress code (stack-protector canaries, exception-unwind tables) that assumes runtime support we don't have.
- `objcopy -O binary kernel.elf kernel.bin` strips all ELF metadata (headers, section table) down to just the raw loadable bytes — the CPU has no idea what an ELF file is; stage2 is just going to jump to a physical address and start executing whatever's there, so it needs to be the bare instruction bytes starting exactly at `_start`. Verified with `objdump -d kernel.elf` that `_start` disassembles at address `0x10000` as expected.

**Loading it off disk:** `boot/stage2.asm` now reads `KERNEL_SECTORS` (8, i.e. 4KB — generous headroom for now) more sectors starting at `KERNEL_START_SECTOR` (4, i.e. right after stage1's 1 sector + stage2's 2 sectors) into `0x1000:0x0000` (physical `0x10000`) via the same BIOS `int 0x13` mechanism as stage1 loading stage2 — still real mode, so BIOS disk services are still available at this point. *Then* it does A20 + GDT + `CR0.PE` + far jump, exactly as before, except the far jump target is now `KERNEL_LOAD_ADDR` (the kernel's `_start`) instead of stage2's own inline print-and-halt code.

**Known rough edge (unchanged from last stage, now touching 3 places instead of 2):** sector counts/offsets are manually-synced constants across `boot/stage1.asm` (`STAGE2_SECTORS`), `boot/stage2.asm` (`KERNEL_START_SECTOR`, `KERNEL_SECTORS`), and `kernel/Makefile` (`KERNEL_SECTORS`, used to pad `kernel.bin` to a whole number of sectors via `truncate`). This is fine while sizes are small and stable, but growing the kernel much further should probably come with a real fix — e.g. a build step that greps the actual compiled sizes and generates these constants automatically, or a stage2 that reads a size header instead of a hardcoded count.

Build: `boot/Makefile`'s `disk.img` target now depends on `kernel/kernel.bin` and invokes `make -C ../kernel` to build it, then concatenates `stage1.bin + stage2.bin + kernel.bin` — 512 + 1024 + 4096 = 5632 bytes total. `make -C boot clean` also cleans the kernel dir.

Verified end-to-end in QEMU: SeaBIOS → stage1 ("loading stage2...") → stage2 ("loading kernel...") → kernel prints its message in green and halts.

Next up: the kernel can currently only write directly to fixed VGA memory offsets. Reasonable next steps: a proper VGA text-mode driver (cursor tracking, scrolling, clear-screen) as a small first abstraction layer, and/or basic keyboard input (reading scancodes off the PS/2 controller) — both are groundwork before anything resembling the roadmap's GUI stage (see the Vision section at the top of this log).

## 2026-08-02 — VGA text-mode driver

Replaced the kernel's one-shot "poke bytes directly at 0xB8000" code with a real driver, `kernel/vga.c` / `kernel/vga.h`, giving `vga_clear()`, `vga_putc()`, `vga_puts()`, and `vga_set_color()`.

What it adds over the raw approach:
- **Cursor tracking** — a `cursor_row`/`cursor_col` pair advances as characters are written, instead of every caller having to compute its own screen offset.
- **Newline handling** — `\n` moves to column 0 of the next row (and `\r` alone just resets the column), rather than every string needing manual positioning.
- **Scrolling** — once `cursor_row` runs past the last row (25 rows in standard VGA text mode), `vga_scroll()` copies every row up by one (row *n* ← row *n+1*, for all rows) and blanks the new bottom row, so output behaves like a normal terminal instead of wrapping/overwriting row 0.
- **Hardware cursor** — VGA has an actual blinking cursor built into the card, controlled by writing a 16-bit position to two of its CRTC (CRT Controller) registers via I/O ports, not memory: index port `0x3D4` selects register `0x0E`/`0x0F` (cursor position high/low byte), then the value is written to data port `0x3D5`. This is the first code needing `outb`, so introduced `kernel/io.h` with `inb`/`outb` wrappers (`inline asm` around the `in`/`out` instructions) — this header will get reused by every future driver that talks to hardware over I/O ports (e.g. the keyboard, next).

`kernel/kernel.c` now just calls into this driver and proves scrolling works by printing 30 lines into a 25-row screen — verified in QEMU that only the most recent ~24-25 lines remain visible, with the earlier header messages correctly scrolled off the top, and the hardware cursor sitting on the new blank bottom row exactly where the next character would land.

Build note: linking now produces a `ld` warning ("LOAD segment with RWX permissions") — expected and harmless at this stage, since paging/segment permissions aren't set up yet, so there's no enforcement to conflict with anyway.

Files: `kernel/vga.c`, `kernel/vga.h`, `kernel/io.h`; `kernel/Makefile` updated to compile and link `vga.o` alongside `kernel.o`.

Next up: keyboard input — reading scancodes off the PS/2 controller (port `0x60`, using the new `inb`) — is the natural next driver, since `io.h` already exists for it and it's the next piece needed before anything interactive.

## 2026-08-02 — PS/2 keyboard driver

Added `kernel/keyboard.c` / `kernel/keyboard.h`: a polling-based PS/2 keyboard driver, and wired `kmain()` up to echo whatever's typed to the VGA driver.

**How PS/2 keyboard input works, no interrupts yet:**
- The keyboard controller has a status port (`0x64`) and a data port (`0x60`). Bit 0 of the status byte is "output buffer full" — set when the controller has a byte ready to read. `keyboard_read_scancode()` just spins (`while (!(inb(0x64) & 1)) {}`) until that bit is set, then reads the byte from `0x60`. This is polling rather than interrupt-driven — simpler to get right first, at the cost of burning CPU while waiting; using the keyboard IRQ instead requires an IDT (Interrupt Descriptor Table), which is a real future stage in its own right (also needed for exceptions, the timer, etc.), not a small addition to this one.
- The byte read is a **scancode**, not ASCII — specifically "scancode set 1", the legacy set the controller still emits by default for compatibility. Pressing a key sends its **make code**; releasing it sends the same value with the top bit set (**break code** = make code `| 0x80`). `keyboard_read_char()` masks that bit off to check `released`, and ignores break codes for every key except shift (see below).
- Two lookup tables (`scancode_to_ascii`, `scancode_to_ascii_shift`), indexed directly by make code, cover the main alphanumeric block (letters, digits, punctuation, space, tab, enter, backspace, escape). Entries of `0` mean "no printable character" — ctrl, alt, capslock, function keys, etc. aren't handled yet, and `keyboard_read_char()` just keeps polling past them rather than returning garbage.
- **Shift is tracked as state, not looked up in the table directly**: `SC_LSHIFT`/`SC_RSHIFT` (`0x2A`/`0x36`) update a `shift_held` flag on press/release instead of producing a character, and every other key's lookup picks the shifted or unshifted table based on that flag. This is the standard approach for any modifier key (ctrl, alt would work the same way) — the hard part of "keyboard input" is this kind of state tracking, not the port I/O itself.

**VGA driver got one addition to support this cleanly:** `vga_putc()` now handles `'\b'` (backspace) by moving the cursor back a column (wrapping to the previous row's last column if already at column 0) and blanking that cell, rather than printing byte `8` as a raw (garbage-looking) glyph.

**Testing an interactive driver without touching a real keyboard:** QEMU's monitor `sendkey` command injects synthetic key events into the guest (e.g. `sendkey a`, `sendkey backspace`, `sendkey shift-a` for a shifted combo) — sent over the same monitor socket used for `screendump`, with a short delay between each command so the guest's polling loop has time to consume each event before the next arrives (sending them with no delay between at all was tried first and didn't register — timing between injected events and the guest's poll loop matters). Verified: typing `a`, `b`, `c`, backspace, `d`, enter, then `shift-a` echoed exactly `abd` on one line and `A` on the next — confirming scancode reading, the backspace erase, and shift-modifier tracking all work correctly together.

Files: `kernel/keyboard.c`, `kernel/keyboard.h`; `kernel/vga.c` (backspace handling added to `vga_putc`); `kernel/kernel.c` now ends in a `keyboard_read_char()` → `vga_putc()` echo loop instead of halting; `kernel/Makefile` updated to build `keyboard.o`.

Next up: this closes out the immediate "bare-metal basics" work (VGA output + keyboard input). Bigger structural pieces still ahead before the GUI stage: an IDT for real interrupt-driven input (and exceptions), a memory allocator, and eventually paging. Worth deciding with the user which of those to tackle next, versus starting to sketch GUI-stage groundwork (VESA/VBE graphics mode) directly.

## 2026-08-02 — VESA/VBE linear framebuffer graphics mode

Jumped ahead to stage 2 (GUI) groundwork: switched the display from VGA text mode to a VESA BIOS Extensions (VBE) linear-framebuffer graphics mode, the foundation any pixel-based GUI needs. This is the first stage where stage2 (still real mode) and the kernel (protected mode) have to cooperate on something neither can fully do alone.

**Why this can't just be a kernel-side driver:** querying and setting a VBE mode requires BIOS `int 0x10` calls, which only work in real mode -- by the time the kernel runs, we're in 32-bit protected mode and the BIOS is unreachable. So the mode switch has to happen in stage2, *before* the protected-mode jump, and the resulting framebuffer details (physical address, pitch, resolution, bit depth) have to be handed to the kernel some other way.

**How the handoff works -- a fixed-address "boot info" struct:**
- `boot/stage2.asm`'s new `setup_video` calls VBE function `0x4F01` ("get mode info") for mode `0x112` (640x480, 32 bits/pixel), which fills a 256-byte `ModeInfoBlock` at a scratch address (`0x9000`). The fields that matter -- `PhysBasePtr` (offset `0x28`, the framebuffer's physical address), `BytesPerScanLine` (`0x10`), `XResolution`/`YResolution` (`0x12`/`0x14`), `BitsPerPixel` (`0x19`) -- get copied into a small packed struct at `0x9500`.
- It then calls VBE function `0x4F02` ("set mode") with mode `0x112 | 0x4000` -- that high bit tells VBE to use linear-framebuffer addressing (a single contiguous memory region) instead of the legacy bank-switched addressing scheme older VBE modes required.
- `kernel/boot_info.h` defines a C struct with the exact same layout at the exact same address (`0x9500`) -- since paging is off and segmentation is flat, both stage2's raw writes and the kernel's struct reads are just talking to the same physical memory, no coordination beyond "same address, same layout" required. This is the same trick as `KERNEL_LOAD_ADDR` from the previous stage: a hardcoded contract between the asm and C sides instead of a real IPC mechanism, because at this level there's no OS yet to provide one.

**The new graphics driver:** `kernel/graphics.c`/`graphics.h` provides `gfx_put_pixel()`, `gfx_clear()`, `gfx_width()`/`gfx_height()`, all reading geometry from `boot_info` rather than hardcoding it. `gfx_put_pixel` computes `framebuffer_addr + y*pitch + x*4` and writes a 32-bit packed color -- assumes XRGB8888 layout, which is what mode `0x112` conventionally provides in QEMU/Bochs VBE, but isn't verified against `RedFieldPosition`/etc. from the ModeInfoBlock yet (noted as a simplifying assumption, not a certainty for all VBE implementations).

**This obsoletes VGA text output for now:** switching video modes means `0xB8000` no longer behaves as VGA text memory, so `vga.c`'s text driver has nothing to write to anymore. `kernel.c` was rewritten to prove the new graphics path instead: it fills the whole 640x480 framebuffer with a classic demoscene XOR-plasma pattern (`r = x*255/w`, `g = y*255/h`, `b = (x^y)&0xFF`) -- cheap to compute, never repeats the same color in a neighboring pixel, and reads as unmistakably "acid," which is the whole visual point of the project. `vga.c` and `keyboard.c` are left in the tree (still compile, keyboard input still technically works) but have no visible output path until a graphics-mode bitmap font renderer exists -- noted as a real gap, not silently dropped.

**Also hardened the build:** `kernel/Makefile`'s `kernel.bin` target now checks the actual compiled size against the `KERNEL_SECTORS` byte budget *before* truncating, and fails loudly instead of silently chopping off real code if the kernel ever grows past it (this was a latent risk since the first two-stage-boot milestone; adding `graphics.o` made it worth fixing now rather than discovering it via a corrupted boot later).

Verified in QEMU via screendump: the full screen renders as a colorful magenta/green/purple XOR pattern with no BIOS text visible at all, confirming the mode switch, the boot_info handoff, and the framebuffer write path all work together correctly.

Files: `kernel/boot_info.h`, `kernel/graphics.c`, `kernel/graphics.h`; `boot/stage2.asm` (`setup_video`); `kernel/kernel.c` rewritten around the graphics driver; `kernel/Makefile` (adds `graphics.o`, size-check guard).

Next up: real text output in graphics mode needs a bitmap font renderer (interestingly, `demos/acidstorm/src/font.c` already has one that could potentially be ported/reused here) -- worth doing before going much further, since debugging future kernel work without any text output at all will get painful fast. After that: mouse input (PS/2 mouse, same controller family as the keyboard), then actual GUI primitives (windows, widgets).
