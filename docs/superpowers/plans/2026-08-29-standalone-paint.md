# Standalone Paint Binary Implementation Plan

> **Amendment (post-Task 5):** every `0x00200000` load address named
> below was correct when this plan was written, but got relocated to
> `0x00340000` mid-plan -- Task 5's own verification found a real
> collision between the fixed ring-3 load address and the graphics
> backbuffer, both at `0x200000` (see `docs/BUILD_LOG.md`'s closeout
> entry / git history for the fix). The actual shipped address, in
> `programs/paint/paint.ld` and everywhere else, is `0x00340000`. Left
> as-written below rather than rewritten throughout -- don't "fix" the
> `.ld` files back to `0x00200000` to match this document.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move PAINT out of the kernel entirely into `programs/paint/paint.bin`, a standalone ring-3 flat binary launched by clicking a new Start Menu "PAINT" entry, and delete every kernel-resident PAINT implementation (window chrome, Forth hooks, Forth script) it replaces.

**Architecture:** `programs/paint/` is a new, freestanding (no libc, no kernel headers) C program built with the existing `i686-elf-*` cross-toolchain, in the exact shape `programs/hello/` already established (fixed load address `0x00200000`, flat binary via `objcopy`). It draws its whole UI (256x256 canvas, 16-swatch color strip, filename field, SAVE/LOAD buttons) using only `SYS_GFX_PUT_PIXEL`/`SYS_GFX_FILL_RECT`/`SYS_GFX_PRESENT_RECT` plus an embedded copy of the kernel's own bitmap font, drives its own event loop with `SYS_WAIT_EVENT` (click/key/close), and saves/loads 256-byte sprite files via `SYS_CREATE_FILE`/`SYS_READ_FILE`/`SYS_DELETE`. `kernel/kernel.c` embeds the built binary's bytes and seeds it to `/BIN/PAINT.BIN`; a new Start Menu item calls the already-shipped-but-never-wired `program_load_and_run("/BIN/PAINT.BIN")`. Once that works end to end, every kernel-resident PAINT thing (struct paint, its window kind, its Forth hooks, its Forth script) is deleted.

**Tech Stack:** C (freestanding, `-nostdlib`), x86 `int 0x80` inline asm, `i686-elf-gcc`/`i686-elf-ld`/`i686-elf-objcopy` cross-toolchain, QEMU (headless, monitor-driven screendumps) for verification.

**Spec:** `docs/superpowers/specs/2026-08-29-standalone-paint-design.md`

## Global Constraints

- Fixed ring-3 load address: `0x00200000` (must match `programs/paint/paint.ld`'s origin — same constant `programs/hello/hello.ld` already uses).
- No new syscalls, no changes to `kernel/arch/syscall.h` or any `syscall_*.c` file — build entirely on the syscalls that already exist.
- `programs/paint/` has zero kernel-header dependencies — any syscall number/struct it needs is locally redeclared, matching `programs/hello/hello.c`'s existing convention.
- On-disk sprite format is unchanged from the old kernel implementation: 256 raw bytes, one per cell, row-major, palette index `0..15`, at `/HOME/<NAME>` (name uppercased).
- No popup/hide-restore palette UI — a single always-visible 16-swatch strip, left-click only (no syscall carries right-click).
- PAINT launches once per boot (Start Menu item only); reopening after close requires a reboot — an accepted, pre-existing kernel limitation, not something to work around here.
- Every kernel-side deletion must leave `kernel/kernel.c` compiling clean with **zero** remaining references to `struct paint`, `WIN_KIND_PAINT`, `paint_save_btn`, `paint_load_btn`, `paint_name_input`, `paint_program_slot`, `paint_palette`, `PAINT_GRID_SIZE`/`PAINT_CELL_PX`/`PAINT_PALETTE_COLORS`/`PAINT_SWATCH_W`/`PAINT_SWATCH_H`/`PAINT_POPUP_*`, `draw_paint_group`, `paint_swatch_hit_test`, `paint_popup_grid_hit_test`, `paint_mouse_cell`, `paint_build_sprite_path`, `BIN_PAINT_PATH`, `seed_bin_paint_script` — `grep -ni "paint" kernel/kernel.c` at the end of Task 6/7 should only turn up the plain English word ("painted"/"repaint"/etc. in unrelated comments) and the new `paint_bin`/`/BIN/PAINT.BIN` seeding from Task 3.

---

## File Structure

- **Create** `programs/paint/font.h`, `programs/paint/font.c` — verbatim copy of `kernel/gfx/font.c`'s 5x7 glyph table (self-contained, no dependency on the kernel's copy).
- **Create** `programs/paint/paint.ld` — linker script, fixed origin `0x00200000` (same shape as `programs/hello/hello.ld`).
- **Create** `programs/paint/paint.c` — the whole standalone program: syscall wrappers, drawing, hit-testing, save/load, main event loop.
- **Create** `programs/paint/Makefile` — cross-toolchain build, mirrors `programs/hello/Makefile`.
- **Modify** `kernel/kernel.c` — embed `paint_bin[]` + seed `/BIN/PAINT.BIN` (Task 3); delete every PAINT-specific kernel-resident implementation (Task 6, Task 7).
- **Modify** `kernel/gui/startmenu.h`, `kernel/gui/startmenu.c` — new `STARTMENU_ITEM_PAINT` entry (Task 4).
- **Modify** `kernel/forth/forth_hooks.h`, `kernel/forth/forth.c` — delete the PAINT-specific hook declarations/primitives (Task 7).
- **Modify** `docs/BUILD_LOG.md` — closeout entry (Task 8).

---

### Task 1: Embedded font module

**Files:**
- Create: `programs/paint/font.h`
- Create: `programs/paint/font.c`

**Interfaces:**
- Produces: `const char *const *font_glyph(char c)` — returns 7 row-strings (5 chars each, `'X'`=on/`'.'`=off) for a glyph, defaulting to a blank glyph for any character with none (matches `kernel/gfx/font.c`'s own contract exactly, since this is a verbatim copy). Consumed by Task 2's `text_puts()`.

- [ ] **Step 1: Create `programs/paint/font.h`**, verbatim copy of `kernel/gfx/font.h`:

```c
#ifndef RAVEOS_PAINT_FONT_H
#define RAVEOS_PAINT_FONT_H

/* Returns 7 row-strings (5 chars each, 'X'=pixel on, '.'=off) for a glyph,
 * or NULL if the character has no glyph (rendered blank). */
const char *const *font_glyph(char c);

#endif
```

- [ ] **Step 2: Create `programs/paint/font.c`**, verbatim copy of `kernel/gfx/font.c`'s glyph tables and `font_glyph()` (do not hand-retype — copy the file directly with `cp kernel/gfx/font.c programs/paint/font.c`), then change only its `#include "font.h"` line to point at this directory's own copy (already correct since both are named `font.h`) and update the top comment to note this is `programs/paint/`'s own self-contained copy:

```bash
cp kernel/gfx/font.c programs/paint/font.c
```

Then edit the top comment (first 6 lines) to read:

```c
/* 5x7 dot-matrix font, a self-contained copy of kernel/gfx/font.c so
 * programs/paint/ has zero dependency on the kernel's own copy --
 * programs/paint/ is a freestanding ring-3 binary with no access to
 * kernel/ headers at all. Each glyph is 7 rows of 5 chars: 'X' = lit,
 * '.' = off. */
```

- [ ] **Step 3: Verify it compiles standalone**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd programs/paint
i686-elf-gcc -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -Wall -Wextra -c font.c -o /tmp/font_test.o
```

Expected: no output, no errors — `/tmp/font_test.o` is produced. Clean it up: `rm /tmp/font_test.o`.

- [ ] **Step 4: Commit**

```bash
git add programs/paint/font.h programs/paint/font.c
git commit -m "programs/paint: add self-contained font module"
```

---

### Task 2: The standalone paint program

**Files:**
- Create: `programs/paint/paint.ld`
- Create: `programs/paint/paint.c`
- Create: `programs/paint/Makefile`

**Interfaces:**
- Consumes: `font_glyph()` from Task 1.
- Produces: `programs/paint/paint.bin` (build artifact, consumed by Task 3's embedding step). Entry point `_start`, fixed at `0x00200000`.

- [ ] **Step 1: Create `programs/paint/paint.ld`**

```ld
/* Fixed load address -- must match PROGRAM_LOAD_ADDR in kernel/kernel.c's
   program_load_and_run() and programs/hello/hello.ld's own origin. See
   docs/superpowers/specs/2026-08-29-loadable-program-design.md for why
   this is fixed rather than relocatable. */
ENTRY(_start)
SECTIONS {
    . = 0x00200000;
    .text   : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) *(COMMON) }
    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note.*) }
}
```

- [ ] **Step 2: Create `programs/paint/paint.c`**

```c
/* programs/paint/paint.c
 *
 * The standalone RAVE-OS paint program -- a freestanding, no-libc
 * ring-3 flat binary launched by kernel/kernel.c's
 * program_load_and_run("/BIN/PAINT.BIN") (wired to a new Start Menu
 * click, see kernel/kernel.c's start-menu dispatch). Replaces the old
 * kernel-resident PAINT window entirely -- see
 * docs/superpowers/specs/2026-08-29-standalone-paint-design.md.
 *
 * Deliberately does NOT include kernel/arch/syscall.h (kernel-internal,
 * never linked into a program built this way) -- every syscall number
 * and argument struct below is a local redeclaration of the same fixed
 * ABI that header documents, the same convention
 * programs/hello/hello.c already established. */

