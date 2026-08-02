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

## 2026-08-02 — Graphics-mode font renderer, ported from ACIDSTORM

Restored text output (lost when VGA text mode got replaced by the VBE framebuffer) by porting ACIDSTORM's font system into the kernel, rather than inventing a second, different font for Rave-OS -- the demo game and the OS now share one glyph set.

**What got ported vs. rewritten:**
- `kernel/font.c`/`font.h` is `demos/acidstorm/src/font.c`'s 5x7 dot-matrix glyph data and `font_glyph()` lookup, copied essentially verbatim -- same `G_A`..`G_Z`, `G_0`..`G_9`, punctuation tables. The **only** change: the original calls libc's `toupper()` (via `<ctype.h>`) to normalize case; the kernel has no libc (`-nostdlib`), so that one call would fail at link time. Replaced with a 3-line manual `raveos_toupper()`. This is the whole reason a straight `#include` of the original file wouldn't have worked -- everything else about the glyph format is identical.
- `kernel/text.c`/`text.h` (`text_puts()`, `text_width()`) is new, but deliberately mirrors ACIDSTORM's `gfx_text()`/`gfx_text_width()` (`demos/acidstorm/src/gfx.c`) formula-for-formula: each lit glyph dot becomes a `scale x scale` block, characters advance `6*scale` pixels (5 wide + 1 spacing), width is `n*6*scale - scale`. Kept identical on purpose so a size like "scale 2" means the same thing in both places.
- `kernel/graphics.c` gained `gfx_fill_rect()` (used by `text_puts` per glyph-dot, and now also by `gfx_clear`, which is just `gfx_fill_rect` over the whole screen -- a small simplification alongside the addition).

**Build-safety note:** adding `font.o`/`text.o` pushed the compiled kernel to 4749 bytes, over the previous `KERNEL_SECTORS=8` (4096-byte) budget -- and the size-check guard added last stage (rather than a stale build silently truncating real code) caught it immediately with a clear error. Bumped `KERNEL_SECTORS` to 12 (6144 bytes, headroom for near-term growth) in both `kernel/Makefile` and `boot/stage2.asm` (still the same manually-synced-constants rough edge noted twice already).

`kernel/kernel.c` now draws the XOR-plasma background as before, then overlays a centered white "RAVE-OS" title (scale 4) and a black "KERNEL: FONT RENDERER ONLINE" subtitle (scale 2) using `text_puts`. Verified in QEMU via screendump: both lines render correctly, legible over the plasma backdrop, confirming the font data, `text.c`'s layout math, and `gfx_fill_rect` all work together.

Files: `kernel/font.c`, `kernel/font.h` (ported), `kernel/text.c`, `kernel/text.h` (new); `kernel/graphics.c`/`.h` (`gfx_fill_rect` added); `kernel/kernel.c` (title/subtitle overlay); `kernel/Makefile` (`font.o`, `text.o`, bumped `KERNEL_SECTORS`); `boot/stage2.asm` (bumped `KERNEL_SECTORS` to match).

Next up: mouse input (PS/2 mouse, same controller/IRQ family as the keyboard) is the other GUI-stage essential still missing. After that, actual GUI primitives -- windows, widgets, an event loop -- start becoming meaningful, though an IDT for real interrupts (rather than polling) is worth revisiting before things get much more interactive.

## 2026-08-02 — PS/2 mouse driver, and a real bpp bug caught along the way

Added `kernel/mouse.c`/`mouse.h`: a polling PS/2 mouse driver, same family as the keyboard controller.

**How PS/2 mouse init/reading works:** the mouse is the 8042 controller's "auxiliary device" -- same two I/O ports as the keyboard (`0x60` data, `0x64` status/command), multiplexed. `mouse_init()` does the standard bring-up sequence: `0xA8` ("enable aux port"), read-modify-write the controller's configuration byte (`0x20`/`0x60` commands) to enable IRQ12 and the mouse clock, then `0xD4`-prefixed mouse commands `0xF6` ("set defaults") and `0xF4` ("enable data reporting"), each acknowledged with `0xFA`. Once streaming, the mouse sends 3-byte packets unprompted: byte 0's bits carry button states + sign bits for the X/Y deltas in bytes 1-2 (which need sign-extending from 8 bits to a real `int`), and bit 3 of byte 0 is always 1 -- used to resync if a read ever starts mid-packet. Status port bit 5 (`PS2_STATUS_AUX_DATA`) tells a waiting byte apart from a keyboard byte, since both devices' data arrives through the same output buffer; `read_mouse_byte()` discards anything not tagged as aux data rather than misreading a keystroke as mouse data.

