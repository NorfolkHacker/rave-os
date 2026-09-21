# Rave-OS

Rave-OS is a hobby x86 operating system built from scratch in C and assembly — its own two-stage bootloader, a mouse-driven windowing GUI, a flat-file filesystem with a Unix-like directory layout, a from-scratch Forth dialect as its scripting language, and an 8-voice SID-style software synthesizer, all running with no libc and no floating point. It also has a real, if still foundational, userspace: identity-mapped paging, a ring 3 + `int 0x80` syscall ABI, a syscall surface covering filesystem/graphics/audio/window operations, and a loadable flat-binary program format (see [Userspace internals](#userspace-internals) below — none of this is user-reachable yet, it's kernel-level groundwork). It exists as a learning project: a record of learning x86 assembly, freestanding C, and OS-level programming by building one, staged deliberately (bootable groundwork → GUI → Forth → self-hosting → userspace) rather than attempted all at once.

> **acid OS v2** — the successor, a FreeRTOS + mruby OS for the M5Stack Tab5
> (ESP32-P4) where every app is a Ruby script in its own VM, lives in
> [`v2/`](v2/). Its apps are documented in
> **[The acid OS v2 Manual](docs/manual/README.md)** — how to write apps in
> Ruby, including the [8-voice synthesiser API](docs/manual/05-sound.md).

Everything below has been verified in QEMU. A real-hardware boot has not yet been attempted — see [Known limitations](#known-limitations).

## Install

### 1. Prerequisites

```bash
sudo apt-get install -y build-essential bison flex libgmp-dev libmpc-dev libmpfr-dev texinfo nasm qemu-system-x86
```

### 2. Build the cross-compiler (once)

Rave-OS builds with a real `i686-elf` cross-compiler, not the host's own `gcc` — a genuine freestanding, libc-less target triplet with no host-specific codegen assumptions to drift out from under it. `toolchain/build-cross.sh` builds and installs one (binutils + gcc, pinned versions, checksummed downloads) to `~/opt/cross` by default:

```bash
./toolchain/build-cross.sh
export PATH="$HOME/opt/cross/bin:$PATH"   # add this to your shell profile too
i686-elf-gcc --version   # sanity check
```

This takes a while (it's compiling gcc from source) but only needs to run once.

### 3. Build and run

```bash
cd boot
make run
```

`make run` builds the kernel, assembles the bootloader, lays out `disk.img` (bootloader + kernel, sized and sector-aligned automatically), creates a second scratch disk `fs.img` for the filesystem, and boots the result in QEMU (`-accel kvm -display sdl -device sb16`). A window opens showing Rave-OS's desktop.

To actually hear audio (the Sound Blaster 16 device works either way, but `make run` doesn't attach a host audio backend by default), run `qemu-system-i386` by hand instead, appending your own backend:

```bash
qemu-system-i386 -accel kvm -display sdl \
  -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 \
  -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 \
  -audiodev pa,id=snd0 -device sb16,audiodev=snd0
```

(swap `pa` for `pipewire`/`alsa`/whatever your host's audio server is)

## Usage

### The desktop

Rave-OS boots straight to a 640x480 GUI desktop with a taskbar and start menu. The start menu has six items:

| Item | What it does |
|---|---|
| **FORTH** | Opens a Forth console — the primary way to script and control the system (see below) |
| **FILES** | A file manager over Rave-OS's own filesystem |
| **SHELL** | A Unix-like command shell (`cd`, `ls`, `cat`, `mkdir`, `rm`, `mv`, `cp`) |
| **CONFIG** | Browses `/ETC` |
| **GAMES** | Browses `/GAMES` |
| **EXIT** | Closes the desktop |

The filesystem has a standard set of top-level directories: `/BIN`, `/ETC`, `/HOME`, `/USR`, `/VAR`, `/TMP`, `/DEV`, plus the two IDE drives surfaced as `/HDA`/`/HDB`.

From FILES, opening a text file launches **EDITOR**; the Forth console's own `PAINT` word (below) launches **PAINT**, a sprite/pixel-art editor.

### The Forth console

Rave-OS's Forth is a small, real dictionary-based Forth: a data stack, integer literals, `IF ... ELSE ... THEN`, `BEGIN ... UNTIL`, and user-defined words via `: NAME ... ;` (a word can only call words defined strictly before it — no forward references, the same restriction real Forth dictionaries have always had). Type words separated by spaces; the whole line runs when you press Enter.

**Stack and I/O:**

| Word | Effect |
|---|---|
| `+` `-` `*` `/` | Arithmetic |
| `DUP` `DROP` `SWAP` `OVER` | Stack shuffling |
| `=` `<` `>` | Comparison (Forth convention: `-1` = true, `0` = false) |
| `.` | Pop and print |
| `CR` | Print a newline |
| `@` `!` | Fetch / store |

**Graphics and input:**

| Word | Stack effect | Effect |
|---|---|---|
| `PIXEL` | `( x y color -- )` | Set a pixel on a 16x16 grid (x, y, color all 0-15) |
| `MOUSE-X` / `MOUSE-Y` | `( -- n )` | Current mouse position |
| `MOUSE-DOWN?` / `MOUSE-RIGHT-DOWN?` | `( -- flag )` | Mouse button state |
| `WINDOW-CLOSED?` | `( -- flag )` | Whether the current window was closed |
| `CURRENT-COLOR` | `( -- n )` | The currently selected color |
| `PAINT` | `( -- )` | Opens the PAINT app |
| `BEEP` | `( -- )` | Plays a short test tone through the Sound Blaster 16 driver |

**The synth engine:** Rave-OS has an 8-voice SID-style software synthesizer, entirely controlled from Forth. `VOICE` selects which of the 8 voices the per-voice words below act on:

| Word | Stack effect | Effect |
|---|---|---|
| `VOICE` | `( n -- )` | Select voice `n` (0-7) as the target for every per-voice word below |
| `WAVE` | `( n -- )` | Set the current voice's waveform: `0`=pulse, `1`=saw, `2`=triangle, `3`=noise |
| `DUTY` | `( n -- )` | Pulse-wave duty cycle, percent |
| `ONA` | `( n -- )` | Set the current voice's note (1-88, standard piano numbering) |
| `ADSR` | `( attack_ms decay_ms sustain_percent release_ms -- )` | Set the current voice's envelope |
| `GATE-ON` / `GATE-OFF` | `( -- )` | Trigger / release the current voice's envelope |
| `FILTER-CUTOFF` | `( n -- )` | The shared resonant filter's cutoff, 0-255 |
| `FILTER-RES` | `( n -- )` | The shared filter's resonance, 0-15 |
| `FILTER-MODE` | `( n -- )` | Which filter outputs sum together: bit0=low-pass, bit1=band-pass, bit2=high-pass (0-7) |
| `FILTER-ROUTE` | `( n -- )` | `1` routes the current voice through the shared filter, `0` bypasses it |
| `RING-PARTNER` / `RING-OFF` | `( n -- )` / `( -- )` | Ring-modulate the current voice's triangle wave against voice `n`, or turn it off |
| `ARP-NOTE` | `( note slot -- )` | Load an arpeggio note (1-88) into one of 4 slots (0-3) on the current voice |
| `ARP-ON` / `ARP-OFF` | `( count -- )` / `( -- )` | Start cycling through `count` (2-4) loaded notes, or stop |
| `ARP-RATE` | `( ms -- )` | Milliseconds per arpeggio step |

There's only one shared filter (matching the real SID chip's own architecture), but ring modulation and the arpeggio are independently configurable per voice. A minimal example — a filtered, arpeggiating sawtooth on voice 0:

```forth
0 VOICE 1 WAVE 10 50 80 300 ADSR
1 0 ARP-NOTE 8 1 ARP-NOTE 15 2 ARP-NOTE
3 ARP-ON 60 ARP-RATE
1 FILTER-ROUTE 40 FILTER-CUTOFF 8 FILTER-RES 1 FILTER-MODE
GATE-ON
```

## Userspace internals

Rave-OS has a real, working ring 3 / syscall foundation, but **nothing
in the desktop, Forth, or SHELL currently launches ring-3 code** — every
piece below was proven with a temporary, throwaway test payload during
development and then removed. There's no `RUN`-style command yet that
loads and executes a real program. This section exists for anyone
reading the source, not as a feature you can drive from the desktop.

What exists at the kernel level:

- **Paging + a minimal syscall ABI.** A single identity-mapped page
  directory with one page-directory entry (the low 4MB) flippable
  between supervisor-only and user-accessible; `enter_ring3()` drops
  CPL0 → CPL3; a hand-written `int 0x80` trap gate carries one syscall
  number and one argument (a plain value, or a pointer to a small args
  struct for multi-argument calls) each way.
- **A syscall surface**, `kernel/arch/syscall_{fs,gfx,audio,window}.c`:
  every real `kernel/fs/fs.h` operation (create/read/append/delete/
  list/mkdir/rename/move/copy); a handful of `kernel/gfx/graphics.h`
  drawing primitives; enough of `kernel/audio/synth.h` to make a voice
  play a note; and `SYS_WINDOW_OPEN`/`SYS_WINDOW_CLOSE` for a single,
  non-interactive ring-3 window (no dragging, no event delivery back to
  the program yet).
- **A loadable program format**, `programs/hello/`: a real, separately
  compiled flat binary (fixed load address, no relocation), loaded from
  a real on-disk file via `kernel/kernel.c`'s `program_load_and_run()`.

See `docs/IDEAS.md`'s "Real userspace" entry, the dated design specs
under `docs/superpowers/specs/`, and `docs/BUILD_LOG.md` for the full
design history and how each piece was verified.

## Project history and roadmap

`docs/BUILD_LOG.md` is a running, detailed record of how the system was built — decisions, concepts learned, what was verified and how. `docs/IDEAS.md` is the informal backlog of things considered but not yet scoped. Design specs and implementation plans for individual features live under `docs/superpowers/`.

## Known limitations

- **No real-hardware verification pass yet.** Everything is built and tested against QEMU; running on real x86 hardware hasn't been attempted.
- **One fixed resolution** (640x480) — no display configuration.
- Fixed-point arithmetic throughout (the kernel builds with `-mgeneral-regs-only`, so there is no floating point anywhere, including in the audio engine).
- **No user-facing way to run a ring-3 program yet.** The syscall ABI, syscall surface, and loadable program format described in [Userspace internals](#userspace-internals) are real and working, but nothing in the desktop/Forth/SHELL currently loads or launches one — every proof so far used a temporary, since-removed test payload. There's also no real event delivery to a ring-3 window (it can only draw once and sit static) and no more than one loaded program can run at a time.
