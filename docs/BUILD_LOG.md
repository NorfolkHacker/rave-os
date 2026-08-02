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