**The bug this stage actually caught:** to visually prove the mouse worked, `kernel.c` needed a movable cursor (drawn via `gfx_fill_rect`, erased by recomputing the plasma backdrop color at the old position). Testing this in QEMU via `mouse_move`/`mouse_button` monitor commands showed no visible cursor at all -- and closer inspection of the *background* plasma pattern (present since the VESA/VBE milestone two entries up) showed it didn't match the intended XOR formula under any color-channel permutation, and even the on-screen text was rendering with visible RGB fringing once compared pixel-by-pixel. Root cause, found by temporarily rendering `boot_info`'s raw fields as hex text on screen (the font renderer paid for itself immediately here): `boot_info->pitch` was `0x780` = 1920 = `640*3`, not `640*4` -- meaning VBE mode `0x112` on this BIOS/QEMU actually reports **`BitsPerPixel = 24`**, not the 32 its standard VESA definition would suggest. `gfx_put_pixel()` had hardcoded a 4-byte word write at a 4-bytes/pixel stride; against an actual 3-bytes/pixel framebuffer, every single pixel write bled into the next pixel's bytes, corrupting the entire screen in a way that (by coincidence) still looked like a plausible, colorful "acid" pattern -- which is exactly why it went unnoticed through two prior milestones' screenshots.

**Fix:** `gfx_put_pixel()` now reads `boot_info->bpp` and writes exactly `bpp/8` bytes per pixel (byte-by-byte: blue, green, red, and a 4th padding byte only if `bytes_per_pixel >= 4`), instead of assuming a fixed 32-bit format. This is a more correct driver in general -- real VBE implementations do vary in reported bit depth -- not just a patch for this one BIOS's quirk.

Re-verified in QEMU with the same `mouse_move`/`mouse_button` sequence as before the fix: the plasma backdrop now renders as clean smooth gradients (no RGB fringing), the title/subtitle text is crisp, and a red 8x8 cursor square appears exactly where the simulated movements (right 100, down 60, left 40, then left-button-press) should place it -- confirming both the color fix and the mouse driver (movement, sign-extension, and button-state decoding) all work correctly together.

Files: `kernel/mouse.c`, `kernel/mouse.h` (new); `kernel/graphics.c` (`gfx_put_pixel` bpp fix); `kernel/kernel.c` (cursor tracking + plasma-restore erase, driven by `mouse_read_packet`); `kernel/Makefile` (`mouse.o`).

Next up: an IDT for real interrupts (keyboard/mouse currently both busy-poll, burning CPU and unable to do anything else concurrently) is the biggest structural gap before GUI primitives (windows, widgets, an event loop) become meaningful -- worth tackling before adding much more polling-based input/output code.

## 2026-08-02 — IDT, PIC, and interrupt-driven keyboard/mouse

Converted keyboard and mouse from busy-polling to real hardware interrupts: new `kernel/idt.c`/`idt.h` (Interrupt Descriptor Table), `kernel/pic.c`/`pic.h` (8259 PIC driver), and `kernel/isr.c` (the actual handlers + wiring).

**The IDT** is protected mode's equivalent of the real-mode interrupt vector table BIOS calls used back in `boot/stage2.asm` (`int 0x10`, `int 0x13`) -- a 256-entry table mapping vector number to handler address, loaded via the `lidt` instruction (the same idea as `lgdt` for the GDT, just a different table). `idt_set_gate()` builds one entry (handler address split into two 16-bit halves for historical reasons, a segment selector, and a type/flags byte); `KERNEL_CODE_SEGMENT` (`0x08`) has to match `CODE_SEG` from `boot/stage2.asm`'s GDT, since that's the segment already loaded into `CS` by the time kernel code runs.

**The PIC (8259)** is the chip that actually routes hardware IRQ lines to the CPU. Two of them exist (cascaded: the second/"slave" PIC's output feeds into the first/"master" PIC's IRQ2 line), and by power-on default they deliver IRQ0-15 at interrupt vectors `0x08`-`0x0F`/`0x70`-`0x77` -- which collide with CPU exception vectors (0-31) and had to be **remapped** (`pic_remap()`, the standard 4-command "ICW" initialization sequence) to `0x20`-`0x2F` instead. After remapping, every IRQ line is masked; callers unmask (`pic_clear_mask()`) only the ones they have a real handler for.