#include "font.h"

/* --- syscall ABI (kernel/arch/syscall.h) --- */
#define SYS_READ_FILE 2
struct sys_read_file_args { const char *path; void *buf; unsigned int buf_size; unsigned int *out_size; };
#define SYS_CREATE_FILE 3
struct sys_create_file_args { const char *path; const void *data; unsigned int size; };
#define SYS_DELETE 5 /* arg IS the path pointer directly */
#define SYS_GFX_WIDTH 11
#define SYS_GFX_HEIGHT 12
#define SYS_GFX_PUT_PIXEL 14
struct sys_gfx_put_pixel_args { int x; int y; unsigned int rgb; };
#define SYS_GFX_FILL_RECT 15
struct sys_gfx_fill_rect_args { int x; int y; int w; int h; unsigned int rgb; };
#define SYS_GFX_PRESENT_RECT 16
struct sys_gfx_present_rect_args { int x; int y; int w; int h; };
#define SYS_WINDOW_OPEN 22
struct sys_window_open_args { int x; int y; int w; int h; const char *title; };
#define SYS_WAIT_EVENT 24
#define RING3_EVENT_NONE 0
#define RING3_EVENT_CLICK 1
#define RING3_EVENT_CLOSED 2
#define RING3_EVENT_KEY 3
struct sys_wait_event_args { int type; int x; int y; char key; };

/* Same "eax=num/ebx=arg, int $0x80" ABI programs/hello/hello.c already
 * uses -- one shared helper here since paint.c makes many more syscalls
 * than hello.c's one-off proof did. */
static int syscall1(int num, int arg) {
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(num), "b"(arg) : "ecx", "edx", "memory");
    return result;
}

static int gfx_width(void) { return syscall1(SYS_GFX_WIDTH, 0); }
static int gfx_height(void) { return syscall1(SYS_GFX_HEIGHT, 0); }

static void gfx_put_pixel(int x, int y, unsigned int rgb) {
    struct sys_gfx_put_pixel_args a = { x, y, rgb };
    syscall1(SYS_GFX_PUT_PIXEL, (int)&a);
}

static void gfx_fill_rect(int x, int y, int w, int h, unsigned int rgb) {
    struct sys_gfx_fill_rect_args a = { x, y, w, h, rgb };
    syscall1(SYS_GFX_FILL_RECT, (int)&a);
}

static void gfx_present_rect(int x, int y, int w, int h) {
    struct sys_gfx_present_rect_args a = { x, y, w, h };
    syscall1(SYS_GFX_PRESENT_RECT, (int)&a);
}

static void window_open(int x, int y, int w, int h, const char *title) {
    struct sys_window_open_args a = { x, y, w, h, title };
    syscall1(SYS_WINDOW_OPEN, (int)&a);
}

/* Fields are OUTPUT params the syscall fills in -- the local `a` below
 * starts uninitialized, matching struct sys_wait_event_args's own
 * documented contract in syscall.h ("filled in by the syscall, not
 * read from by it"). */
static void wait_event(int *type, int *x, int *y, char *key) {
    struct sys_wait_event_args a;
    syscall1(SYS_WAIT_EVENT, (int)&a);
    *type = a.type;
    *x = a.x;
    *y = a.y;
    *key = a.key;
}

static void fs_delete(const char *path) {
    syscall1(SYS_DELETE, (int)path);
}

static void fs_create_file(const char *path, const void *data, unsigned int size) {
    struct sys_create_file_args a = { path, data, size };
    syscall1(SYS_CREATE_FILE, (int)&a);
}

static int fs_read_file(const char *path, void *buf, unsigned int buf_size, unsigned int *out_size) {
    struct sys_read_file_args a = { path, buf, buf_size, out_size };
    return syscall1(SYS_READ_FILE, (int)&a);
}

/* Same 5x7-glyph, 6px-advance layout as kernel/gfx/text.c's own
 * text_puts() -- issues one SYS_GFX_FILL_RECT per lit pixel instead of
 * calling gfx_fill_rect() directly, since this program has no access
 * to the kernel's real graphics.c. */
static void text_puts(int x, int y, const char *s, unsigned int rgb, int scale) {
    int cx = x;
    const char *p;
    for (p = s; *p; p++) {
        const char *const *glyph = font_glyph(*p);
        if (glyph) {
            int row, col;
            for (row = 0; row < 7; row++) {
                for (col = 0; col < 5; col++) {
                    if (glyph[row][col] == 'X') {
                        gfx_fill_rect(cx + col * scale, y + row * scale, scale, scale, rgb);
                    }
                }
            }
        }
        cx += 6 * scale;
    }
}

/* --- layout (window-relative offsets; GRID/CELL match the old
 * kernel-side canvas exactly, so the on-disk sprite format's 256-byte
 * row-major shape lines up) --- */
#define GRID 16
#define CELL 16
#define CANVAS_SIZE (GRID * CELL) /* 256 */
#define PALETTE_COLORS 16
#define OFF_X 8
#define CANVAS_OFF_Y 8
#define STRIP_OFF_Y (CANVAS_OFF_Y + CANVAS_SIZE + 4)  /* 268 */
#define STRIP_H 20
#define SWATCH_W (CANVAS_SIZE / PALETTE_COLORS)        /* 16 */
#define NAME_OFF_Y (STRIP_OFF_Y + STRIP_H + 8)         /* 296 */
#define NAME_H 20
#define NAME_W CANVAS_SIZE                              /* 256 */
#define BTN_OFF_Y (NAME_OFF_Y + NAME_H + 6)            /* 322 */
#define BTN_H 22
#define BTN_W ((CANVAS_SIZE - 8) / 2)                   /* 124 */
#define WIN_W (OFF_X + CANVAS_SIZE + 8)                 /* 272 */
#define WIN_H (BTN_OFF_Y + BTN_H + 8)                   /* 352 */

