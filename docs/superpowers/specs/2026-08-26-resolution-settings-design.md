# Removing the hardcoded 640x480 assumption

## Purpose

`docs/IDEAS.md`'s "Different resolution settings" entry (raised
2026-08-20): `boot/stage2.asm`'s `VBE_MODE` is hardcoded to `0x112`
(640x480, 32bpp) with no path to request a different mode. Closer
inspection during this spec's brainstorming found the actual gap
narrower than the entry suspected -- `kernel/gfx/graphics.c`'s
`gfx_width()`/`gfx_height()` already read real values out of the
`boot_info` struct stage2 fills in at boot (see
`kernel/boot_info.h`), and the taskbar/start menu already derive their
own position from those at runtime. The only two things actually
hardcoded to 640x480 are stage2's single VBE mode number and five
window's initial x/y positions in `kernel/kernel.c`.

## Scope

**In scope:**
- Make `VBE_MODE` a build-time override (`boot/Makefile`/`nasm -D`,
  the same pattern already used for `KERNEL_SECTORS`/
  `KERNEL_START_SECTOR`) instead of a fixed `equ`. Default stays
  `0x112` (640x480) -- a plain `make`/`make run` is unaffected.
- Scale the five hardcoded window `.x`/`.y` positions (FORTH, FILES,
  SHELL, EDITOR, PAINT) in `kernel/kernel.c` against the real screen
  size at boot, instead of using literal pixel constants.
- Verify both the default (640x480, must be byte-for-byte identical to
  today) and one alternate resolution (800x600, VBE mode `0x115`) boot
  correctly and lay out sensibly.

**Out of scope, deliberately:**
- **Runtime mode selection or fallback.** stage2 still queries and
  sets exactly one VBE mode; which one is a build-time constant, not
  something the kernel negotiates or lets the user pick at boot. Same
  reasoning `docs/IDEAS.md`'s dropped "boot-time config screen for
  CPU/RAM" entry already established for this project: a launch-time
  parameter, not an in-OS feature.
- **Window size scaling.** Every window's `.w`/`.h` stays exactly the
  literal pixel size it is today -- only position scales. A 400x180
  FORTH window is equally readable at 640x480 or 800x600; scaling
  sizes too would be more visual "consistency" for no functional
  benefit and would touch more of each window's setup block for it.
- **Any bpp change.** `graphics.c` already handles `boot_info->bpp`
  dynamically (see its existing bytes-per-pixel fix in
  `docs/BUILD_LOG.md`); this spec doesn't touch bpp at all, only
  width/height via the VBE mode number.
- **Any other GUI file.** Taskbar, start menu, and every button/input
  field already derive their position from `w`/`h` or from a window's
  own position, not from independent literals -- confirmed by grep,
  not assumed.

## Design

### `boot/Makefile` / `boot/stage2.asm`: an overridable VBE mode

`boot/stage2.asm` currently has:
```asm
VBE_MODE       equ 0x112
```
This becomes a `%ifndef`-guarded default, the same way `nasm -D`
overrides already work for this file's `KERNEL_SECTORS`/
`KERNEL_START_SECTOR`:
```asm
%ifndef VBE_MODE
VBE_MODE       equ 0x112
%endif
```
`boot/Makefile` gains a `VBE_MODE ?= 0x112` variable and passes
`-D VBE_MODE=$(VBE_MODE)` alongside its existing `-D` flags in the
`disk.img` recipe's stage2 assembly steps (both the measurement pass
and the real pass). `make` and `make run` are unaffected by default;
`make VBE_MODE=0x115` (or `make run VBE_MODE=0x115`) builds against
800x600 instead. No other stage2 code changes -- it already queries
the BIOS's ModeInfoBlock for whichever mode number this is and writes
the real `XResolution`/`YResolution`/`BytesPerScanLine`/`BitsPerPixel`
it gets back into `boot_info`, so nothing downstream needs to know the
mode number was ever anything other than what it turns out to be at
runtime.

### `kernel/kernel.c`: scaling the five window positions

New constants next to the other screen-geometry `#define`s:
```c
#define BASELINE_W 640
#define BASELINE_H 480
```
Each of the five hardcoded assignments changes from a literal to a
scaled expression, e.g.:
```c
windows[WIN_KIND_FORTH].x = 170;
windows[WIN_KIND_FORTH].y = 230;
```
becomes
```c
windows[WIN_KIND_FORTH].x = 170 * w / BASELINE_W;
windows[WIN_KIND_FORTH].y = 230 * h / BASELINE_H;
```
(`w`/`h` are already in scope at this point in `kmain()` -- see
`kernel.c:1964-1965`, `w = gfx_width(); h = gfx_height();`, captured
before any window setup runs.) Same transform for FILES, SHELL,
EDITOR, and PAINT's `.x`/`.y`. At the default 640x480 this is
arithmetically identical to today (`170 * 640 / 640 == 170`, exactly, for every one of the five pairs --
whenever `w == BASELINE_W` and `h == BASELINE_H`, the multiply and
divide are always by the same value, so integer division never
truncates) -- zero behavior change unless `VBE_MODE` is actually
overridden. At 800x600, each position scales by a factor of
800/640 = 600/480 = 1.25, same as before but now with ordinary integer
division: e.g. FORTH's `x = 170 * 800 / 640` lands at 212, not the
non-integer 212.5, a sub-pixel truncation that's cosmetically
irrelevant. The whole hand-placed layout (today's "overlapping the
other windows' corners is fine" arrangement) spreads out
proportionally rather than clustering in the top-left quarter of the
larger screen.

No other file changes: buttons, input fields, and PAINT's own
sub-widgets already compute their position from `windows[...].x`/`.y`
(confirmed by grep -- see Purpose), so they automatically follow their
parent window.

## Testing

**Build verification:**
- `make clean && make` (default `VBE_MODE`) in both `kernel/` and
  `boot/` builds clean, zero new warnings.
- `make clean && make VBE_MODE=0x115` (in `boot/`, which rebuilds the
  kernel too) also builds clean.

**Headless QEMU, both resolutions** (same `-display none` +
`-accel kvm` + monitor `screendump` technique used for the boot
splash work):
- Default (640x480): screendump the desktop and confirm window
  positions are pixel-identical to a screendump taken before this
  change (proves the scaling math is a true no-op at baseline).
- 800x600: screendump the desktop and confirm (a) the framebuffer is
  actually 800x600 now (not letterboxed/cropped 640x480 content), (b)
  every window's titlebar is fully on-screen, none clipped or
  off-canvas, (c) the taskbar spans the new full width and sits at the
  new bottom edge.

No host-buildable unit test is warranted here: the scaling expression
is a one-line arithmetic transform with no branching, and the
meaningful failure modes (wrong framebuffer size, windows off-screen)
are only observable by actually booting and looking, not by
unit-testing arithmetic in isolation.