**Handlers use GCC's `__attribute__((interrupt))`** rather than hand-written assembly stubs: given a function shaped `void handler(struct interrupt_frame *frame)` (or with a leading `unsigned int error_code` for the handful of CPU exceptions that push one -- fixed by the architecture, encoded in `exception_has_error_code()`), the compiler generates the register save/restore and `iret` a real handler needs. Added `-mgeneral-regs-only` to `CFLAGS` globally, per GCC's own recommendation for these handlers -- there's no FPU/SSE state save around interrupt entry/exit, so any incidental use (even compiler-generated) would corrupt whatever the interrupted code was doing with those registers. The one thing this technique can't do: a handler has no way to know *which* vector invoked it (the CPU doesn't pass that in), so the generic exception/unused-IRQ catch-alls can halt-safely or send EOI, but can't report which specific exception or IRQ actually fired -- a real per-vector stub in assembly (pushing its own vector number before jumping to a shared dispatcher) would be needed for that. Noted as a rough edge, not fixed here, since the two IRQs that matter (1, 12) get real dedicated handlers regardless.

**keyboard.c and mouse.c** changed from "poll a port until a byte shows up" to "read from a small ring buffer that the IRQ handler fills, `hlt`-waiting (not busy-spinning) when it's empty" -- `keyboard_irq_push_scancode()`/`mouse_irq_push_byte()` are the only new surface the handlers in `isr.c` call. The scancode-to-ASCII translation and mouse packet decoding logic is unchanged from the polling versions. A nice side effect: with real separate IRQ1/IRQ12 vectors, the hardware itself now keeps the keyboard and mouse byte streams apart, so `mouse.c` no longer needs the old polling version's manual "check status port bit 5" disambiguation -- interrupts solve, in hardware, a problem software had to work around before.

**A real bug this caught:** `mouse_init()`'s handshake (still polling-based, run with IRQ12 masked so it can't race the new handler) reads two ACK bytes (`0xFA`) directly. First attempt at wiring this up produced a mouse cursor that jumped to a wildly wrong position, decoding a definitely-bogus packet. Tracing raw bytes (temporarily rendering them as hex via the font renderer -- paying for itself again) showed the very first "packet" byte read after enabling interrupts was `0xFA` -- the ACK byte, not a movement byte at all, but coincidentally also has bit 3 set (the bit `mouse_read_packet`'s resync check uses to recognize "start of a real packet"), so it slipped past unnoticed. Root cause: 8259 PICs latch an IRQ line's edge into their Interrupt Request Register even while that line is masked -- so polling a byte directly (bypassing the handler) while IRQ12 was masked left a *stale pending request* that fired the instant `interrupts_enable()` unmasked it, well after the byte it corresponded to had already been consumed. **Fix:** both `irq1_keyboard` and `irq12_mouse` now check the status port's "output buffer full" bit before trusting `inb(0x60)`, turning a spurious fire into a harmless no-op instead of a phantom byte. Re-verified with the same `mouse_move`/`mouse_button` sequence as the previous milestone: cursor lands exactly where expected again.

Build note: adding three more source files pushed the kernel past its `KERNEL_SECTORS` budget again (caught immediately by last stage's size-check guard, as intended) -- bumped from 12 to 20 sectors (10240 bytes) in both `kernel/Makefile` and `boot/stage2.asm`.

Files: `kernel/idt.c`, `kernel/idt.h`, `kernel/pic.c`, `kernel/pic.h`, `kernel/isr.c`, `kernel/interrupts.h` (new); `kernel/keyboard.c`/`.h`, `kernel/mouse.c`/`.h` (polling -> ring-buffer + IRQ push); `kernel/kernel.c` (calls `interrupts_init()` / `mouse_init()` / `interrupts_enable()` in that order, for the masking reason above); `kernel/Makefile` (`idt.o`/`pic.o`/`isr.o`, `-mgeneral-regs-only`, bumped `KERNEL_SECTORS`); `boot/stage2.asm` (bumped `KERNEL_SECTORS` to match).

Next up: actual GUI primitives (a window/rectangle abstraction, simple widgets, an event loop tying keyboard+mouse+graphics together) can now build on real interrupt-driven input. A proper compositor/backbuffer (flagged as a rough edge back in the mouse-driver stage) is worth doing before that gets far, so cursor/window movement doesn't corrupt whatever's underneath.

## 2026-08-02 — Offscreen backbuffer + present, real cursor compositing

Replaced "draw straight to video memory, and 'erase' by recomputing what the background formula should be" with a proper offscreen backbuffer and a single `gfx_present()` step that flips it to the real screen -- fixing, at the root, the cursor-corrupts-text bug flagged (and accepted, at the time) back in the mouse-driver stage.

**A discovery that shaped the design:** a backbuffer for 640x480x32bpp is ~1.2MB -- too big to just declare as a static C array without checking what that actually costs. Two problems surfaced on inspection:
1. `objcopy -O binary` **drops `.bss` entirely** from the flat binary -- confirmed empirically (`readelf -l kernel.elf` showed `FileSiz=0x1c0d` vs `MemSiz=0x24e6`; the raw binary came out exactly `0x1c0d` bytes, not `0x24e6`). This means the kernel has only ever "worked" so far because whatever RAM stage2 happened to hand it read as zero for the small amount of `.bss` in use (ring buffers, the IDT table, etc.) -- not because anything actually zeroed it. A large `.bss` array would inherit garbage instead of zeros.
2. Even setting that aside, a 1.2MB static array appended after the kernel's `.data`/`.bss` (loaded at `0x10000`) would push the kernel's memory footprint past `0x90000` -- exactly where `boot/stage2.asm` parks the stack. The backbuffer and the stack would overlap.

**Resolution:** the backbuffer lives at a fixed physical address (`0x200000`, 2MB -- comfortably clear of both the loaded kernel and the stack) accessed via a raw pointer, the same pattern `boot_info.h` already uses for `BOOT_INFO_ADDR` -- not a C array, so it costs the linker nothing and sidesteps the `.bss` question entirely. `gfx_init()` explicitly zeroes it at startup rather than assuming anything about the memory's initial contents. Proper `.bss` zeroing (via linker-provided `__bss_start`/`__bss_end` symbols, zeroed in `kernel_entry.asm` before `kmain`) is still not done -- noted here as a real gap now that it's been identified, deferred since the backbuffer's fixed-address approach doesn't need it to work correctly.

**The architecture:** `gfx_put_pixel`/`gfx_get_pixel`/`gfx_fill_rect`/`gfx_clear` all now read/write the backbuffer -- a plain, hardware-agnostic 32-bit-per-pixel array -- instead of touching video memory directly. This is simpler than before in its own right: the bpp-awareness `gfx_put_pixel` needed since the last bug fix (writing `bpp/8` bytes at the correct stride) moved to the one place that actually needs it, `gfx_present()`, which walks the whole backbuffer and writes it out in whatever format `boot_info->bpp` says the hardware wants. Every other drawing function got simpler by not needing to know hardware format at all.

**Cursor compositing** (`kernel.c`) changed from `erase_cursor()` recomputing the plasma formula to real `save_under_cursor()`/`restore_under_cursor()`: before drawing the cursor somewhere, remember the actual backbuffer pixels there (via the new `gfx_get_pixel`); before moving it, put those exact pixels back. This is correct regardless of what's underneath -- plasma, text, or (later) a window -- because it never has to guess; it just remembers what was really there. `kmain` now builds the whole scene (plasma, title, subtitle, cursor) into the backbuffer and calls `gfx_present()` once per frame (once after the initial draw, once per mouse packet) rather than every draw call being immediately visible.

**Verified in QEMU:** moved the cursor directly over the "O" in "COMPOSITOR" and the ":" in "KERNEL:" (via `mouse_move`), screendumped mid-sweep (cursor visibly sitting on the text), then moved away and screendumped again -- the text renders perfectly intact, no corruption, unlike what the old plasma-recompute approach would have left behind.

**Known tradeoff, not yet addressed:** `gfx_present()` blits the entire 640x480 backbuffer every time, even though only an 8x8 cursor moved -- correct but not efficient. A real compositor would track a damage rectangle (here, just the union of the cursor's old and new position) and only blit that. Deferred until it's an actual observed problem rather than a theoretical one, per not building for hypothetical requirements -- but noted here since window/widget work will make this more relevant, not less.

Files: `kernel/graphics.c`/`.h` (backbuffer, `gfx_init`, `gfx_get_pixel`, `gfx_present`); `kernel/kernel.c` (`save_under_cursor`/`restore_under_cursor` replacing `erase_cursor`, `gfx_present()` calls).

Next up: actual GUI primitives (window/rectangle abstraction, simple widgets, an event loop) can now build on a correct compositing foundation. Proper `.bss` zeroing (flagged above) and damage-rect present are both reasonable to pick up whenever they start actually mattering.

## 2026-08-02 — First GUI primitives: windows, a button, a real event loop

Added `kernel/window.c`/`.h` (a titled rectangle) and `kernel/button.c`/`.h` (a clickable, hoverable rectangle with a label) -- the first actual widgets -- plus a real event loop in `kernel.c` that merges keyboard and mouse input instead of blocking exclusively on one.

**The event loop problem:** `keyboard_read_char()` and `mouse_read_packet()` both worked by blocking (via `hlt`) until their specific device produced something. That's fine for a demo exercising one input at a time, but a real GUI needs to react to *either* input, whichever arrives first, without starving the other. Fixed by splitting each driver into a non-blocking `poll` primitive (`keyboard_poll_char()`, `mouse_poll_packet()` -- return 1 if something was available and already decoded, 0 immediately if not) with the existing blocking function rebuilt on top for callers that still want it. `kmain`'s loop now checks both every iteration, redraws and calls `gfx_present()` if either produced something, and only actually `hlt`s when *neither* did -- the CPU still goes idle between input, just without picking a single device to idle-wait on.

`mouse_poll_packet()` has one small wrinkle: once it finds a valid packet-start byte (bit 3 set), it does briefly `hlt`-wait for that packet's other 2 bytes rather than bailing out non-blockingly -- otherwise, on a partial read, the next `poll_packet()` call would resync onto whatever byte happens to be at the front of the buffer, potentially misreading a second packet's first byte as this one's second. Since all 3 bytes of a real packet arrive back-to-back within the same tight interrupt burst, this wait is negligible in practice, not a real block.

**Window** (`window.c`) is a title bar (filled rect + `text_puts` for the title) plus a body (filled rect) plus a border -- no dragging, no close button, no layering between multiple windows yet, just enough for widgets to live somewhere and to prove the existing drawing primitives compose into something that reads as "a window."

**Button** (`button.c`) is the first widget with actual behavior: `button_hit_test()` (point-in-rect) plus three fill colors (`hovered`, `pressed`, plain) makes cursor position mean something for the first time, rather than just being a shape following mouse input. `kmain` tracks a `prev_left_held` flag to detect the press *edge* (button held this packet, not held last packet, while hovering) rather than counting every packet where the button happens to be down -- otherwise holding the mouse button while sitting still would count as many clicks as packets arrive.

**Demo tying it together:** a "RAVE-OS PANEL" window containing a "CLICK ME" button (hover/press colors, a `CLICKS: N` counter using a small hand-written `format_uint()` -- no libc, no `itoa`), and a live-typed line (`TYPE: ...`, backspace and Enter both handled) fed straight from `keyboard_poll_char()`. `draw_scene()` redraws the entire backbuffer (backdrop, window, button, text, cursor, in that order) every time either input produces an event, rather than tracking incremental per-widget damage -- simpler, and correct by full reconstruction rather than bookkeeping, given the scene stays cheap. Same full-`gfx_present()`-per-frame tradeoff noted in the previous stage; still deferred.

Verified in QEMU: `mouse_move` onto the button showed the hover color; `mouse_button 1` while hovering showed the pressed color and incremented `CLICKS` to 1; `mouse_button 0` returned to hover color without incrementing again; `sendkey h`/`i`/`shift-1` produced `TYPE: HI!` live on screen -- mouse and keyboard events both driving the same redraw loop correctly.

Files: `kernel/window.c`, `kernel/window.h`, `kernel/button.c`, `kernel/button.h` (new); `kernel/keyboard.c`/`.h`, `kernel/mouse.c`/`.h` (poll primitives added, blocking functions rebuilt on top); `kernel/kernel.c` (window/button/event-loop demo); `kernel/Makefile` (`window.o`, `button.o`).

Next up: window dragging (mouse-down on the title bar moves the window) is the natural next step for "actual" windowing, followed by more widget variety (checkboxes, text input fields) and eventually multiple overlapping windows with real z-ordering -- which is also where the deferred damage-rect `gfx_present()` optimization will likely stop being optional.

## 2026-08-02 — Consolidation pass: paying down rough edges

No new features this stage -- a pause to work through the gaps flagged across prior entries rather than let them accumulate further.

**Deleted `vga.c`/`vga.h`.** The VGA text-mode driver had zero callers since the VESA/VBE switch several stages back -- complete, working, but fully superseded once graphics mode took over and never coming back without a real reason to run a second display mode. Removed rather than left as unreferenced cruft; the code is still in git history if ever needed.

**Automated the sector-count constants.** `STAGE2_SECTORS` (in `stage1.asm`), `KERNEL_START_SECTOR`/`KERNEL_SECTORS` (in `stage2.asm`), and a matching `KERNEL_SECTORS` budget in `kernel/Makefile` had been hand-copied, manually-synced constants since the very first two-stage-boot milestone -- flagged as a rough edge three separate times without being fixed. `boot/Makefile` now computes all of them from actual measured build sizes: it builds `kernel.bin` (which `kernel/Makefile` no longer pads or budgets at all -- that job moved here entirely), assembles `stage2.asm` once with placeholder values just to measure stage2's own true size, then re-assembles it for real with computed `KERNEL_SECTORS`/`KERNEL_START_SECTOR`, pads both `stage2.bin` and `kernel.bin` to sector boundaries, and assembles `stage1.asm` with the real `STAGE2_SECTORS`. Both `.asm` files keep `%ifndef`-guarded fallback values matching the old hardcoded numbers, in case either is ever assembled directly outside the Makefile. **Caught immediately by its own smoke test:** the first version computed `kernel_start_sector` as `1 + stage2_sectors`, off by one (should be `2 + stage2_sectors` -- stage1's own sector, plus stage2's sectors, plus one more to reach the next free sector) -- causing the kernel read to overlap the tail of stage2 and silently corrupt text rendering (shapes still drew fine; only text vanished). A full boot-and-screendump check caught it before it went anywhere -- exactly the kind of bug the old manual system was prone to, this time caught by a build that fails loudly (wrong boot) rather than one that requires remembering to update three files.

**Fixed `.bss` not actually being zeroed.** Flagged as a discovered-but-deferred gap in the backbuffer stage: `objcopy -O binary` drops `.bss` from the flat kernel image entirely, and the kernel had only worked so far because unused RAM happened to read as zero. `kernel/linker.ld` now exports `__bss_start`/`__bss_end` symbols bracketing the `.bss` section, and `kernel/kernel_entry.asm`'s `_start` zeroes that whole range (`rep stosb`) before calling `kmain` -- the standard, correct fix, rather than continuing to rely on emulator luck.

**Named exception handlers for divide error, invalid opcode, GPF, and page fault.** The IDT/PIC stage's generic exception catch-all could halt safely but never say *why* -- a real gap for a project that's also meant to be learnable from. Added a `panic()` helper (red banner + message via the existing graphics/text drivers, then halt) and four dedicated `__attribute__((interrupt))` handlers for vectors 0, 6, 13, and 14, wired ahead of the generic catch-all for just those. **Verified for real**, not just by inspection: temporarily forced an actual divide-by-zero in `kmain` and confirmed `PANIC: DIVIDE ERROR` renders correctly -- first attempt triggered a triple fault (reset back to SeaBIOS) because the test ran *before* `interrupts_init()`, i.e. before a valid IDT existed to catch anything; moving it to after `interrupts_enable()` produced the intended clean panic screen. (Page fault's handler is installed but not independently testable yet -- paging isn't enabled, so vector 14 can't actually fire until a future stage turns it on.)

**Left deliberately alone:** `gfx_present()`'s full-screen blit per frame (still no observed performance problem to justify damage-rect tracking), and `gfx_put_pixel`'s assumption of XRGB8888 channel order (works correctly on this QEMU/BIOS target; verifying against the ModeInfoBlock's actual `RedFieldPosition`/etc. would add real complexity for a portability concern with no evidence it's needed). Both remain accurately documented rather than fixed for their own sake.

Files: deleted `kernel/vga.c`, `kernel/vga.h`; `boot/Makefile` (sector-count automation), `boot/stage1.asm`/`stage2.asm` (`%ifndef`-guarded constants), `kernel/Makefile` (dropped fixed budget/padding); `kernel/linker.ld` (`__bss_start`/`__bss_end`), `kernel/kernel_entry.asm` (`.bss` zeroing); `kernel/isr.c` (`panic()`, four named exception handlers).

Next up: unchanged from before this pass -- window dragging, more widget variety, multiple windows with z-ordering, and revisiting damage-rect `gfx_present()` once it's an actual observed problem.

## 2026-08-02 — An EXIT button, so QEMU's mouse grab doesn't require a force-reboot to escape

Small but overdue fix: there was no way to leave a running Rave-OS session cleanly. QEMU (run via `boot/Makefile`'s `run` target, no special display flags) grabs the host mouse the moment you click into its window, since the PS/2 mouse protocol is relative-motion and QEMU needs exclusive pointer input to generate those deltas. QEMU's own release shortcut (Ctrl+Alt+G) exists for this, but with nothing in the guest OS itself offering a way out, a force-reboot ended up being the escape hatch in practice.

**The real fix is to make the guest exit cleanly, not to rely on remembering a host-side shortcut.** QEMU's default machine type (`pc`, i440fx chipset) emulates a PIIX4 ACPI power-management block; writing `0x2000` to I/O port `0x604` (the PM1a control register) requests an ACPI S5 ("soft off") transition -- the same mechanism a real BIOS/ACPI-aware OS uses to power off hardware, and one of the most common tricks hobby OSes use to shut down cleanly under QEMU without needing any extra `-device`/`-machine` flags. Added `outw()` to `io.h` (alongside the existing `outb`/`inb`) and a `power_shutdown()` helper in `kernel.c` that does exactly this, halting in a loop afterward as a fallback in case the write is ever a no-op on a different machine type.

**Wired up as a second button**, reusing the existing generic `struct button`/`button_draw`/`button_hit_test` from the previous GUI-primitives stage rather than adding any new widget code: an "EXIT" button sits next to "CLICK ME" in the panel, sized to fill the remaining panel width. The event loop's existing hover/press-edge logic (already tracking `prev_left_held` for click-edge detection) extended naturally to a second button by just repeating the same hit-test/edge-detect block against `exit_btn` and calling `power_shutdown()` on its click edge instead of incrementing a counter.

**Verified headlessly**, since the point was confirming an actual mouse click makes QEMU's process exit (not just that the code compiles): booted `disk.img` with `-display none -monitor unix:...`, drove the guest via monitor `mouse_move`/`mouse_button` commands to hover and click the EXIT button, then confirmed the QEMU process itself was gone from `/proc` afterward -- process exit, not a hang or a crash. A `screendump` before the click also confirmed the new button renders correctly alongside the existing panel (border, label, and hover/press coloring all shared with `CLICK ME` for free, being the same widget code).

Files: `kernel/io.h` (`outw`); `kernel/kernel.c` (`power_shutdown()`, `exit_btn`, event loop wiring).

Next up: unchanged from before this pass -- window dragging, more widget variety, multiple windows with z-ordering, and revisiting damage-rect `gfx_present()` once it's an actual observed problem.

## 2026-08-02 — Erratic real-mouse tracking, and why the EXIT-button test never caught it

The EXIT button worked in headless verification, but booting Rave-OS with a real display and an actual physical mouse showed something new: the cursor didn't track cleanly, it jumped around erratically. This is a genuinely different bug from the earlier "mouse grab has no escape" problem the EXIT button fixed -- and, worse, every prior "verified in QEMU" claim for the mouse driver going back to its introduction had only ever exercised it via the QEMU monitor's `mouse_move`/`mouse_button` commands, which inject synthetic PS/2 packets directly and completely bypass real hardware-grab input. None of that testing ever ran a real, continuous stream of physical mouse packets through the driver.

**Root cause: the event loop couldn't keep up with real input.** `kmain`'s loop redraws the *entire* scene (a per-pixel plasma backdrop, ~307,200 `gfx_put_pixel` calls, plus `gfx_present()` writing the whole 640x480 framebuffer back out one byte at a time through a `volatile` pointer -- up to ~1.2M individual MMIO writes) on **every single mouse packet**. A real mouse streams packets continuously (PS/2 default ~100/sec) -- much faster than that redraw can complete under QEMU's TCG (software) emulation, which this project runs without `-accel kvm`. `mouse.c`'s ring buffer (32 bytes, ~10 packets) backs up during a slow redraw, and `mouse_irq_push_byte()` drops individual *bytes* once full, not whole 3-byte packets -- desyncing the stream's framing. Since `mouse_poll_packet()`'s resync check (bit 3 of byte 0) can coincidentally match a delta byte that happens to land in that position after a drop, it can resync onto garbage mid-packet, decoding nonsense dx/dy values -- exactly the "erratic jumps" observed, and never reproducible via the monitor's synthetic commands because those are sent one at a time, far slower than the redraw loop.

**Fix, in `kernel.c`:** the event loop now drains *every* mouse packet already queued (`while (mouse_poll_packet(...))` instead of `if`) before triggering a single redraw, rather than redrawing per packet. Each packet's position/hover/click-edge state is still applied individually and correctly (so click-edge detection and bounds clamping are unaffected), but a burst of queued packets collapses into one redraw instead of one-per-packet -- keeping the loop caught up with real input instead of falling further behind with every packet that arrives mid-redraw.

**Also fixed in `mouse.c`, found by re-reading the packet format while tracking this down:** byte 0's bits 6/7 (X/Y overflow) were never checked. A real fast mouse flick can exceed the 9-bit signed delta range these packets carry, at which point the device sets these bits to say the reported delta byte is unreliable -- the driver was trusting it anyway. `mouse_poll_packet()` now clamps to the max representable magnitude (255) in the reported direction when either overflow bit is set, rather than trusting a bogus value. This is a real spec-correctness gap, not just defense-in-depth -- fast real mouse motion can trip it even with the redraw fix in place.

**Not changed:** the underlying full-screen-redraw-per-frame architecture (flagged as a deferred tradeoff repeatedly since the backbuffer stage) is still there -- this fix keeps the loop from falling behind by batching redraws, but each individual redraw is exactly as expensive as before. A damage-rect `gfx_present()` remains the real fix for that cost; this stage is now concrete evidence it's worth doing, not just a hypothetical.

**Verified with a real physical mouse** (not just the monitor's synthetic commands, which -- as this whole entry explains -- can't reproduce this class of bug): rebuilt, relaunched with a visible display, moved the mouse around continuously. Cursor tracked smoothly, confirmed by the user directly.

Files: `kernel/kernel.c` (drain-all-queued-packets-before-redraw); `kernel/mouse.c` (overflow-bit clamping).

Next up: unchanged from before this pass, plus damage-rect `gfx_present()` has graduated from "deferred, no evidence needed" to "worth doing soon" now that a real performance ceiling has been observed to cause an actual correctness bug, not just dropped frames.

## 2026-08-02 — Window dragging

The panel window can now be moved by pressing on its title bar and dragging, the natural next step flagged repeatedly since the first-GUI-primitives stage.

**`window_titlebar_hit_test()`** (`window.c`/`.h`) is a point-in-rect test against just the title bar strip, sibling to `button_hit_test()` -- what the event loop checks before starting a drag, so clicking the window body doesn't also grab it.

**Dragging itself needs no grab-offset bookkeeping**, which simplified the implementation: since PS/2 packets are relative deltas rather than absolute positions, applying a packet's raw `dx`/`dy` to the window (and, since they're independent absolute-positioned structs, its child widgets -- `btn`, `exit_btn`) the same way it's already applied to the cursor keeps the cursor's position over the title bar constant for the whole drag, with no separate offset to track. `kmain` tracks one new flag, `dragging_titlebar`: the press that starts a drag (edge-detected the same way button clicks already are) only sets the flag, deferring actual movement to the next packet -- so the initiating click doesn't also apply that same packet's incidental motion. A nice side effect of moving the cursor and the window by the same delta every packet: the cursor's position *relative to* the window never changes during a drag, so it can never drift onto `CLICK ME`/`EXIT` mid-drag and cause a spurious hover or click.

**Verified two ways**, per the lesson from the previous stage: headlessly first (QEMU monitor commands with small sleeps between them, since a burst sent with no delay outran the guest's ability to process and redraw between screendumps -- an artifact of the *test script's* timing, not a kernel bug, caught by comparing a screendump taken too early against one taken after adding delays), confirming press-then-idle didn't move the window but press-then-move did, and the child buttons moved with it, staying clickable at their new position. Then confirmed with a real physical mouse, per [[feedback_qemu_input_testing]] -- dragging felt correct interactively, not just in synthetic replay.

Files: `kernel/window.c`/`.h` (`window_titlebar_hit_test()`); `kernel/kernel.c` (`dragging_titlebar` state, drag-apply logic in the event loop).

Next up: more widget variety (checkboxes, text input fields), multiple windows with z-ordering (dragging one window over another doesn't yet mean anything, since there's only one), and the deferred damage-rect `gfx_present()`.

## 2026-08-02 — Re-theme: black-and-acid-green, borrowed from androidacid.com

The original full-screen XOR-rainbow plasma backdrop was flagged directly as too harsh to look at for long. Replaced the whole visual palette with the "black and acid green" language from androidacid.com (its actual GitHub Pages source, `AndroidAcid/AndroidAcid.github.io`, was available to read directly -- its `assets/style.css` gave exact CSS custom properties to work from rather than guessing at a screenshot).

**Translating a CSS palette (with alpha-blended `rgba()` panels and text) to a kernel that only does opaque pixel fills** meant hand-computing each color's flat-RGB equivalent: `fg*alpha + bg*(1-alpha)` per channel, composited onto the site's own `--bg` (`#050607`). Most translated directly (`--hard: #00ff66` used as-is for accent/borders/pressed-state fills, `--text`/`--muted` composited down to `0xD4E6DB`/`0x9DAAA3`). One didn't: `--panel`'s literal composite came out to roughly `0x070C09` -- correct by the math, but on a flat kernel renderer with no backdrop-blur or content showing through, it read as almost indistinguishable from the background it's meant to visually separate from. Brightened by hand to `0x0B1712` (window body) / `0x0A1A12`-`0x123322` (button idle/hover) instead of the literal composite -- a case where matching the *intent* (a panel that reads as a panel) mattered more than matching the arithmetic exactly.

**The backdrop** (`kernel.c`): `plasma_color()` -- an XOR-based full-spectrum formula -- became `backdrop_color()`, a mostly-flat near-black fill with one soft radial green glow (integer-only linear falloff from a fixed point, no floats, matching the rest of the kernel) standing in for the site's CSS `radial-gradient` glow blobs. Same per-pixel cost class as the plasma it replaced, so no redraw-performance regression on top of the previous stage's fix.

**Buttons** (`button.c`) needed one behavioral change alongside the recolor: the pressed state now inverts to a solid `--hard` green fill (echoing how the site uses that color as a hard highlight), which meant the label also needed to switch to a dark color on top of it for contrast -- previously a single label color worked for every state because no fill was ever that bright.

**Verified visually**: headless screendump compared against the intended palette before handing it to a live QEMU window; user confirmed it read better in person ("thats better") after the harshness complaint that started this.

Files: `kernel/kernel.c` (`backdrop_color()` replacing `plasma_color()`, text/cursor color constants); `kernel/window.c` (color constants); `kernel/button.c` (color constants, pressed-state label contrast).

Next up: unchanged -- more widgets, multi-window z-ordering, damage-rect `gfx_present()`. True rounded corners (androidacid.com leans on large CSS `border-radius` throughout) were deliberately left out of this pass -- the color language was the headline signal from the reference, corner rounding would need a real rounded-rect fill primitive, and nothing about the current square corners is broken.