/* Same 16-color palette as the old kernel-side paint_palette[]
 * (kernel/kernel.c), copied verbatim so a sprite saved by the old
 * implementation still looks the same reopened here. */
static const unsigned int palette[PALETTE_COLORS] = {
    0x050607, 0xFFFFFF, 0xFF3B30, 0xFF9500, 0xFFEB3B, 0x00FF66, 0x2979FF, 0xB026FF,
    0x8D6E4C, 0xFF4FA3, 0x18E0E0, 0x0A6E3D, 0x1A2E8C, 0x808080, 0x2B2B2B, 0xCC3300,
};

static int grid[GRID][GRID];
static int current_color = 1; /* white -- visible default against the near-black eraser color at index 0 */
static char name[64] = "SPRITE";
static int name_len = 6;
static int win_x, win_y;

static void draw_canvas(void) {
    int row, col;
    for (row = 0; row < GRID; row++) {
        for (col = 0; col < GRID; col++) {
            gfx_fill_rect(win_x + OFF_X + col * CELL, win_y + CANVAS_OFF_Y + row * CELL, CELL, CELL,
                          palette[grid[row][col]]);
        }
    }
}

static void draw_strip(void) {
    int i;
    for (i = 0; i < PALETTE_COLORS; i++) {
        int sx = win_x + OFF_X + i * SWATCH_W;
        int sy = win_y + STRIP_OFF_Y;
        gfx_fill_rect(sx, sy, SWATCH_W, STRIP_H, palette[i]);
        if (i == current_color) {
            gfx_fill_rect(sx, sy, SWATCH_W, 2, 0x00FF66);
            gfx_fill_rect(sx, sy + STRIP_H - 2, SWATCH_W, 2, 0x00FF66);
        }
    }
}

static void draw_name_field(void) {
    int fx = win_x + OFF_X, fy = win_y + NAME_OFF_Y;
    gfx_fill_rect(fx - 1, fy - 1, NAME_W + 2, NAME_H + 2, 0x00FF66);
    gfx_fill_rect(fx, fy, NAME_W, NAME_H, 0x0B1712);
    text_puts(fx + 4, fy + (NAME_H - 7) / 2, name, 0xD4E6DB, 1);
}

static void draw_buttons(void) {
    int sx = win_x + OFF_X, sy = win_y + BTN_OFF_Y;
    int lx = sx + BTN_W + 8;
    gfx_fill_rect(sx, sy, BTN_W, BTN_H, 0x123322);
    gfx_fill_rect(sx, sy, BTN_W, 1, 0x00FF66);
    text_puts(sx + 8, sy + (BTN_H - 7) / 2, "SAVE", 0xD4E6DB, 1);
    gfx_fill_rect(lx, sy, BTN_W, BTN_H, 0x123322);
    gfx_fill_rect(lx, sy, BTN_W, 1, 0x00FF66);
    text_puts(lx + 8, sy + (BTN_H - 7) / 2, "LOAD", 0xD4E6DB, 1);
}

static void draw_all(void) {
    draw_canvas();
    draw_strip();
    draw_name_field();
    draw_buttons();
    gfx_present_rect(win_x, win_y, WIN_W, WIN_H);
}

/* Absolute-screen-coordinate hit tests -- click events report absolute
 * coordinates (see struct sys_wait_event_args's own doc-comment in
 * syscall.h), same convention every gfx/window syscall already uses. */
static int hit_canvas(int cx, int cy, int *col, int *row) {
    int lx = cx - (win_x + OFF_X);
    int ly = cy - (win_y + CANVAS_OFF_Y);
    if (lx < 0 || lx >= CANVAS_SIZE || ly < 0 || ly >= CANVAS_SIZE) {
        return 0;
    }
    *col = lx / CELL;
    *row = ly / CELL;
    return 1;
}

static int hit_strip(int cx, int cy, int *idx) {
    int lx = cx - (win_x + OFF_X);
    int ly = cy - (win_y + STRIP_OFF_Y);
    if (lx < 0 || lx >= CANVAS_SIZE || ly < 0 || ly >= STRIP_H) {
        return 0;
    }
    *idx = lx / SWATCH_W;
    return 1;
}

static int hit_rect(int cx, int cy, int rx, int ry, int rw, int rh) {
    return cx >= rx && cx < rx + rw && cy >= ry && cy < ry + rh;
}

/* Builds "/HOME/" + the filename buffer, uppercased -- same convention
 * the old kernel-side paint_build_sprite_path() used, so a sprite this
 * program saves lands at the same path the old one would have.
 * Returns 0 (leaving out untouched) if the name is empty. */
static int build_path(char *out, int out_max) {
    int pos = 0;
    int i;
    const char *prefix = "/HOME/";

    if (name_len == 0) {
        return 0;
    }
    for (i = 0; prefix[i] && pos < out_max - 1; i++) {
        out[pos++] = prefix[i];
    }
    for (i = 0; i < name_len && pos < out_max - 1; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        out[pos++] = c;
    }
    out[pos] = 0;
    return 1;
}

/* Writes the grid to /HOME/<name> as 256 raw bytes, one per cell,
 * row-major -- same format and same fs_delete()+fs_create_file() shape
 * the old kernel-side SAVE button used. */
static void do_save(void) {
    char path[80];
    unsigned char bytes[GRID * GRID];
    int r, c;

    if (!build_path(path, (int)sizeof(path))) {
        return;
    }
    for (r = 0; r < GRID; r++) {
        for (c = 0; c < GRID; c++) {
            bytes[r * GRID + c] = (unsigned char)grid[r][c];
        }
    }
    fs_delete(path);
    fs_create_file(path, bytes, sizeof(bytes));
}

/* Reads /HOME/<name> back into the grid -- only accepts an exact
 * 256-byte file (same "reject a truncated/wrong-format file outright"
 * stance the old kernel-side LOAD button took), clamping any
 * out-of-range byte to 0 so a stray value can't index palette[] out of
 * bounds. */
static void do_load(void) {
    char path[80];
    unsigned char bytes[GRID * GRID];
    unsigned int out_size;
    int r, c;

    if (!build_path(path, (int)sizeof(path))) {
        return;
    }
    if (fs_read_file(path, bytes, sizeof(bytes), &out_size) != 0 || out_size != sizeof(bytes)) {
        return;
    }
    for (r = 0; r < GRID; r++) {
        for (c = 0; c < GRID; c++) {
            unsigned char v = bytes[r * GRID + c];
            grid[r][c] = (v < PALETTE_COLORS) ? v : 0;
        }
    }
}

