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