void _start(void) {
    int type, x, y;
    char key;

    win_x = 340 * gfx_width() / 640;  /* same 640x480 baseline every other window's own placement math uses */
    win_y = 80 * gfx_height() / 480;

    window_open(win_x, win_y, WIN_W, WIN_H, "RAVE-OS PAINT");
    draw_all();

    for (;;) {
        wait_event(&type, &x, &y, &key);
        if (type == RING3_EVENT_CLOSED) {
            break;
        } else if (type == RING3_EVENT_CLICK) {
            int col, row, idx;
            if (hit_canvas(x, y, &col, &row)) {
                grid[row][col] = current_color;
                gfx_fill_rect(win_x + OFF_X + col * CELL, win_y + CANVAS_OFF_Y + row * CELL, CELL, CELL,
                              palette[current_color]);
                gfx_present_rect(win_x + OFF_X + col * CELL, win_y + CANVAS_OFF_Y + row * CELL, CELL, CELL);
            } else if (hit_strip(x, y, &idx)) {
                current_color = idx;
                draw_strip();
                gfx_present_rect(win_x + OFF_X, win_y + STRIP_OFF_Y, CANVAS_SIZE, STRIP_H);
            } else if (hit_rect(x, y, win_x + OFF_X, win_y + BTN_OFF_Y, BTN_W, BTN_H)) {
                do_save();
            } else if (hit_rect(x, y, win_x + OFF_X + BTN_W + 8, win_y + BTN_OFF_Y, BTN_W, BTN_H)) {
                do_load();
                draw_canvas();
                gfx_present_rect(win_x + OFF_X, win_y + CANVAS_OFF_Y, CANVAS_SIZE, CANVAS_SIZE);
            }
        } else if (type == RING3_EVENT_KEY) {
            if (key == '\b') {
                if (name_len > 0) {
                    name_len--;
                }
            } else if (key >= 32 && key < 127 && name_len < (int)sizeof(name) - 1) {
                name[name_len++] = key;
            }
            name[name_len] = 0;
            draw_name_field();
            gfx_present_rect(win_x + OFF_X, win_y + NAME_OFF_Y, NAME_W, NAME_H);
        }
    }

    /* The window is closed -- but enter_ring3() is a one-way jump
     * (kernel/arch/ring3.asm), so this program's own execution never
     * returns to kmain()'s original call site. Keep calling
     * SYS_WAIT_EVENT forever, discarding whatever it returns (no
     * window means no legitimate event will ever arrive): its own
     * blocking loop (ring3_wait_event() in kernel/kernel.c) is what
     * re-drives kmain_frame() at all, which is the only thing keeping
     * the rest of the desktop (every other window) responsive after
     * this one closes. See docs/superpowers/specs/2026-08-29-
     * standalone-paint-design.md's Design section. */
    for (;;) {
        wait_event(&type, &x, &y, &key);
    }
}
```

- [ ] **Step 3: Create `programs/paint/Makefile`**

```makefile
# Builds paint.bin, the standalone RAVE-OS paint program -- a flat
# binary fixed at 0x00200000 by paint.ld, loadable via
# kernel/kernel.c's program_load_and_run(). Same cross-toolchain and
# base flags as programs/hello/Makefile; requires $HOME/opt/cross/bin
# on PATH.

CC := i686-elf-gcc
LD := i686-elf-ld
OBJCOPY := i686-elf-objcopy

CFLAGS := -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -Wall -Wextra

.PHONY: all clean

all: paint.bin

paint.o: paint.c font.h
	$(CC) $(CFLAGS) -c paint.c -o paint.o

font.o: font.c font.h
	$(CC) $(CFLAGS) -c font.c -o font.o

paint.elf: paint.o font.o paint.ld
	$(LD) -m elf_i386 -T paint.ld -nostdlib -o paint.elf paint.o font.o

paint.bin: paint.elf
	$(OBJCOPY) -O binary paint.elf paint.bin

clean:
	rm -f paint.o font.o paint.elf paint.bin
```

- [ ] **Step 4: Build it and verify the entry point**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd programs/paint
make clean && make
i686-elf-nm paint.elf | grep _start
i686-elf-objdump -f paint.elf | grep "start address"
```

Expected: `make` produces `paint.bin` with no errors/warnings; `nm` shows `00200000 T _start`; `objdump`'s start address is also `0x00200000`.

- [ ] **Step 5: Commit**

```bash
git add programs/paint/paint.ld programs/paint/paint.c programs/paint/Makefile
git commit -m "programs/paint: add the standalone paint program"
```

---

### Task 3: Embed paint.bin into the kernel and seed it to disk

**Files:**
- Modify: `kernel/kernel.c` (near the existing `hello_bin`/`/BIN/USERPROG.BIN` seeding block, ~line 3570-3577)

**Interfaces:**
- Consumes: `programs/paint/paint.bin` (Task 2's build artifact).
- Produces: `/BIN/PAINT.BIN` on `fs.img`, readable by `program_load_and_run()` (already-shipped, unmodified).

- [ ] **Step 1: Build the real `paint.bin` and generate its C byte array**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd programs/paint && make clean && make && cd -
python3 -c "
data = open('programs/paint/paint.bin', 'rb').read()
print('static const unsigned char paint_bin[] = {')
for i in range(0, len(data), 16):
    row = data[i:i+16]
    print('            ' + ', '.join('0x%02x' % b for b in row) + ',')
print('        };')
print('/* size:', len(data), 'bytes */')
" > /tmp/paint_bin_array.txt
cat /tmp/paint_bin_array.txt
```

Expected: a `static const unsigned char paint_bin[] = { ... };` block prints, with a trailing size comment.

- [ ] **Step 2: Insert it into `kernel/kernel.c`**

Find the existing `hello_bin`/`/BIN/USERPROG.BIN` seeding block (search for `USERPROG.BIN`), and add a new block immediately after it:

```c
    /* Seeds the standalone paint program onto disk -- same technique
     * as hello_bin/USERPROG.BIN above: a compiled, freestanding flat
     * binary (programs/paint/, fixed at PROGRAM_LOAD_ADDR by its own
     * linker script; see docs/superpowers/specs/2026-08-29-standalone-
     * paint-design.md), embedded here since this repo has no host-side
     * disk-image tooling. Launched by the Start Menu's PAINT item
     * (see kmain()'s start-menu click dispatch), not auto-run at boot. */
    {
        static const unsigned char paint_bin[] = {
            /* paste the generated array body from /tmp/paint_bin_array.txt here */
        };
        fs_create_file("/BIN/PAINT.BIN", paint_bin, (unsigned int)sizeof(paint_bin));
    }
```

Paste the actual generated byte rows from `/tmp/paint_bin_array.txt` into the `paint_bin[]` initializer (replace the placeholder comment line with the real generated rows).

- [ ] **Step 3: Rebuild the kernel and confirm it compiles clean**

```bash
cd boot && make 2>&1 | tail -30
```

Expected: build succeeds with no new warnings/errors.

- [ ] **Step 4: Commit**

```bash
git add kernel/kernel.c
git commit -m "kernel: embed and seed the standalone paint binary"
```

---

### Task 4: Start Menu launcher

**Files:**
- Modify: `kernel/gui/startmenu.h`
- Modify: `kernel/gui/startmenu.c`
- Modify: `kernel/kernel.c` (start-menu click dispatch, ~line 2355-2391)

**Interfaces:**
- Consumes: `program_load_and_run(const char *path)` (already shipped, `kernel/kernel.c`, unmodified signature).
- Produces: `STARTMENU_ITEM_PAINT` — a new symbolic constant every later reference in this task uses.

- [ ] **Step 1: Add the item constant in `kernel/gui/startmenu.h`**

Replace:
```c
#define STARTMENU_ITEM_FORTH 0
#define STARTMENU_ITEM_FILES 1
#define STARTMENU_ITEM_SHELL 2
#define STARTMENU_ITEM_CONFIG 3
#define STARTMENU_ITEM_GAMES 4
#define STARTMENU_ITEM_FX 5
#define STARTMENU_ITEM_EXIT 6
#define STARTMENU_ITEM_COUNT 7
```
with:
```c
#define STARTMENU_ITEM_FORTH 0
#define STARTMENU_ITEM_FILES 1
#define STARTMENU_ITEM_SHELL 2
#define STARTMENU_ITEM_PAINT 3
#define STARTMENU_ITEM_CONFIG 4
#define STARTMENU_ITEM_GAMES 5
#define STARTMENU_ITEM_FX 6
#define STARTMENU_ITEM_EXIT 7
#define STARTMENU_ITEM_COUNT 8
```

Also update the comment above (currently: `"FORTH, FILES, and SHELL are the three windows that exist at all now, all launched directly..."`) to add PAINT to that sentence, since it's now a fourth directly-launched item: `"FORTH, FILES, SHELL, and PAINT are the windows/programs launched directly..."`.

- [ ] **Step 2: Add the label in `kernel/gui/startmenu.c`**

In `startmenu_item_label()`, add a case (position doesn't matter inside the switch, but keep it visually grouped with the other direct-launch items):
```c
    case STARTMENU_ITEM_SHELL:
        return "SHELL";
    case STARTMENU_ITEM_PAINT:
        return "PAINT";
    case STARTMENU_ITEM_CONFIG:
        return "CONFIG";
```

- [ ] **Step 3: Wire the click handler in `kernel/kernel.c`**

Add a new `else if` branch to the start-menu item dispatch (alongside `STARTMENU_ITEM_FORTH`/`STARTMENU_ITEM_FILES`/etc., ~line 2355-2391):
```c
                } else if (item == STARTMENU_ITEM_SHELL) {
                    windows[WIN_KIND_SHELL].state = WINDOW_OPEN;
                    raise_window(z_order, WIN_KIND_SHELL);
                } else if (item == STARTMENU_ITEM_PAINT) {
                    /* Unlike every other item above, this never returns --
                     * program_load_and_run() -> enter_ring3() is a one-way
                     * jump into ring 3 (kernel/arch/ring3.asm). The rest of
                     * this function's own stack frame (and kmain()'s
                     * original call to it) is simply abandoned; paint.bin's
                     * own event loop is what keeps the desktop alive from
                     * here on, exactly the way ring3_wait_event() already
                     * does for the generic ring-3 window. See
                     * docs/superpowers/specs/2026-08-29-standalone-paint-
                     * design.md. */
                    extern void program_load_and_run(const char *path);
                    program_load_and_run("/BIN/PAINT.BIN");
                } else if (item == STARTMENU_ITEM_CONFIG) {
```

(`program_load_and_run()` is already defined earlier in this same file, but it's declared `static`-free/file-scope already — check whether it needs the `extern` forward declaration here or is already visible at this call site by normal C file-order; if `program_load_and_run()`'s definition already appears *before* this dispatch code in the file, drop the `extern` line and call it directly.)

- [ ] **Step 4: Rebuild and verify it compiles clean**

```bash
cd boot && make 2>&1 | tail -30
```

Expected: build succeeds, no warnings/errors, no "implicit declaration" warning for `program_load_and_run`.

- [ ] **Step 5: Commit**

```bash
git add kernel/gui/startmenu.h kernel/gui/startmenu.c kernel/kernel.c
git commit -m "gui: add a Start Menu PAINT entry that launches the standalone binary"
```

---

### Task 5: Verify the standalone binary works, before touching the old kernel-side PAINT

This is a manual/scripted verification checkpoint, not a code change -- confirms Tasks 1-4 actually work end to end while the old kernel-side PAINT implementation is still present and untouched (so a failure here is unambiguously about the new code, not a removal mistake).

**Files:** none (verification only).

- [ ] **Step 1: Build the full disk images**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd boot && make && make fs.img
```

Expected: both succeed with no errors.

- [ ] **Step 2: Boot headless with a QEMU monitor socket**

```bash
cd boot
qemu-system-i386 -accel kvm -display none -device sb16 \
  -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 \
  -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 \
  -monitor unix:/tmp/raveos-qemu.sock,server,nowait &
sleep 3
```

- [ ] **Step 3: Click the Start Menu button, then the PAINT item**

Use the monitor's `mouse_move`/`mouse_button` commands (via `socat -` to `/tmp/raveos-qemu.sock`, or the project's existing headless-click helper if one exists in `boot/` already -- check for a script before writing raw monitor commands by hand) to click the Start Menu button (bottom-left), then click the "PAINT" item in the popup, then take a screendump:

```bash
echo "screendump /tmp/paint_open.ppm" | socat - unix:/tmp/raveos-qemu.sock
```

Expected: `/tmp/paint_open.ppm` shows the PAINT window open: an empty (all index-0) 256x256 canvas, the 16-swatch color strip below it, a "SPRITE" filename field, and SAVE/LOAD buttons.

- [ ] **Step 4: Paint a few cells with different colors**

Click a non-default swatch in the strip, then click 2-3 canvas cells; screendump again. Expected: the clicked cells show the selected color, matching `palette[]`'s values.

- [ ] **Step 5: Verify save/load round-trip**

Click SAVE (default name "SPRITE" is already filled in), then click a different canvas cell to change it, then click LOAD; screendump. Expected: the grid reverts to the pattern from before the post-save click -- proving the round-trip went through real `SYS_CREATE_FILE`/`SYS_READ_FILE` file I/O, not just in-memory state.

- [ ] **Step 6: Close the window and verify the desktop survives**

Click the window's close button, screendump. Expected: the PAINT window is gone. Then click the Start Menu button and open FILES; screendump. Expected: FILES opens normally and browsing to `/HOME` shows the saved sprite file -- proving `paint.bin`'s idle tail loop is still re-driving `kmain_frame()` and the rest of the desktop is fully responsive.

- [ ] **Step 7: Shut down QEMU**

```bash
echo "quit" | socat - unix:/tmp/raveos-qemu.sock
```

If any step fails, fix the bug in `programs/paint/paint.c` (or the Task 3/4 wiring), rebuild, and re-run this task from Step 1 before proceeding to Task 6 -- do not start deleting the old kernel-side implementation until this passes cleanly.

---

### Task 6: Remove the in-kernel PAINT window, state, and UI

**Files:**
- Modify: `kernel/kernel.c` (many sites, listed below)

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing new -- pure deletion. After this task, `WIN_KIND_RING3` is renumbered to `4` and `MAX_WINDOWS` to `5` (both symbolic constants; every reference is by name, not raw number, so this renumbering is safe).

Work through these removals in order. After each, leave the surrounding code exactly as it was except for the described change -- do not "clean up" anything not listed here.

- [ ] **Step 1: Enum/window-kind constants (~line 61-77)**

Change:
```c
#define MAX_WINDOWS 6
#define WIN_KIND_FORTH 0
#define WIN_KIND_FILES 1
#define WIN_KIND_SHELL 2
#define WIN_KIND_EDITOR 3
#define WIN_KIND_PAINT 4
/* Owned by a ring-3 program ... */
#define WIN_KIND_RING3 5
```
to:
```c
#define MAX_WINDOWS 5
#define WIN_KIND_FORTH 0
#define WIN_KIND_FILES 1
#define WIN_KIND_SHELL 2
#define WIN_KIND_EDITOR 3
/* Owned by a ring-3 program ... */
#define WIN_KIND_RING3 4
```
(keep the `WIN_KIND_RING3` doc-comment as-is, just renumber the `#define`.)

- [ ] **Step 2: PAINT layout macros, `struct paint`, its globals, and `paint_palette[]` (~line 442-488)**

Delete the entire block from `#define PAINT_GRID_SIZE 16` through the end of the `paint_palette[]` array initializer, i.e. everything from:
```c
#define PAINT_GRID_SIZE 16
#define PAINT_CELL_PX 16
...
static const uint32_t paint_palette[PAINT_PALETTE_COLORS] = {
    0x050607, 0xFFFFFF, 0xFF3B30, 0xFF9500, 0xFFEB3B, 0x00FF66, 0x2979FF, 0xB026FF,
    0x8D6E4C, 0xFF4FA3, 0x18E0E0, 0x0A6E3D, 0x1A2E8C, 0x808080, 0x2B2B2B, 0xCC3300,
};
```
Keep the `static struct window windows[MAX_WINDOWS];` / `static int z_order[MAX_WINDOWS];` lines that follow (those are shared by every window kind, not paint-specific) -- only their preceding doc-comment references `windows[WIN_KIND_PAINT]`/`raise_window(z_order, WIN_KIND_PAINT)` as examples; leave the comment's prose as historical color (it still makes sense read generically) unless it no longer parses without the example -- if so, trim just the `WIN_KIND_PAINT`-specific example clause.

- [ ] **Step 3: `struct window_content` fields (~line 549-552)**

Delete these four lines from the struct definition:
```c
    struct paint *pt;
    struct console_input *paint_name_input;
    struct button *paint_save_btn;
    struct button *paint_load_btn;
```

- [ ] **Step 4: `move_window_content()`'s PAINT branch (~line 592-603)**

Delete the entire `} else if (kind == WIN_KIND_PAINT) { ... }` branch, including its comment:
```c
    } else if (kind == WIN_KIND_PAINT) {
        /* The canvas/palette are drawn directly from wc->pt's own
         * fixed-size arrays at an offset from the window's own x/y
         * (see draw_paint_group()) -- nothing about wc->pt itself needs
         * to move when the window is dragged, only its own widgets. */
        wc->paint_name_input->x += applied_dx;
        wc->paint_name_input->y += applied_dy;
        wc->paint_save_btn->x += applied_dx;
        wc->paint_save_btn->y += applied_dy;
        wc->paint_load_btn->x += applied_dx;
        wc->paint_load_btn->y += applied_dy;
    }
```
so the function's final branch (`WIN_KIND_EDITOR`) closes with `}` immediately followed by the function's own closing `}`.

- [ ] **Step 5: `draw_paint_group()` and its preceding comment (~line 634-699)**

Delete the entire function and the comment block immediately above it (the one starting `/* The fifth window: a 16x16 pixel canvas...`), through the function's closing `}`.

- [ ] **Step 6: Paint-specific Forth hooks (~line 868-983)**

Delete these functions in full, in order (they are contiguous except where noted):
- `forth_hook_paint_open()` (~868-882)
- `forth_hook_pixel()` (~884-898)
- `paint_mouse_cell()` static helper, including its preceding comment (~900-929)
- `forth_hook_mouse_x()` (~931-937)
- `forth_hook_mouse_y()` (~939-945)
- `forth_hook_mouse_down()` (~947-956)
- `forth_hook_mouse_right_down()` (~958-969)
- `forth_hook_current_color()` (~971-973)

**Do NOT delete** `forth_hook_yield()` (~975-979, immediately after `forth_hook_current_color()`) -- it's a general Forth scheduler-yield hook used by every compiled `BEGIN...UNTIL` loop, not paint-specific.

Then delete `forth_hook_window_closed()` (~981-983, immediately after `forth_hook_yield()`).

Then delete `paint_swatch_hit_test()` and `paint_popup_grid_hit_test()`, including their preceding comments (~985-1017).

Then delete `paint_build_sprite_path()`, including its preceding comment (~1019-1045).

Also trim the section-intro comment above this whole area (~line 701-714, starting `/* forth_hooks.h implementations -- forth.c's only window into graphics/mouse state...`) -- it references `draw_paint_group()` by name (now deleted) as historical color explaining why these hooks live in this spot. Replace it with a shorter version that drops the dangling reference:
```c
/* forth_hooks.h implementations -- forth.c's only window into
 * beep/scheduler-yield state, both implemented in kernel.c. The
 * graphics/mouse hooks that used to live here (added for the PAINT
 * Forth words) were removed when PAINT moved to a standalone ring-3
 * binary -- see docs/superpowers/specs/2026-08-29-standalone-paint-
 * design.md. */
```

- [ ] **Step 7: `draw_window_by_index()`'s PAINT branch (~line 1798-1799)**

Delete:
```c
    } else if (idx == WIN_KIND_PAINT) {
        draw_paint_group(&windows[idx], wc->pt, wc->paint_name_input, wc->paint_save_btn, wc->paint_load_btn);
```
so the function flows directly from the `WIN_KIND_EDITOR` branch to the `WIN_KIND_RING3` branch.

- [ ] **Step 8: Damage-tracking snapshot locals (~line 2219-2230, ~2248-2251)**

Delete these declarations:
```c
        int old_paint_current_color = paint.current_color;
        int old_paint_save_btn_hovered = paint_save_btn.hovered;
        int old_paint_save_btn_pressed = paint_save_btn.pressed;
        int old_paint_load_btn_hovered = paint_load_btn.hovered;
        int old_paint_load_btn_pressed = paint_load_btn.pressed;
        int old_paint_palette_popup_open = paint.palette_popup_open;
        uint32_t old_paint_palette_hidden_mask = paint.palette_hidden_mask;
        int old_paint_grid_generation = paint.grid_generation;
        int old_paint_name_input_len = paint_name_input.len;
        int old_paint_name_input_cursor = paint_name_input.cursor;
        int old_paint_name_input_focused = paint_name_input.focused;
        char old_paint_name_input_text[CONSOLE_INPUT_MAX + 1];
```
and this text-snapshot loop:
```c
        for (i = 0; paint_name_input.text[i]; i++) {
            old_paint_name_input_text[i] = paint_name_input.text[i];
        }
        old_paint_name_input_text[i] = 0;
```

- [ ] **Step 9: Close-button PAINT special-case (~line 2426-2430)**

Delete:
```c
                        if (target == WIN_KIND_PAINT && paint_program_slot >= 0) {
                            scheduler_request_close(paint_program_slot);
                        }
```
leaving the `WIN_KIND_RING3` close-button special-case (`if (target == WIN_KIND_RING3) { ring3_event_pending = RING3_EVENT_CLOSED; }`) untouched immediately after.

- [ ] **Step 10: Drag-start palette-popup-close (~line 2446-2453)**

Delete the comment and line:
```c
                        /* Dragging any window while PAINT's popup is
                         * open would leave it rendered at a stale
                         * position relative to a window that just
                         * moved -- simplest fix is closing it
                         * outright rather than threading a second
                         * movable position through
                         * move_window_content(). */
                        paint.palette_popup_open = 0;
```
leaving:
```c
                    raise_window(z_order, target);
                    if (window_titlebar_hit_test(&windows[target], cx, cy)) {
                        dragging_window = target;
                    }
```

- [ ] **Step 11: `paint_is_topmost` local and its focus-assignment use (~line 2484, ~2517)**

Delete the declaration:
```c
                int paint_is_topmost = topmost == WIN_KIND_PAINT;
```
and the focus-assignment line:
```c
                    paint_name_input.focused = paint_is_topmost && console_input_hit_test(&paint_name_input, cx, cy);
```
(leave every other window's own `*.focused = ...` line on either side untouched).

- [ ] **Step 12: Palette popup click routing and SAVE/LOAD click handling (~line 2699-2796)**

Delete the entire block from the `/* Click routing for the palette-chooser popup ... */` comment through the end of the LOAD button's `paint_load_btn.pressed = ...` line -- this is the whole remaining PAINT-specific section of the per-frame click-handling code, including:
- the palette-popup open/select/dismiss `if (click_edge) { if (paint_is_topmost) { ... } }` block
- the right-click hide/restore block
- the SAVE button hit-test/click/write block
- the LOAD button hit-test/click/read block

Everything from `if (click_edge) {` (the one immediately preceded by the "Click routing for the palette-chooser popup" comment) through `paint_load_btn.pressed = paint_load_btn.hovered && left_held;` should be gone. Confirm what comes immediately after in the original (the next section, unrelated to PAINT) is now what directly follows whatever precedes this block.

- [ ] **Step 13: Filename-field keyboard dispatch (~line 2993-3004)**

Delete:
```c
            } else if (paint_name_input.focused && c == KEY_LEFT) {
                console_input_move_cursor(&paint_name_input, -1);
            } else if (paint_name_input.focused && c == KEY_RIGHT) {
                console_input_move_cursor(&paint_name_input, 1);
            } else if (paint_name_input.focused) {
                /* No Enter-triggered action here, unlike FILES' own
                 * name_input -- SAVE only ever fires from clicking the
                 * SAVE button itself, never from typing. Enter just
                 * inserts nothing (console_input_feed_char()'s return
                 * value is ignored) since a filename has no meaningful
                 * use for a literal newline. */
                console_input_feed_char(&paint_name_input, c);
```
so the chain flows directly from the `ed.focused` branches to the `ring3_focused` branch:
```c
            } else if (ed.focused) {
                editor_feed_char(&ed, c);
            } else if (ring3_focused) {
                ring3_event_pending = RING3_EVENT_KEY;
                ring3_event_key = c;
            }
```

- [ ] **Step 14: `touched[WIN_KIND_PAINT]` damage calculation (~line 3076-3100)**

Delete the whole comment block and calculation:
```c
            /* paint.grid[][]'s own content changes (via PIXEL) are
             * caught via paint.grid_generation (bumped unconditionally
             ...
             * grid_generation removes that reliance on coincidence. */
            touched[WIN_KIND_PAINT] = touched[WIN_KIND_PAINT] || (paint.current_color != old_paint_current_color) ||
                                      (paint.grid_generation != old_paint_grid_generation) ||
                                      (paint_name_input.len != old_paint_name_input_len) ||
                                      (paint_name_input.cursor != old_paint_name_input_cursor) ||
                                      (paint_name_input.focused != old_paint_name_input_focused) ||
                                      !str_eq(paint_name_input.text, old_paint_name_input_text) ||
                                      (paint_save_btn.hovered != old_paint_save_btn_hovered) ||
                                      (paint_save_btn.pressed != old_paint_save_btn_pressed) ||
                                      (paint_load_btn.hovered != old_paint_load_btn_hovered) ||
                                      (paint_load_btn.pressed != old_paint_load_btn_pressed) ||
                                      (paint.palette_popup_open != old_paint_palette_popup_open) ||
                                      (paint.palette_hidden_mask != old_paint_palette_hidden_mask);
```

- [ ] **Step 15: The three `struct window_content` literal sites (~line 2340-2349, ~3102-3110, ~3597-3606)**

In each of the three places a `struct window_content wc = { ... };` is built, delete these two lines from the initializer:
```c
                            .pt = &paint, .paint_name_input = &paint_name_input, .paint_save_btn = &paint_save_btn,
                            .paint_load_btn = &paint_load_btn,
```
(exact indentation varies per call site -- match whatever's already there) so the initializer's last field is `.save_btn = &save_btn,` (drag-path and boot-time sites) or the equivalent trailing field, each followed directly by the closing `};`.

- [ ] **Step 16: `kmain()`'s PAINT window init block (~line 3330-3341)**

Delete:
```c
    windows[WIN_KIND_PAINT].x = 340 * w / BASELINE_W;
    windows[WIN_KIND_PAINT].y = 80 * h / BASELINE_H;
    windows[WIN_KIND_PAINT].w = 272;
    windows[WIN_KIND_PAINT].h = 356;
    windows[WIN_KIND_PAINT].title = "RAVE-OS PAINT";
    windows[WIN_KIND_PAINT].accent_color = 0xFF3B30; /* red, same as PAINT's own palette index 2 */
    /* Closed at boot, opened only via the PAINT Forth word (Task 3) --
     ...
     */
    windows[WIN_KIND_PAINT].state = WINDOW_CLOSED;
    windows[WIN_KIND_PAINT].minimize_hovered = 0;
    windows[WIN_KIND_PAINT].close_hovered = 0;
```
(read the actual comment body at this site before deleting -- delete the whole comment along with the five `windows[WIN_KIND_PAINT].*` assignment lines it documents.)

- [ ] **Step 17: `z_order[]` init (~line 3359-3364)**

Change:
```c
    z_order[0] = WIN_KIND_FORTH;
    z_order[1] = WIN_KIND_FILES;
    z_order[2] = WIN_KIND_SHELL;
    z_order[3] = WIN_KIND_EDITOR;
    z_order[4] = WIN_KIND_PAINT;
    z_order[5] = WIN_KIND_RING3;
```
to:
```c
    z_order[0] = WIN_KIND_FORTH;
    z_order[1] = WIN_KIND_FILES;
    z_order[2] = WIN_KIND_SHELL;
    z_order[3] = WIN_KIND_EDITOR;
    z_order[4] = WIN_KIND_RING3;
```

- [ ] **Step 18: `paint_name_input`/`paint_save_btn`/`paint_load_btn` init block (~line 3461-3489)**

Delete every line initializing `paint_name_input.*`, `paint_save_btn.*`, `paint_load_btn.*`, `paint.current_color`, and `paint.opened_once` (the whole block from `paint_name_input.x = windows[WIN_KIND_PAINT].x + 8;` through `paint.opened_once = 0;`), including any blank lines/comments that exist solely to separate these three widgets' setup from each other (but keep the blank line separating this whole block from whatever unrelated init code comes immediately before/after it).

- [ ] **Step 19: `kmain()`'s `seed_bin_paint_script()` call (~line 3546)**

Delete the line:
```c
    seed_bin_paint_script();
```
(this function itself is deleted in Task 7 -- deleting the call site here first, or in either order within the same commit, is fine as long as both are gone before the next build.)

- [ ] **Step 20: Grep-verify no dangling references remain**

```bash
grep -ni "paint" kernel/kernel.c
```

Expected: every remaining hit is the plain English word (e.g. "painted", "repaint", "PAINT's own palette index" in an unrelated accent-color comment at ~line 3169/3200/3290/3305 -- these describe *other* windows' accent colors by comparison to PAINT's old palette and are just historical color, harmless to leave) -- **zero** hits of `WIN_KIND_PAINT`, `struct paint`, `paint_save_btn`, `paint_load_btn`, `paint_name_input`, `paint_program_slot`, `paint_palette`, `PAINT_GRID_SIZE`, `draw_paint_group`, `paint_swatch_hit_test`, `paint_popup_grid_hit_test`, `paint_mouse_cell`, `paint_build_sprite_path`. If any remain, resolve them before moving on -- they're compile errors waiting to happen.

- [ ] **Step 21: Build**

```bash
cd boot && make 2>&1 | tail -40
```

Expected: clean build, no errors, no warnings about unused/undeclared identifiers.

- [ ] **Step 22: Commit**

```bash
git add kernel/kernel.c
git commit -m "kernel: remove the in-kernel PAINT window, state, and UI"
```

---

### Task 7: Remove the Forth PAINT integration

**Files:**
- Modify: `kernel/forth/forth_hooks.h`
- Modify: `kernel/forth/forth.c`
- Modify: `kernel/kernel.c` (the `BIN_PAINT_PATH`/`seed_bin_paint_script()` block, ~line 1340-1417)

**Interfaces:** none -- pure deletion.

- [ ] **Step 1: `kernel/forth/forth_hooks.h`**

Delete these declarations (and their doc-comments):
```c
void forth_hook_paint_open(void);
```
```c
void forth_hook_pixel(int x, int y, int color);

/* Canvas-relative cell column/row, 0..15, or -1 if the cursor isn't
 * currently over the PAINT window's canvas at all (window closed,
 * cursor elsewhere, or over the palette/SAVE area instead). */
int forth_hook_mouse_x(void);
int forth_hook_mouse_y(void);

int forth_hook_mouse_down(void);       /* 1 if the left button is currently held, 0 otherwise */
int forth_hook_mouse_right_down(void); /* 1 if the right button is currently held, 0 otherwise */
int forth_hook_current_color(void);    /* the natively-selected palette index, 0..7 */
```
and:
```c
/* 1 if the PAINT window has been closed (state != WINDOW_OPEN), 0
 * otherwise -- lets a script's own loop condition notice its window
 * closed instead of only ever checking its own domain-specific exit
 * condition (PLOOP's is MOUSE-RIGHT-DOWN?). */
int forth_hook_window_closed(void);
```

Keep `void forth_hook_beep(void);` and `void forth_hook_yield(void);` (both general-purpose, not paint-specific) and the entire synth block below them untouched.

Update the file's top doc-comment:
```c
/* forth.c's only window into graphics/mouse state -- implemented in
 * kernel.c, which already owns all of it (the window array, the paint
 * canvas, the live cursor position). forth.c itself has no
 * graphics.h/mouse.h/window.h dependency; it only sees these eight
 * extension points. See docs/superpowers/specs/2026-08-16-paint-design.md. */
```
to:
```c
/* forth.c's only window into kernel-owned state it can't reach
 * directly (beep, scheduler-yield) -- implemented in kernel.c.
 * forth.c itself has no graphics.h/mouse.h/window.h dependency. The
 * PAINT-specific graphics/mouse hooks that used to live here were
 * removed when PAINT moved to a standalone ring-3 binary -- see
 * docs/superpowers/specs/2026-08-29-standalone-paint-design.md. */
```

- [ ] **Step 2: `kernel/forth/forth.c` -- delete the primitive functions**

Delete `prim_paint()` (~line 285-288):
```c
static void prim_paint(struct forth_vm *vm) {
    (void)vm;
    forth_hook_paint_open();
}
```

Delete `prim_pixel()`, `prim_mouse_x()`, `prim_mouse_y()`, `prim_mouse_down()`, `prim_mouse_right_down()`, `prim_window_closed()`, `prim_current_color()` (~line 474-511) -- all seven functions, contiguous.

- [ ] **Step 3: `kernel/forth/forth.c` -- delete the word-table entries**

In the `primitives[]` table, change:
```c
    {"PAINT", prim_paint}, {"BEEP", prim_beep},
```
to:
```c
    {"BEEP", prim_beep},
```
and delete these three lines entirely:
```c
    {"PIXEL", prim_pixel}, {"MOUSE-X", prim_mouse_x}, {"MOUSE-Y", prim_mouse_y},
    {"MOUSE-DOWN?", prim_mouse_down}, {"MOUSE-RIGHT-DOWN?", prim_mouse_right_down},
    {"WINDOW-CLOSED?", prim_window_closed},
    {"CURRENT-COLOR", prim_current_color},
```

- [ ] **Step 4: `kernel/kernel.c` -- delete `BIN_PAINT_PATH` and `seed_bin_paint_script()`**

Delete the entire block from `#define BIN_PAINT_PATH "/BIN/PAINT"` (~line 1340) through the end of `seed_bin_paint_script()`'s closing `}` (~line 1412), including its long doc-comment (the one explaining the `PLOOP` Forth script and the two "real deviations from the design spec" it documents).

(Its call site, `seed_bin_paint_script();` in `kmain()`, was already deleted in Task 6 Step 19 -- if for some reason it wasn't, delete it now too.)

- [ ] **Step 5: Grep-verify**

```bash
grep -rni "paint" kernel/forth/ 
```

Expected: zero hits.

```bash
grep -ni "bin_paint\|seed_bin_paint" kernel/kernel.c
```

Expected: zero hits.

- [ ] **Step 6: Build**

```bash
cd boot && make 2>&1 | tail -40
```

Expected: clean build, no errors, no warnings.

- [ ] **Step 7: Commit**

```bash
git add kernel/forth/forth_hooks.h kernel/forth/forth.c kernel/kernel.c
git commit -m "forth: remove the PAINT-specific hooks and primitive words"
```

---

### Task 8: Full regression verification and closeout

**Files:**
- Modify: `docs/BUILD_LOG.md`

- [ ] **Step 1: Full rebuild from scratch**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd boot && make clean && make && make fs.img
```

Expected: clean build, no errors.

- [ ] **Step 2: Headless QEMU regression pass**

Repeat Task 5's Steps 2-7 in full (boot, open PAINT via the Start Menu, paint some cells, save, modify, load-to-confirm-revert, close, confirm FILES/other windows still work) against this fully-cleaned-up build. This re-proves everything Task 5 already proved, but now against the tree with the old kernel-side implementation entirely gone -- confirming the removal in Tasks 6-7 didn't break the new binary's own dependencies (`program_load_and_run()`, the gfx/fs/window syscalls, `ring3_wait_event()`).

- [ ] **Step 3: Confirm a plain boot (no PAINT click at all) is unaffected**

```bash
cd boot
qemu-system-i386 -accel kvm -display none -device sb16 \
  -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 \
  -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 \
  -monitor unix:/tmp/raveos-qemu-plain.sock,server,nowait &
sleep 3
echo "screendump /tmp/plain_boot.ppm" | socat - unix:/tmp/raveos-qemu-plain.sock
echo "quit" | socat - unix:/tmp/raveos-qemu-plain.sock
```

Expected: an ordinary desktop screendump -- taskbar, Start Menu button, no open windows, no PAINT window anywhere -- confirming the new `/BIN/PAINT.BIN` seeding and Start Menu item are true no-ops until clicked.

- [ ] **Step 4: `docs/BUILD_LOG.md` closeout entry**

Add a new dated entry (follow this file's existing entry format/style -- read the most recent few entries first to match tone and structure) summarizing: PAINT moved from kernel-resident window+Forth-script to a standalone ring-3 binary (`programs/paint/`), launched via a new Start Menu item; the old `WIN_KIND_PAINT` window kind, its Forth hooks (`PIXEL`/`MOUSE-X`/`MOUSE-Y`/`MOUSE-DOWN?`/`MOUSE-RIGHT-DOWN?`/`CURRENT-COLOR`/`WINDOW-CLOSED?`/`PAINT`), and the `/BIN/PAINT` Forth script are gone; the palette popup/hide-restore feature was dropped (no right-click event exists in the ring-3 syscall ABI) in favor of a plain always-visible color strip; PAINT can be launched once per boot (pre-existing "one ring-3 program at a time" kernel limitation, not new).

- [ ] **Step 5: Commit**

```bash
git add docs/BUILD_LOG.md
git commit -m "docs: closeout entry for the standalone paint binary"
```

---

## Self-Review Notes

**Spec coverage:** every "In scope" bullet from `docs/superpowers/specs/2026-08-29-standalone-paint-design.md` maps to a task above -- `programs/paint/` (Tasks 1-2), embedding+seeding (Task 3), Start Menu wiring (Task 4), and the full kernel-side removal list (Tasks 6-7) each name every symbol the spec's own "In scope" deletion list names. The spec's five-step Testing section maps to Task 5 (pre-removal) and Task 8 Step 2 (post-removal regression).

**Type/name consistency:** `program_load_and_run(const char *path)`'s signature (Task 4) matches its existing definition in `kernel/kernel.c` (unmodified). `STARTMENU_ITEM_PAINT` is used identically in Task 4's `startmenu.h`/`startmenu.c`/`kernel.c` edits. The on-disk sprite format (256 bytes, row-major, `/HOME/<NAME>` uppercased) is identical between Task 2's `paint.c` and the deleted kernel code it replaces, so a sprite saved by the old implementation (if any survived on a real disk image) loads correctly in the new one.
