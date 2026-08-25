/* Rave-OS's first C kernel code. No libc, no OS underneath us -- this runs
 * directly on the bare metal that stage2 handed off to, in 32-bit protected
 * mode with paging still off, so every pointer here is just a physical
 * address. */

#include "graphics.h"
#include "text.h"
#include "mouse.h"
#include "keyboard.h"
#include "interrupts.h"
#include "window.h"
#include "button.h"
#include "taskbar.h"
#include "startmenu.h"
#include "console_input.h"
#include "console_history.h"
#include "console_output.h"
#include "forth.h"
#include "forth_hooks.h"
#include "scheduler.h"
#include "serial.h"
#include "shell.h"
#include "editor.h"
#include "io.h"
#include "ata.h"
#include "sb16.h"
#include "synth.h"
#include "fs.h"

/* Note: stage2 switches the display into a VBE graphics mode before the
 * kernel even starts, so raw VGA text-mode writes at 0xB8000 don't apply
 * here -- text.c (built on font.c, ported from ACIDSTORM) is the real
 * text output path now. */

#define CURSOR_SIZE 8

/* Three windows exist now: the Forth console, a navigable file manager
 * that can delete, create, and rename entries too, and a bash-like
 * shell (shell.h) for typed Unix-style commands (ls/cd/cat/...) against
 * the same real filesystem FILES already browses with the mouse.
 * Tracked as a real array/z-order list rather than named locals so
 * every window is treated uniformly regardless of what's inside it. The
 * original two demo windows (PANEL, INFO) are gone -- their one piece
 * of real content each (the EXIT/FX controls, ATA/FS status text)
 * either moved into the bottom-left start menu (startmenu.h) or was
 * dropped as boot-only diagnostic noise nothing was actually reading
 * off-screen. The read-only file VIEWER window is gone too, on request
 * -- clicking a file in FILES is now a no-op, same as clicking anything
 * else this project doesn't have a use for yet (cat in the new SHELL
 * window does what VIEWER used to, typed instead of clicked). Content
 * still differs per window, so each window's content is drawn via a
 * kind-indexed dispatch (draw_window_by_index()) rather than a generic
 * widget framework -- there are exactly three content kinds, not an
 * open-ended number, so a small switch is simpler than a real
 * polymorphic app system. */
#define MAX_WINDOWS 5
#define WIN_KIND_FORTH 0
#define WIN_KIND_FILES 1
#define WIN_KIND_SHELL 2
#define WIN_KIND_EDITOR 3
#define WIN_KIND_PAINT 4

/* Shared text colors for the androidacid.com-derived palette (see
 * backdrop_color() below for how the flat-RGB values were derived from
 * the site's actual CSS custom properties, alpha-composited onto its
 * black background since this kernel only does opaque fills). */
#define TEXT_ACCENT_COLOR 0x00FF66 /* their --hard: pure acid green */
#define TEXT_PRIMARY_COLOR 0xD4E6DB /* their --text at 0.9 alpha over --bg */
#define TEXT_MUTED_COLOR 0x9DAAA3  /* their --muted at 0.66 alpha over --bg */
#define CURSOR_IDLE_COLOR 0x00FF66
#define CURSOR_CLICK_COLOR 0xD4E6DB

/* QEMU's default i440fx/PIIX4 machine emulates just enough ACPI to honor
 * this: writing 0x2000 to the PM1a control port (0x604) requests an S5
 * ("soft off") transition, which quits QEMU the same as closing its
 * window -- unlike a triple-fault or infinite hlt loop, this actually
 * ends the process, so there's nothing left holding the mouse grab. */
static void power_shutdown(void) {
    outw(0x604, 0x2000);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* Rave-OS's palette used to be a full-screen XOR-rainbow plasma -- loud,
 * but flagged directly as too harsh to stare at for long. Replaced with
 * the "black + acid green" language borrowed from androidacid.com: a
 * near-black backdrop (their --bg, #050607) with one soft green glow
 * blob (their --hard, #00ff66, used the way their CSS radial-gradient
 * glows are) instead of color everywhere. No floats used (matching the
 * rest of the kernel) -- just an integer linear falloff from a fixed
 * point, capped at a low peak intensity so it reads as a glow, not a
 * second plasma. */
#define BACKDROP_R 5
#define BACKDROP_G 6
#define BACKDROP_B 7
#define GLOW_PEAK 36

/* fx_enabled gates the glow blob -- the start menu's "FX" row toggles it,
 * the one existing visual effect there was to wire a toggle to. Without
 * it, the backdrop is flat near-black. */
static uint32_t backdrop_color(int x, int y, int w, int h, int fx_enabled) {
    int gx = w * 3 / 10;
    int gy = h / 8;
    int dx = x - gx;
    int dy = y - gy;
    int dist2 = dx * dx + dy * dy;
    int radius = w * 3 / 5;
    int radius2 = radius * radius;
    int g = BACKDROP_G;
    int b = BACKDROP_B;

    if (fx_enabled && dist2 < radius2) {
        int intensity = GLOW_PEAK - (dist2 * GLOW_PEAK) / radius2;
        g += intensity;
        b += intensity / 3;
    }

    return ((uint32_t)BACKDROP_R << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void format_uint(unsigned int v, char *out) {
    char tmp[12];
    int i = 0, j = 0;
    if (v == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (v > 0) {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i > 0) {
        out[j++] = tmp[--i];
    }
    out[j] = 0;
}

/* cap is dst's total size (including room for the terminator) -- every
 * caller must pass sizeof(dst) for a real array, not a guess. src is
 * truncated rather than overflowing dst if it would run past cap; this
 * matters because some callers build dst out of on-disk filenames
 * (fs_dirent.name) that are trusted only to fit FS_NAME_MAX, not to
 * respect whatever fixed-size buffer they're being appended into. */
static void str_append(char *dst, int *pos, int cap, const char *src) {
    if (cap <= 0) {
        return;
    }
    while (*src && *pos < cap - 1) {
        dst[(*pos)++] = *src++;
    }
    dst[*pos] = 0;
}

/* No libc strcmp in this freestanding kernel -- used by the Forth
 * console's damage tracking to catch a same-length history recall
 * changing ci.text's content (ci.len alone wouldn't notice). */
static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

/* Splits buf on '\n' and appends each segment as its own console line,
 * in place (buf is mutated -- '\n' bytes become nul terminators). The
 * shared idiom forth_eval_line() output (the Forth console, and RUN's
 * per-script-line output) needs, since forth.c never touches
 * console_output.h itself. */
static void append_split_lines(struct console_output *co, char *buf) {
    int oi = 0, line_start = 0;
    while (buf[oi]) {
        if (buf[oi] == '\n') {
            buf[oi] = 0;
            console_output_append_line(co, &buf[line_start]);
            line_start = oi + 1;
        }
        oi++;
    }
    if (line_start < oi) {
        console_output_append_line(co, &buf[line_start]);
    }
}

#define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */
#define TITLE_TEXT "RAVE-OS"
#define TITLE_Y 40
#define TITLE_SCALE 4
#define SUBTITLE_TEXT "KERNEL: GUI PRIMITIVES ONLINE"
#define SUBTITLE_Y 90
#define SUBTITLE_SCALE 2

static void draw_title_subtitle(int w) {
    text_puts((w - text_width(TITLE_TEXT, TITLE_SCALE)) / 2, TITLE_Y, TITLE_TEXT, TEXT_ACCENT_COLOR, TITLE_SCALE);
    text_puts((w - text_width(SUBTITLE_TEXT, SUBTITLE_SCALE)) / 2, SUBTITLE_Y, SUBTITLE_TEXT, TEXT_MUTED_COLOR,
              SUBTITLE_SCALE);
}

/* Bounding box of both the title and subtitle, treated as one unit --
 * they're small and always redrawn together, so there's no need to track
 * them as two separately-damaged regions. */
static void title_block_rect(int w, int *x0, int *y0, int *x1, int *y1) {
    int title_w = text_width(TITLE_TEXT, TITLE_SCALE);
    int subtitle_w = text_width(SUBTITLE_TEXT, SUBTITLE_SCALE);
    int title_x = (w - title_w) / 2;
    int subtitle_x = (w - subtitle_w) / 2;

    *x0 = title_x < subtitle_x ? title_x : subtitle_x;
    *y0 = TITLE_Y;
    *x1 = (title_x + title_w) > (subtitle_x + subtitle_w) ? (title_x + title_w) : (subtitle_x + subtitle_w);
    *y1 = SUBTITLE_Y + GLYPH_HEIGHT * SUBTITLE_SCALE;
}

/* The window's full painted extent, border included -- matches the
 * rects window_draw() actually fills (see window.c). Works for any
 * window -- callers pass whichever one they mean. */
static void window_outer_rect(const struct window *win, int *x0, int *y0, int *x1, int *y1) {
    *x0 = win->x - 2;
    *y0 = win->y - WINDOW_TITLEBAR_HEIGHT - 2;
    *x1 = win->x + win->w + 2;
    *y1 = win->y + win->h + 2;
}

static int window_outer_contains(const struct window *win, int px, int py) {
    int x0, y0, x1, y1;
    window_outer_rect(win, &x0, &y0, &x1, &y1);
    return px >= x0 && px < x1 && py >= y0 && py < y1;
}

static int rects_overlap(int ax0, int ay0, int ax1, int ay1, int bx0, int by0, int bx1, int by1) {
    return ax0 < bx1 && bx0 < ax1 && ay0 < by1 && by0 < ay1;
}

static void rect_union(int *x0, int *y0, int *x1, int *y1, int bx0, int by0, int bx1, int by1) {
    if (bx0 < *x0) {
        *x0 = bx0;
    }
    if (by0 < *y0) {
        *y0 = by0;
    }
    if (bx1 > *x1) {
        *x1 = bx1;
    }
    if (by1 > *y1) {
        *y1 = by1;
    }
}

/* Which window (if any) is visually on top at a screen point, given the
 * current z-order -- what hit-testing (drag-start, button clicks,
 * raise-on-click) needs to check before acting, so a click on a point
 * where windows overlap only ever affects whichever one is actually
 * visible there. z_order[0] is checked first, since it's the frontmost.
 * A minimized or closed window is never hit -- it isn't on the desktop.
 * Returns a window index, or -1. */
static int topmost_window_at(const struct window *windows, const int *z_order, int px, int py) {
    int i;
    for (i = 0; i < MAX_WINDOWS; i++) {
        int idx = z_order[i];
        if (windows[idx].state != WINDOW_OPEN) {
            continue;
        }
        if (window_outer_contains(&windows[idx], px, py)) {
            return idx;
        }
    }
    return -1;
}

/* Moves a window index to the front of the z-order, shifting the rest
 * back a slot -- the generic version of what used to be a single
 * panel_on_top flag flip. A no-op if it's already frontmost. */
static void raise_window(int *z_order, int idx) {
    int i, pos = -1;

    for (i = 0; i < MAX_WINDOWS; i++) {
        if (z_order[i] == idx) {
            pos = i;
            break;
        }
    }
    if (pos <= 0) {
        return;
    }
    for (i = pos; i > 0; i--) {
        z_order[i] = z_order[i - 1];
    }
    z_order[0] = idx;
}

/* File-scope, not kmain()-local: forth_hook_mouse_x()/mouse_y()/
 * mouse_down()/mouse_right_down() (added for the PAINT Forth words,
 * see docs/superpowers/specs/2026-08-16-paint-design.md) need to read
 * genuinely live mouse state from deep inside forth_eval_line()'s own
 * call stack, with no path back to kmain()'s locals -- kmain()'s own
 * event loop uses these exactly as it always has, only their storage
 * moved. */
static int mx, my;
static int mouse_buttons_live = 0;

/* Drains every mouse packet currently queued, updating the live mx/my/
 * mouse_buttons_live state above -- the same accumulate-and-clamp math
 * kmain()'s own per-packet event-loop block already does, but
 * self-contained and callable from anywhere in this file (specifically:
 * the PAINT mouse-reading hooks, called from deep inside
 * forth_eval_line()'s call stack while kmain()'s own loop isn't
 * running at all). Packets this function drains are gone from
 * mouse.c's ring buffer -- if kmain()'s own loop runs again afterward
 * expecting to see them, it won't, which is correct: nothing else in
 * the OS should react to clicks a Forth script already consumed for
 * painting. */
static void poll_mouse_state(void) {
    int dx, dy, buttons;
    while (mouse_poll_packet(&dx, &dy, &buttons)) {
        serial_write_str("PMS dx="); serial_write_int(dx);
        serial_write_str(" dy="); serial_write_int(dy);
        serial_write_str(" btn="); serial_write_int(buttons);
        serial_write_str("\n");
        mx += dx;
        my += dy;
        if (mx < 0) {
            mx = 0;
        }
        if (my < 0) {
            my = 0;
        }
        if (mx > gfx_width() - CURSOR_SIZE) {
            mx = gfx_width() - CURSOR_SIZE;
        }
        if (my > gfx_height() - CURSOR_SIZE) {
            my = gfx_height() - CURSOR_SIZE;
        }
        mouse_buttons_live = buttons;
    }
}

#define PAINT_GRID_SIZE 16
#define PAINT_CELL_PX 16
#define PAINT_PALETTE_COLORS 16
#define PAINT_SWATCH_W 40
#define PAINT_SWATCH_H 24
#define PAINT_POPUP_COLS 4
#define PAINT_POPUP_SWATCH 32
#define PAINT_POPUP_GAP 4
#define PAINT_POPUP_SIZE (PAINT_POPUP_COLS * PAINT_POPUP_SWATCH + (PAINT_POPUP_COLS - 1) * PAINT_POPUP_GAP)

/* File-scope, same "hook-reachability" reasoning as mx/my above --
 * forth_hook_pixel()/forth_hook_paint_open()/forth_hook_current_color()
 * are called from deep inside forth_eval_line()'s call stack, with no
 * path back to kmain()'s locals. kmain() still threads &paint through
 * the normal five-function draw pipeline exactly like every other
 * window's own state (struct editor ed, etc.) -- this doesn't change
 * that, it only additionally makes paint reachable from the hooks,
 * which aren't part of that pipeline at all. */
struct paint {
    int grid[PAINT_GRID_SIZE][PAINT_GRID_SIZE]; /* palette index 0..15 per cell, row-major */
    int current_color;                          /* natively-selected palette swatch, 0..15 */
    int opened_once;                             /* clears grid to all-zero only the first time PAINT ever opens */
    int palette_popup_open;                      /* whether the palette-chooser popup is showing */
    uint32_t palette_hidden_mask;                /* bit i set means color i is hidden from selection (Task 3) */
    int grid_generation;                         /* bumped on every grid[][] write -- see forth_hook_pixel() */
};
static struct paint paint;
static struct button paint_save_btn;
static struct button paint_load_btn;
static struct console_input paint_name_input;
static int paint_program_slot = -1;

/* Same reachability problem mx/my/paint had above: forth_hook_paint_open()
 * (Task 3) needs to reach windows[WIN_KIND_PAINT].state and call
 * raise_window(z_order, WIN_KIND_PAINT), and forth_hook_window_closed()
 * plus paint_mouse_cell() (used by forth_hook_mouse_x()/
 * forth_hook_mouse_y()) need windows[WIN_KIND_PAINT] too -- all hook
 * functions (or hook helpers) with no path back to kmain()'s locals.
 * kmain() still initializes and threads these through the normal
 * pipeline exactly as before; only their storage moved. */
static struct window windows[MAX_WINDOWS];
static int z_order[MAX_WINDOWS];

static const uint32_t paint_palette[PAINT_PALETTE_COLORS] = {
    0x050607, 0xFFFFFF, 0xFF3B30, 0xFF9500, 0xFFEB3B, 0x00FF66, 0x2979FF, 0xB026FF,
    0x8D6E4C, 0xFF4FA3, 0x18E0E0, 0x0A6E3D, 0x1A2E8C, 0x808080, 0x2B2B2B, 0xCC3300,
};

/* Clamps a window's position so its full outer bounds (border included)
 * stay on-screen. Shared by every draggable window since the bounds math
 * is identical regardless of what's inside -- dragging didn't enforce
 * this at all originally, which meant a window dragged off-screen could
 * make window_draw()'s gfx_fill_rect() calls write outside the
 * backbuffer (gfx_put_pixel() has never bounds-checked). */
static void clamp_window_to_screen(struct window *win, int w, int h) {
    int min_x = 2;
    int max_x = w - win->w - 2;
    int min_y = WINDOW_TITLEBAR_HEIGHT + 2;
    int max_y = h - win->h - 2;

    if (win->x < min_x) {
        win->x = min_x;
    }
    if (win->x > max_x) {
        win->x = max_x;
    }
    if (win->y < min_y) {
        win->y = min_y;
    }
    if (win->y > max_y) {
        win->y = max_y;
    }
}

/* Bundles every widget/state pointer the window-content pipeline
 * (move_window_content()/draw_files_group()/draw_window_by_index()/
 * draw_scene()/update_and_present()) needs to move or draw a window's
 * content, keyed by window kind -- replacing what used to be each of
 * those five functions' own hand-widened parameter list (up to ~30 for
 * update_and_present()) after each new window kind added its own
 * widgets. Built fresh at each of the three call sites from kmain()'s
 * own locals; nothing here changes where those locals actually live.
 * Fields stay non-const even though most callers only read through the
 * struct -- move_window_content() is the one that mutates widget
 * positions directly, and a single shared struct is simpler than a
 * matching const-pointee twin just for the read-only callers (the
 * bundle's own pointer can't be reseated by a `const struct
 * window_content *`, but nothing stops writing through e.g. wc->co->x
 * -- relies on the same caller discipline this codebase already trusts
 * elsewhere, not a compiler guarantee). */
struct window_content {
    struct console_output *co;
    struct console_input *ci;
    struct console_output *shell_co;
    struct console_input *shell_ci;
    const char *cwd;
    const struct fs_dirent *file_entries;
    unsigned int file_entry_count;
    uint32_t files_selected_mask;
    struct console_input *name_input;
    struct button *new_dir_btn;
    struct button *delete_btn;
    struct button *cut_btn;
    struct button *copy_btn;
    struct button *paste_btn;
    struct editor *ed;
    struct button *save_btn;
    struct paint *pt;
    struct console_input *paint_name_input;
    struct button *paint_save_btn;
    struct button *paint_load_btn;
};

/* Moves a window's content widgets by the same delta already applied to
 * the window itself during a drag. This is the single place that
 * happens now, replacing what used to be a hand-written list of
 * `widget.x += dx` lines duplicated at each drag site -- exactly the
 * spot that twice forgot a widget earlier this session when a new one
 * was added. Centralizing it here means a future window's widget only
 * needs to be added in one place to drag correctly, not remembered at
 * every call site that moves the window. */
static void move_window_content(int kind, struct window_content *wc, int applied_dx, int applied_dy) {
    if (kind == WIN_KIND_FORTH) {
        wc->co->x += applied_dx;
        wc->co->y += applied_dy;
        wc->ci->x += applied_dx;
        wc->ci->y += applied_dy;
    } else if (kind == WIN_KIND_FILES) {
        wc->name_input->x += applied_dx;
        wc->name_input->y += applied_dy;
        wc->new_dir_btn->x += applied_dx;
        wc->new_dir_btn->y += applied_dy;
        wc->delete_btn->x += applied_dx;
        wc->delete_btn->y += applied_dy;
        wc->cut_btn->x += applied_dx;
        wc->cut_btn->y += applied_dy;
        wc->copy_btn->x += applied_dx;
        wc->copy_btn->y += applied_dy;
        wc->paste_btn->x += applied_dx;
        wc->paste_btn->y += applied_dy;
    } else if (kind == WIN_KIND_SHELL) {
        wc->shell_co->x += applied_dx;
        wc->shell_co->y += applied_dy;
        wc->shell_ci->x += applied_dx;
        wc->shell_ci->y += applied_dy;
    } else if (kind == WIN_KIND_EDITOR) {
        wc->ed->x += applied_dx;
        wc->ed->y += applied_dy;
        wc->save_btn->x += applied_dx;
        wc->save_btn->y += applied_dy;
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
}

/* The first window: the Forth console. */
static void draw_forth_group(const struct window *forth, const struct console_output *co,
                             const struct console_input *ci) {
    window_draw(forth);
    console_output_draw(co);
    console_input_draw(ci);
}

/* The third window: a bash-like shell (shell.h) -- same console pane
 * shape draw_forth_group() uses, just backed by shell_eval_line()
 * instead of forth_eval_line(). */
static void draw_shell_group(const struct window *shell, const struct console_output *shell_co,
                             const struct console_input *shell_ci) {
    window_draw(shell);
    console_output_draw(shell_co);
    console_input_draw(shell_ci);
}

/* The fourth window: a full-window multi-line text buffer (editor.h),
 * opened only via SHELL's EDIT command -- same window+widget shape
 * draw_forth_group()/draw_shell_group() use, with a SAVE button in
 * place of a second widget. */
static void draw_editor_group(const struct window *ed_win, const struct editor *ed, const struct button *save_btn) {
    window_draw(ed_win);
    editor_draw(ed);
    button_draw(save_btn);
}

/* The fifth window: a 16x16 pixel canvas, a click-to-open palette
 * popup, and a SAVE button -- opened only via the PAINT Forth word,
 * no start-menu launcher, matching EDIT's own SHELL-only precedent.
 * Canvas and palette are both drawn directly from paint's own state
 * (no separate widget module, unlike editor.c -- this window's whole
 * content is exactly two nested loops over fixed-size arrays). */
static void draw_paint_group(const struct window *win, const struct paint *pt, const struct console_input *name_input,
                             const struct button *save_btn, const struct button *load_btn) {
    int row, col;
    int canvas_x = win->x + 8;
    int canvas_y = win->y + 8;
    int palette_y = canvas_y + PAINT_GRID_SIZE * PAINT_CELL_PX + 4;

    window_draw(win);

    for (row = 0; row < PAINT_GRID_SIZE; row++) {
        for (col = 0; col < PAINT_GRID_SIZE; col++) {
            gfx_fill_rect(canvas_x + col * PAINT_CELL_PX, canvas_y + row * PAINT_CELL_PX, PAINT_CELL_PX,
                         PAINT_CELL_PX, (uint32_t)paint_palette[pt->grid[row][col]]);
        }
    }

    /* Current-color "well" -- always visible, shows what PIXEL paints
     * with next. Clicking it opens the popup grid (paint_swatch_hit_test()/
     * paint_popup_grid_hit_test()); the 1px accent border (same
     * border-rect-behind-a-smaller-fill-rect technique
     * console_input_draw() already uses) marks it as clickable. */
    gfx_fill_rect(canvas_x - 1, palette_y - 1, PAINT_SWATCH_W + 2, PAINT_SWATCH_H + 2, 0x00FF66);
    gfx_fill_rect(canvas_x, palette_y, PAINT_SWATCH_W, PAINT_SWATCH_H, paint_palette[pt->current_color]);

    if (pt->palette_popup_open) {
        int pi;

        /* Overlays the canvas's own top-left corner rather than
         * appearing below the swatch -- the window has no vertical
         * room left below the swatch row (see the design spec), so a
         * 140x140 popup temporarily covers part of the canvas while
         * open, same as any floating color-picker dialog would. */
        gfx_fill_rect(canvas_x - 2, canvas_y - 2, PAINT_POPUP_SIZE + 4, PAINT_POPUP_SIZE + 4, 0x00FF66);
        gfx_fill_rect(canvas_x, canvas_y, PAINT_POPUP_SIZE, PAINT_POPUP_SIZE, 0x0B1712);

        for (pi = 0; pi < PAINT_PALETTE_COLORS; pi++) {
            int pcol = pi % PAINT_POPUP_COLS;
            int prow = pi / PAINT_POPUP_COLS;
            int sx = canvas_x + pcol * (PAINT_POPUP_SWATCH + PAINT_POPUP_GAP);
            int sy = canvas_y + prow * (PAINT_POPUP_SWATCH + PAINT_POPUP_GAP);

            uint32_t swatch_color = paint_palette[pi];
            if (pt->palette_hidden_mask & (1u << pi)) {
                /* Halves each RGB channel -- no blending primitive
                 * needed, just a bit-shift, enough to read as "dimmed"
                 * against the popup's dark backing. */
                swatch_color = (swatch_color >> 1) & 0x7F7F7F;
            }
            gfx_fill_rect(sx, sy, PAINT_POPUP_SWATCH, PAINT_POPUP_SWATCH, swatch_color);
            if (pi == pt->current_color) {
                gfx_fill_rect(sx, sy, PAINT_POPUP_SWATCH, 2, 0x00FF66);
                gfx_fill_rect(sx, sy + PAINT_POPUP_SWATCH - 2, PAINT_POPUP_SWATCH, 2, 0x00FF66);
            }
        }
    }

    console_input_draw(name_input);
    button_draw(save_btn);
    button_draw(load_btn);
}

/* forth_hooks.h implementations -- forth.c's only window into
 * graphics/mouse state (see docs/superpowers/specs/2026-08-16-paint-design.md).
 * Inserted here, immediately after draw_paint_group(), for historical
 * reasons: forth_hook_refresh() used to live here and called
 * draw_paint_group() directly, which had to already be visible (this is
 * C, not a language with forward declarations by default) to avoid an
 * implicit-function-declaration warning under -Wall -Wextra.
 * forth_hook_refresh() itself is gone (2026-08-19 concurrency pass --
 * kmain()'s own normal per-frame draw pipeline, update_and_present(),
 * now redraws PAINT the same as every other window, so no hook needs to
 * call draw_paint_group() directly any more), but the remaining hooks
 * are left in this same spot rather than moved. poll_mouse_state()
 * itself is defined much earlier in this file, so these hooks can call
 * it regardless of where they themselves sit. */

/* A short, fixed 400Hz square wave -- just enough to prove the hardware
 * path works, not a synth voice (see docs/superpowers/specs/2026-08-22-
 * sb16-audio-driver-design.md's explicit scope cut). Generated once, into
 * a .bss buffer, with pure integer arithmetic -- no floats needed for a
 * square wave, and .bss costs zero bytes in kernel.bin (objcopy drops it;
 * see arch/linker.ld's own comment on that), unlike a giant literal array
 * would have. aligned(4096): a 4096-aligned buffer of at most 4096 bytes
 * can never straddle the 64KB physical boundary ISA DMA can't cross,
 * since 65536 is itself a multiple of 4096. */
#define BEEP_SAMPLE_RATE 8000u
#define BEEP_FREQ_HZ 400u
#define BEEP_SAMPLES 2000u /* 250ms at 8000Hz */

static unsigned char beep_tone[BEEP_SAMPLES] __attribute__((aligned(4096)));
static int beep_tone_ready = 0;

#define AUDIO_STREAM_HALF_LEN 1024u

static unsigned char audio_stream_buf[AUDIO_STREAM_HALF_LEN * 2u] __attribute__((aligned(4096)));
static int audio_stream_started = 0;
static int synth_current_voice = 0;

/* Lazily starts the continuous audio stream on the first synth Forth
 * word call -- mirrors BEEP's own lazy tone-generation pattern. Once
 * started, the stream runs forever (rendering silence when no voice
 * is gated) rather than starting/stopping DMA per note. */
static void audio_ensure_stream_started(void) {
    if (!audio_stream_started) {
        unsigned int i;

        /* audio_stream_buf lives in .bss, zeroed to 0x00 at boot -- but
         * this driver's silence level is 0x80 (8-bit unsigned PCM
         * midpoint; see synth_render_half()'s own doc comment in
         * synth.h). sb16_start_stream() below commands the DSP to
         * start playing immediately, and the first real refill can
         * only happen after the first IRQ fires (one whole half-buffer
         * later) -- without this fill, DMA would spend that whole
         * first half-buffer's worth of playback time streaming raw
         * 0x00 (full-scale) instead of silence, an audible startup
         * thump. */
        for (i = 0; i < AUDIO_STREAM_HALF_LEN * 2u; i++) {
            audio_stream_buf[i] = 128;
        }
        sb16_start_stream(audio_stream_buf, AUDIO_STREAM_HALF_LEN, SYNTH_SAMPLE_RATE);
        audio_stream_started = 1;
    }
}

static void beep_tone_generate(void) {
    unsigned int half_period = BEEP_SAMPLE_RATE / (2u * BEEP_FREQ_HZ);
    unsigned int i;
    for (i = 0; i < BEEP_SAMPLES; i++) {
        beep_tone[i] = ((i / half_period) % 2u == 0u) ? 160 : 96;
    }
    beep_tone_ready = 1;
}

void forth_hook_beep(void) {
    if (!beep_tone_ready) {
        beep_tone_generate();
    }
    sb16_play_buffer(beep_tone, BEEP_SAMPLES, BEEP_SAMPLE_RATE);
}

void forth_hook_synth_voice(int voice) {
    audio_ensure_stream_started();
    if (voice < 0 || voice >= SYNTH_NUM_VOICES) {
        return;
    }
    synth_current_voice = voice;
}

void forth_hook_synth_wave(int wave) {
    audio_ensure_stream_started();
    if (wave < WAVE_PULSE || wave > WAVE_NOISE) {
        return;
    }
    synth_set_voice_waveform(synth_current_voice, (enum synth_waveform)wave);
}

void forth_hook_synth_duty(int duty_percent) {
    audio_ensure_stream_started();
    synth_set_duty(synth_current_voice, duty_percent);
}

void forth_hook_synth_ona(int ona) {
    audio_ensure_stream_started();
    synth_set_ona(synth_current_voice, ona);
}

void forth_hook_synth_adsr(int attack_ms, int decay_ms, int sustain_percent, int release_ms) {
    audio_ensure_stream_started();
    synth_set_adsr(synth_current_voice, attack_ms, decay_ms, sustain_percent, release_ms);
}

void forth_hook_synth_gate_on(void) {
    audio_ensure_stream_started();
    synth_gate_on(synth_current_voice);
}

void forth_hook_synth_gate_off(void) {
    synth_gate_off(synth_current_voice);
}

void forth_hook_synth_filter_cutoff(int cutoff) {
    audio_ensure_stream_started();
    synth_set_filter_cutoff(cutoff);
}

void forth_hook_synth_filter_res(int resonance) {
    audio_ensure_stream_started();
    synth_set_filter_resonance(resonance);
}

void forth_hook_synth_filter_mode(int mode_mask) {
    audio_ensure_stream_started();
    synth_set_filter_mode(mode_mask);
}

void forth_hook_synth_filter_route(int routed) {
    audio_ensure_stream_started();
    synth_set_voice_filter_route(synth_current_voice, routed);
}

void forth_hook_synth_ring_partner(int partner) {
    audio_ensure_stream_started();
    synth_set_ring_partner(synth_current_voice, partner);
}

void forth_hook_synth_ring_off(void) {
    synth_clear_ring_partner(synth_current_voice);
}

void forth_hook_synth_arp_note(int note, int slot) {
    audio_ensure_stream_started();
    synth_set_arp_note(synth_current_voice, slot, note);
}

void forth_hook_synth_arp_on(int count) {
    audio_ensure_stream_started();
    synth_arp_on(synth_current_voice, count);
}

void forth_hook_synth_arp_off(void) {
    synth_arp_off(synth_current_voice);
}

void forth_hook_synth_arp_rate(int ms) {
    audio_ensure_stream_started();
    synth_set_arp_rate(synth_current_voice, ms);
}

void forth_hook_paint_open(void) {
    serial_write_str("HOOK paint_open\n");
    if (!paint.opened_once) {
        int row, col;
        for (row = 0; row < PAINT_GRID_SIZE; row++) {
            for (col = 0; col < PAINT_GRID_SIZE; col++) {
                paint.grid[row][col] = 0;
            }
        }
        paint.opened_once = 1;
    }
    windows[WIN_KIND_PAINT].state = WINDOW_OPEN;
    raise_window(z_order, WIN_KIND_PAINT);
    paint_program_slot = scheduler_current_slot();
}

void forth_hook_pixel(int x, int y, int color) {
    serial_write_str("HOOK pixel x="); serial_write_int(x);
    serial_write_str(" y="); serial_write_int(y);
    serial_write_str(" c="); serial_write_int(color);
    serial_write_str("\n");
    paint.grid[y][x] = color;
    /* Bumped unconditionally, same "generation only ever increases, so
     * any change is detectable as !=" invariant console_output.h's own
     * generation field already documents -- lets touched[WIN_KIND_PAINT]
     * (see kmain()) detect a grid change without diffing all 256 cells
     * every frame, and without relying on some other window's damage
     * (or the cursor's own footprint) coincidentally overlapping the
     * canvas to trigger a redraw. */
    paint.grid_generation++;
}

/* Converts the live cursor position (poll_mouse_state()'s own mx/my,
 * refreshed on every call so a Forth loop polling this every pass sees
 * genuinely current state) into a canvas-relative cell index -- -1 if
 * the window is closed, or the cursor isn't over the canvas region at
 * all (same 8px-margin offset draw_paint_group()/
 * paint_swatch_hit_test() already use). */
static int paint_mouse_cell(int *out_col, int *out_row) {
    int canvas_x, canvas_y, col, row;

    poll_mouse_state();
    if (windows[WIN_KIND_PAINT].state != WINDOW_OPEN || paint.palette_popup_open) {
        /* The popup overlays the canvas's own top-left corner (see
         * draw_paint_group()) -- while it's open the canvas underneath
         * is not reachable, same as any modal overlay, so MOUSE-X/
         * MOUSE-Y correctly report "not over the canvas" rather than
         * letting a click meant for the popup also paint through to
         * whatever cell happens to be underneath it. */
        return -1;
    }
    canvas_x = windows[WIN_KIND_PAINT].x + 8;
    canvas_y = windows[WIN_KIND_PAINT].y + 8;
    col = (mx + CURSOR_SIZE / 2 - canvas_x) / PAINT_CELL_PX;
    row = (my + CURSOR_SIZE / 2 - canvas_y) / PAINT_CELL_PX;
    if (col < 0 || col >= PAINT_GRID_SIZE || row < 0 || row >= PAINT_GRID_SIZE) {
        return -1;
    }
    *out_col = col;
    *out_row = row;
    return 0;
}

int forth_hook_mouse_x(void) {
    int col, row;
    if (paint_mouse_cell(&col, &row) != 0) {
        return -1;
    }
    return col;
}

int forth_hook_mouse_y(void) {
    int col, row;
    if (paint_mouse_cell(&col, &row) != 0) {
        return -1;
    }
    return row;
}

int forth_hook_mouse_down(void) {
    poll_mouse_state();
    if (mouse_buttons_live != 0) {
        serial_write_str("HOOK mouse_down mbl="); serial_write_int(mouse_buttons_live);
        serial_write_str(" mx="); serial_write_int(mx);
        serial_write_str(" my="); serial_write_int(my);
        serial_write_str("\n");
    }
    return mouse_buttons_live & 0x01;
}

int forth_hook_mouse_right_down(void) {
    poll_mouse_state();
    /* While the palette popup is open, a right-click is the popup's own
     * hide/restore gesture (see kmain()'s PAINT click-handling), not a
     * signal meant for the running Forth script -- otherwise hiding a
     * color would also trigger PLOOP's own MOUSE-RIGHT-DOWN? quit
     * condition and kill the program. */
    if (paint.palette_popup_open) {
        return 0;
    }
    return (mouse_buttons_live & 0x02) != 0;
}

int forth_hook_current_color(void) {
    return paint.current_color;
}

void forth_hook_yield(void) {
    if (scheduler_current_slot() >= 0) {
        scheduler_yield();
    }
}

int forth_hook_window_closed(void) {
    return windows[WIN_KIND_PAINT].state != WINDOW_OPEN;
}

/* Hit-tests the always-visible current-color swatch -- clicking it
 * opens the popup grid (paint_popup_grid_hit_test()). */
static int paint_swatch_hit_test(const struct window *win, int px, int py) {
    int canvas_x = win->x + 8;
    int canvas_y = win->y + 8;
    int palette_y = canvas_y + PAINT_GRID_SIZE * PAINT_CELL_PX + 4;
    return px >= canvas_x && px < canvas_x + PAINT_SWATCH_W && py >= palette_y && py < palette_y + PAINT_SWATCH_H;
}

/* Converts a click position into a popup swatch index (0..15), or -1
 * if the click missed the grid entirely. Mirrors
 * files_list_hit_test()'s own "convert a click into a logical index"
 * shape -- same spirit the old paint_palette_hit_test() used for the
 * strip it replaced. */
static int paint_popup_grid_hit_test(const struct window *win, int px, int py) {
    int canvas_x = win->x + 8;
    int canvas_y = win->y + 8;
    int col, row;

    if (px < canvas_x || px >= canvas_x + PAINT_POPUP_SIZE || py < canvas_y || py >= canvas_y + PAINT_POPUP_SIZE) {
        return -1;
    }
    /* The bounds check above already guarantees col/row land in
     * [0, PAINT_POPUP_COLS) -- PAINT_POPUP_SIZE is exactly
     * PAINT_POPUP_COLS swatches plus the gaps between them, no
     * trailing gap past the last column. A click inside a gap between
     * swatches is attributed to the swatch just before it (integer
     * division) -- same loose tolerance files_list_hit_test() already
     * uses for its own row bands. */
    col = (px - canvas_x) / (PAINT_POPUP_SWATCH + PAINT_POPUP_GAP);
    row = (py - canvas_y) / (PAINT_POPUP_SWATCH + PAINT_POPUP_GAP);
    return row * PAINT_POPUP_COLS + col;
}

/* Builds "/HOME/" + the filename field's own text, uppercased to match
 * this filesystem's all-caps path convention -- the same fold RUN's
 * own path resolution already applies (the old synchronous
 * forth_run_command() covered this in its own comment before it was
 * replaced in the 2026-08-19 concurrency pass; the fold itself now
 * lives inline in kmain()'s own RUN handling). Used by kmain()'s own
 * SAVE- and LOAD-button click handlers below. Returns 0 (leaving *out*
 * unset) if the field is empty -- SAVE/LOAD with no name is a no-op,
 * same "no-error-UI, silent no-op" convention as the rest of this
 * kernel's filesystem writes; the caller checks this before doing
 * anything else. */
static int paint_build_sprite_path(char *out, int out_max) {
    int pos = 0;
    int i;

    if (paint_name_input.text[0] == 0) {
        return 0;
    }
    str_append(out, &pos, out_max, "/HOME/");
    str_append(out, &pos, out_max, paint_name_input.text);
    for (i = 0; out[i]; i++) {
        if (out[i] >= 'a' && out[i] <= 'z') {
            out[i] = (char)(out[i] - 32);
        }
    }
    return 1;
}

/* fs.h's own constant, kept under this file's existing local name (no
 * call site here needs to change) -- same "the old name survives a
 * refactor" precedent VIEWER_BUF_SIZE already set when the VIEWER
 * window was removed. */
#define FILES_PATH_MAX FS_PATH_MAX
#define FILES_ROW_HEIGHT 20
#define FILES_LIST_Y_OFFSET 12

/* Subtle green tint behind the row a right-click selected -- same value
 * as BUTTON_HOVER_COLOR (button.c) so a "highlighted" thing looks the
 * same everywhere in the UI, not a color invented just for this. */
#define FILES_SELECTED_BG_COLOR 0x123322

/* One sector's worth -- every current test file is far smaller. RUN
 * (run_program_entry(), which reads it) uses VIEWER_BUF_SIZE
 * - 1 so there's always room for a manual nul terminator, since
 * fs_read_file() copies exactly out_size raw bytes and doesn't add one
 * itself. Kept its original name (predating the VIEWER window's removal)
 * rather than renamed, since RUN's read really is the same "read a whole
 * small file into one buffer" shape the VIEWER used. */
#define VIEWER_BUF_SIZE 512

/* Case-insensitive match for a leading "RUN " token (mirrors forth.c's
 * own case-insensitive word lookup, so RUN behaves the same regardless
 * of typed case) followed by at least one non-space character. Returns
 * a pointer to the trimmed start of the argument within text (borrowed,
 * not copied), or 0 if text isn't a RUN command. */
static const char *match_run_command(const char *text) {
    char c0 = text[0], c1 = text[1], c2 = text[2];
    int i;

    if (c0 >= 'a' && c0 <= 'z') {
        c0 = (char)(c0 - 32);
    }
    if (c1 >= 'a' && c1 <= 'z') {
        c1 = (char)(c1 - 32);
    }
    if (c2 >= 'a' && c2 <= 'z') {
        c2 = (char)(c2 - 32);
    }
    if (c0 != 'R' || c1 != 'U' || c2 != 'N' || text[3] != ' ') {
        return 0;
    }

    i = 3;
    while (text[i] == ' ') {
        i++;
    }
    return text[i] ? &text[i] : 0;
}

/* Case-insensitive match for a leading "EDIT " token, same shape
 * match_run_command() already uses for "RUN ". Returns a pointer to the
 * trimmed start of the argument within text (borrowed, not copied), or
 * 0 if text isn't an EDIT command. */
static const char *match_edit_command(const char *text) {
    char c0 = text[0], c1 = text[1], c2 = text[2], c3 = text[3];
    int i;

    if (c0 >= 'a' && c0 <= 'z') {
        c0 = (char)(c0 - 32);
    }
    if (c1 >= 'a' && c1 <= 'z') {
        c1 = (char)(c1 - 32);
    }
    if (c2 >= 'a' && c2 <= 'z') {
        c2 = (char)(c2 - 32);
    }
    if (c3 >= 'a' && c3 <= 'z') {
        c3 = (char)(c3 - 32);
    }
    if (c0 != 'E' || c1 != 'D' || c2 != 'I' || c3 != 'T' || text[4] != ' ') {
        return 0;
    }

    i = 4;
    while (text[i] == ' ') {
        i++;
    }
    return text[i] ? &text[i] : 0;
}

/* EDIT <path>, typed at the SHELL console: resolves arg, figures out
 * whether it names an existing file, an existing directory, or nothing
 * yet (fs_read_file()'s own -1 can't distinguish "not found" from "too
 * big for the buffer" from "is a directory" -- see fs.h -- so this
 * checks the parent's own listing first rather than trusting that
 * single failure code), and on success opens+raises the EDITOR window
 * loaded with whatever it found (or empty, for a genuinely new path).
 * Writes "edit: failed" into shell_co on any failure, matching
 * shell.c's own "<cmd>: failed" wording, and leaves any already-open
 * EDITOR window completely untouched. */
static void handle_edit_command(struct shell *sh, struct editor *ed, char *editor_path, int editor_path_cap,
                                char *editor_title, int editor_title_cap, struct window *windows, int *z_order,
                                struct console_output *shell_co, const char *arg) {
    char resolved[FS_PATH_MAX];
    char parent[FS_PATH_MAX];
    char leaf[FS_NAME_MAX];
    struct fs_dirent entries[FS_LIST_MAX];
    unsigned int count, ei;
    int found_type = -1;
    int last_slash = 0, li, k;

    shell_resolve_path(sh, arg, resolved, (int)sizeof(resolved));

    {
        int p = 0;
        str_append(parent, &p, (int)sizeof(parent), resolved);
    }
    fs_path_parent(parent);

    for (k = 0; resolved[k]; k++) {
        if (resolved[k] == '/') {
            last_slash = k;
        }
    }
    li = 0;
    for (k = last_slash + 1; resolved[k] && li < (int)sizeof(leaf) - 1; k++) {
        leaf[li++] = resolved[k];
    }
    leaf[li] = 0;

    /* An empty leaf (e.g. "edit /", root has no leaf name of its own)
     * can never name a real file to load or a valid path to SAVE back
     * to -- fail outright rather than falling through to "not found,
     * open empty" and handing back an editor that can never save. */
    if (leaf[0] == 0) {
        console_output_append_line(shell_co, "edit: failed");
        return;
    }

    /* fs_list_dir() failing here means parent itself doesn't exist or
     * isn't listable -- a bogus path, not "a genuinely new file in a
     * real directory" (whose parent DOES exist and list successfully).
     * Treat it as any other edit failure instead of falling through to
     * "not found, open empty": that fallthrough would hand back a
     * working-looking empty editor whose SAVE can only ever fail later,
     * silently, per fs_create_file()'s own walk_to_parent() check. */
    if (fs_list_dir(parent, entries, FS_LIST_MAX, &count) != 0) {
        console_output_append_line(shell_co, "edit: failed");
        return;
    }
    for (ei = 0; ei < count; ei++) {
        if (str_eq(entries[ei].name, leaf)) {
            found_type = entries[ei].type;
            break;
        }
    }

    if (found_type == FS_TYPE_DIR) {
        console_output_append_line(shell_co, "edit: failed");
        return;
    }

    if (found_type == FS_TYPE_FILE) {
        char tmp[EDITOR_BUF_SIZE];
        unsigned int out_size;
        if (fs_read_file(resolved, tmp, EDITOR_BUF_SIZE, &out_size) != 0) {
            console_output_append_line(shell_co, "edit: failed");
            return;
        }
        editor_set_text(ed, tmp, out_size);
    } else {
        editor_clear(ed);
    }
    ed->cursor = ed->len;

    {
        int p = 0;
        str_append(editor_path, &p, editor_path_cap, resolved);
    }
    {
        int p = 0;
        str_append(editor_title, &p, editor_title_cap, "RAVE-OS EDIT: ");
        str_append(editor_title, &p, editor_title_cap, resolved);
    }
    windows[WIN_KIND_EDITOR].title = editor_title;
    windows[WIN_KIND_EDITOR].state = WINDOW_OPEN;
    raise_window(z_order, WIN_KIND_EDITOR);
}

struct run_program_ctx {
    struct forth_vm vm;
    struct console_output *co;
    char path[FILES_PATH_MAX];
};
static struct run_program_ctx run_ctxs[MAX_PROGRAMS];

/* Entry point for a RUN-launched program, executed inside its own
 * scheduler slot (scheduler.h) instead of blocking kmain() -- replaces
 * the old synchronous forth_run_command(), which read and evaluated a
 * whole script in a single call. path/vm/co are filled in by the RUN
 * call site (below) before scheduler_activate() ever runs this, since
 * ci.text (the source of the raw argument) is cleared as soon as that
 * call site returns -- path resolution can't be deferred to here. The
 * read-and-line-feed logic itself is otherwise identical to before;
 * the only behavioral difference is that a BEGIN...UNTIL loop inside
 * the script now yields (forth.c's OP_CALL_YIELD) back to
 * scheduler_tick(), and through it to kmain()'s own per-frame work,
 * between iterations instead of blocking here until the whole script
 * finishes. */
static void run_program_entry(void *arg) {
    struct run_program_ctx *ctx = (struct run_program_ctx *)arg;
    char buf[VIEWER_BUF_SIZE];
    unsigned int out_size;
    int oi, line_start;

    if (fs_read_file(ctx->path, buf, VIEWER_BUF_SIZE - 1, &out_size) != 0) {
        console_output_append_line(ctx->co, "(RUN FAILED)");
        return;
    }
    buf[out_size] = 0;

    line_start = 0;
    for (oi = 0; oi <= (int)out_size; oi++) {
        if (oi == (int)out_size || buf[oi] == '\n') {
            char eval_out[128];
            char saved = buf[oi];
            buf[oi] = 0;
            forth_eval_line(&ctx->vm, &buf[line_start], eval_out, sizeof(eval_out));
            append_split_lines(ctx->co, eval_out);
            buf[oi] = saved;
            line_start = oi + 1;
        }
    }
}

/* Launches a RUN target: reserves a scheduler slot, resolves run_arg to
 * a /BIN/-relative or absolute path (uppercased, same filesystem
 * convention as before), and activates it via run_program_entry() --
 * shared by both consoles that intercept RUN (the FORTH console and
 * SHELL), so a future third console picks it up by calling this
 * instead of re-deriving the same path/scheduler dance. out_co is
 * where the launched program's own output (or a failure message) goes
 * -- each caller's own console, not necessarily the FORTH one. */
static void handle_run_command(struct console_output *out_co, const char *run_arg) {
    int run_slot = scheduler_reserve("RUN");
    if (run_slot < 0) {
        console_output_append_line(out_co, "(TOO MANY PROGRAMS RUNNING)");
    } else {
        struct run_program_ctx *ctx = &run_ctxs[run_slot];
        int rpos = 0;

        forth_init(&ctx->vm);
        ctx->co = out_co;
        /* Same case-fold as before (see the old forth_run_command()'s
         * comment, now moved here): every real path in this filesystem
         * is uppercase by convention, and fs.c's lookups are
         * byte-exact. */
        if (run_arg[0] == '/') {
            str_append(ctx->path, &rpos, (int)sizeof(ctx->path), run_arg);
        } else {
            str_append(ctx->path, &rpos, (int)sizeof(ctx->path), "/BIN/");
            str_append(ctx->path, &rpos, (int)sizeof(ctx->path), run_arg);
        }
        for (rpos = 0; ctx->path[rpos]; rpos++) {
            if (ctx->path[rpos] >= 'a' && ctx->path[rpos] <= 'z') {
                ctx->path[rpos] = (char)(ctx->path[rpos] - 32);
            }
        }
        scheduler_activate(run_slot, run_program_entry, ctx);
    }
}

#define ETC_CONFIG_PATH "/ETC/CONFIG"

/* Seeds /ETC/CONFIG with today's default if missing (same idempotent
 * fs_create_file() write-once shape as /BIN/HELLO), then reads it back
 * to decide the FX toggle's initial state -- real config the kernel
 * acts on, not just a name on disk. Plain KEY=VALUE lines, even with a
 * single key today, so the format doesn't need retrofitting once a
 * second setting exists; no generic parser, just enough to find one key. */
static int fx_default_from_config(void) {
    static const char etc_config_default[] = "FX=0\n";
    char buf[64];
    unsigned int out_size;
    unsigned int i;

    fs_create_file(ETC_CONFIG_PATH, etc_config_default, (unsigned int)(sizeof(etc_config_default) - 1));

    if (fs_read_file(ETC_CONFIG_PATH, buf, sizeof(buf) - 1, &out_size) != 0) {
        return 0;
    }
    buf[out_size] = 0;

    for (i = 0; i + 3 <= out_size; i++) {
        if (buf[i] == 'F' && buf[i + 1] == 'X' && buf[i + 2] == '=') {
            return buf[i + 3] == '1';
        }
    }
    return 0;
}

#define BIN_PAINT_PATH "/BIN/PAINT"

/* Seeds /BIN/PAINT with the default interactive drawing loop if
 * missing -- same idempotent fs_create_file() write-once shape
 * fx_default_from_config() already uses for /ETC/CONFIG, so a user who
 * opens this in EDITOR and rewrites it keeps their own version across
 * reboots (fs_create_file() only ever succeeds the very first time a
 * path exists). While the left button is held and the cursor is over
 * the canvas, paints the current color at the cursor's cell; stops
 * when the right button is pressed or the window is closed
 * (WINDOW-CLOSED?, Task 4). The bounds check exists because
 * MOUSE-X/MOUSE-Y return -1 when the cursor isn't over the canvas at
 * all (e.g. hovering the current-color swatch, or while the palette
 * popup is open and covering it).
 *
 * No REFRESH/PALETTE-PICK/SAVE-PICK calls here (2026-08-19 concurrency
 * pass deleted all three, along with the hooks they wrapped) -- those
 * existed only because kmain()'s own per-frame redraw, palette
 * hit-test, and SAVE hit-test didn't run at all while this
 * BEGIN...UNTIL blocked inside forth_eval_line()'s call chain. Since
 * Task 5, a compiled BEGIN...UNTIL loop yields back to scheduler_tick()
 * (and through it to kmain()'s own per-frame work) on every iteration
 * instead of blocking, so all of that now happens for free through the
 * exact same general per-frame path every other window already uses --
 * no PAINT-specific workaround needed.
 *
 * Two real deviations from the design spec's illustrative script, both
 * found while headlessly verifying this against the actual dialect
 * (forth.c), not just assumed from the spec's prose:
 *
 * 1. The spec's script used ">=" and "AND", but this Forth's
 *    primitives[] table has neither -- only
 *    "+ - * / DUP DROP SWAP OVER = < > . CR @ !" plus the paint words.
 *    MOUSE-X/MOUSE-Y already only ever return -1 (off-canvas) or 0..15
 *    (on-canvas, see paint_mouse_cell()), so "greater than -1" alone
 *    distinguishes valid from invalid -- no upper-bound check needed.
 *    "OVER OVER SWAP -1 > SWAP -1 > *" duplicates x and y, tests each
 *    against -1 with ">", and ANDs the two 0/-1 flags together with "*"
 *    (both true multiplies to a nonzero 1; either false multiplies to
 *    0) -- all while leaving the original x/y underneath for PIXEL's
 *    use in the THEN branch.
 * 2. BEGIN/IF/ELSE/THEN/UNTIL are recognized only inside a colon
 *    definition's compile mode (handle_compile_token() in forth.c) --
 *    handle_immediate_token(), the path every top-level/console-typed
 *    token (and thus every line RUN feeds through forth_eval_line()
 *    outside of compile mode) actually goes through, has no case for
 *    any of them at all, so a bare top-level "BEGIN ... UNTIL" (as the
 *    spec's illustrative script has it) fails with "UNKNOWN" the
 *    instant BEGIN is reached -- caught headlessly via RUN PAINT
 *    printing five UNKNOWNs instead of opening a live loop. The loop
 *    body is instead compiled into a real word (PLOOP) via ":"/";",
 *    then that word is invoked as its own top-level line -- the
 *    ordinary, idiomatic way any Forth runs a loop from the console,
 *    and RUN's own line-by-line feed already preserves compile-mode
 *    state across lines for exactly this shape (see run_program_entry()
 *    above -- the old synchronous forth_run_command() did the same
 *    before it was replaced in the 2026-08-19 concurrency pass). */
static void seed_bin_paint_script(void) {
    static const char bin_paint_default[] =
        "PAINT\n"
        ": PLOOP\n"
        "  BEGIN\n"
        "    MOUSE-DOWN? IF\n"
        "      MOUSE-X MOUSE-Y\n"
        "      OVER OVER SWAP -1 > SWAP -1 > *\n"
        "      IF CURRENT-COLOR PIXEL ELSE DROP DROP THEN\n"
        "    THEN\n"
        "    MOUSE-RIGHT-DOWN? WINDOW-CLOSED? +\n"
        "  UNTIL\n"
        ";\n"
        "PLOOP\n";
    fs_create_file(BIN_PAINT_PATH, bin_paint_default, (unsigned int)(sizeof(bin_paint_default) - 1));
}

/* Holds files cut/copied from the FILES window, independent of the
 * current listing/selection so it survives navigating to a different
 * directory before pasting -- the whole point of "select, then navigate,
 * then paste". v1 is files-only: a selected directory is never staged
 * here (see files_clipboard_stage() below). */
struct files_clipboard {
    char source_dir[FILES_PATH_MAX];
    char names[FS_LIST_MAX][FS_NAME_MAX];
    unsigned int count;
    int is_cut; /* 1 = CUT (fs_move on paste), 0 = COPY (fs_copy_file on paste) */
};

/* Snapshots every currently-selected row in file_entries that's a file
 * (a selected FS_TYPE_DIR row is silently skipped -- directories are out
 * of scope for move/copy in v1) into clip, recording cwd as where they
 * came from and whether this was a CUT or a COPY. If nothing file-typed
 * ended up selected, clip is left completely untouched -- same silent-
 * no-op convention as every other unsupported action in this window. */
static void files_clipboard_stage(struct files_clipboard *clip, const char *cwd,
                                  const struct fs_dirent *file_entries, unsigned int file_entry_count,
                                  uint32_t files_selected_mask, int is_cut) {
    unsigned int i;
    unsigned int n = 0;

    for (i = 0; i < file_entry_count; i++) {
        if ((files_selected_mask & (1u << i)) && file_entries[i].type == FS_TYPE_FILE) {
            int p = 0;
            str_append(clip->names[n], &p, (int)sizeof(clip->names[n]), file_entries[i].name);
            n++;
        }
    }
    if (n == 0) {
        return;
    }
    {
        int p = 0;
        str_append(clip->source_dir, &p, (int)sizeof(clip->source_dir), cwd);
    }
    clip->count = n;
    clip->is_cut = is_cut;
}

/* A listing of the current directory (cwd), navigable -- Stage B of the
 * file manager, plus (Stage D) a right-click-selected entry and a
 * DELETE button, plus (Stage E) a name-entry field and a NEW DIR button.
 * Row 0 is always the path itself (not clickable); row 1 is ".." if cwd
 * isn't root; real entries follow. files_list_hit_test() below mirrors
 * this exact row numbering so the two can never disagree about what a
 * given pixel row means. A bit in files_selected_mask names a selected
 * row (its index into file_entries) -- several bits can be set at once,
 * and every row whose bit is set is drawn with a highlight background. */
static void draw_files_group(const struct window *files, const struct window_content *wc) {
    unsigned int i;
    int row = 1;

    window_draw(files);
    text_puts(files->x + 8, files->y + FILES_LIST_Y_OFFSET, wc->cwd, TEXT_ACCENT_COLOR, 1);

    if (!str_eq(wc->cwd, "/")) {
        text_puts(files->x + 8, files->y + FILES_LIST_Y_OFFSET + row * FILES_ROW_HEIGHT, "..", TEXT_PRIMARY_COLOR, 1);
        row++;
    }

    if (wc->file_entry_count == 0) {
        text_puts(files->x + 8, files->y + FILES_LIST_Y_OFFSET + row * FILES_ROW_HEIGHT, "(EMPTY)", TEXT_MUTED_COLOR,
                  1);
    }

    for (i = 0; i < wc->file_entry_count; i++) {
        char line[FS_NAME_MAX + 16];
        int pos = 0;
        int row_y = files->y + FILES_LIST_Y_OFFSET + row * FILES_ROW_HEIGHT;
        uint32_t text_color = TEXT_PRIMARY_COLOR;

        /* file_entry_count can be as large as FS_LIST_MAX (21), but the
         * window is only ever sized for a handful of visible rows. Stop
         * drawing once a row would run past name_input's top edge (the
         * start of the footer chrome, not the window's own bottom edge --
         * Stage E added a name field and button row below the list)
         * instead of walking gfx_fill_rect/text_puts over them -- rows
         * this far down are unreachable by files_list_hit_test() too, as
         * long as callers pass it this same name_input->y as
         * list_bottom_y (which every call site in this file does): it
         * mirrors this exact row_y + FILES_ROW_HEIGHT > list_bottom_y
         * check, so nothing here needs to become clickable, just stop
         * being drawn. */
        if (row_y + FILES_ROW_HEIGHT > wc->name_input->y) {
            break;
        }

        str_append(line, &pos, (int)sizeof(line), wc->file_entries[i].name);
        if (wc->file_entries[i].type == FS_TYPE_DIR) {
            str_append(line, &pos, (int)sizeof(line), "/");
        } else {
            char num[12];
            str_append(line, &pos, (int)sizeof(line), " ");
            format_uint(wc->file_entries[i].size_bytes, num);
            str_append(line, &pos, (int)sizeof(line), num);
            str_append(line, &pos, (int)sizeof(line), "B");
        }

        if (wc->files_selected_mask & (1u << i)) {
            gfx_fill_rect(files->x + 4, row_y - 2, files->w - 8, FILES_ROW_HEIGHT - 2, FILES_SELECTED_BG_COLOR);
            text_color = TEXT_ACCENT_COLOR;
        }

        text_puts(files->x + 8, row_y, line, text_color, 1);
        row++;
    }

    console_input_draw(wc->name_input);
    button_draw(wc->new_dir_btn);
    button_draw(wc->delete_btn);
    button_draw(wc->cut_btn);
    button_draw(wc->copy_btn);
    button_draw(wc->paste_btn);
}

#define FILES_HIT_NONE (-1)
#define FILES_HIT_UP (-2)

/* Converts a click position into the same row numbering
 * draw_files_group() just drew -- FILES_HIT_NONE for the path header (row
 * 0), outside the window, or at/past a row draw_files_group() stopped
 * short of drawing (pass name_input->y as list_bottom_y, the same "stop
 * drawing" line draw_files_group() itself uses -- see its row_y +
 * FILES_ROW_HEIGHT > name_input->y check, mirrored below using the same
 * row_y formula and row counter, so the two can never disagree about
 * where the drawn list actually ends, down to the pixel); FILES_HIT_UP
 * for "..", otherwise an index into file_entries[]. */
static int files_list_hit_test(const struct window *files, const char *cwd, unsigned int file_entry_count, int px,
                               int py, int list_bottom_y) {
    int up_present = !str_eq(cwd, "/");
    int rel_row;
    int row_y;

    if (px < files->x || px >= files->x + files->w || py < files->y || py >= files->y + files->h) {
        return FILES_HIT_NONE;
    }

    rel_row = (py - (files->y + FILES_LIST_Y_OFFSET)) / FILES_ROW_HEIGHT;
    if (rel_row <= 0) {
        return FILES_HIT_NONE; /* the path header row, or above it */
    }

    row_y = files->y + FILES_LIST_Y_OFFSET + rel_row * FILES_ROW_HEIGHT;
    if (row_y + FILES_ROW_HEIGHT > list_bottom_y) {
        return FILES_HIT_NONE; /* draw_files_group() stopped drawing before this row */
    }

    if (up_present) {
        if (rel_row == 1) {
            return FILES_HIT_UP;
        }
        rel_row -= 2;
    } else {
        rel_row -= 1;
    }

    if (rel_row < 0 || (unsigned int)rel_row >= file_entry_count) {
        return FILES_HIT_NONE;
    }
    return rel_row;
}

/* Navigates the FILES window straight to `target` (an absolute path,
 * e.g. "/ETC") and raises it -- the start menu's CONFIG/GAMES items
 * (startmenu.h) funnel through this instead of duplicating the "set cwd,
 * re-list, clear selection, open, raise" sequence twice. Same re-listing
 * call every other cwd change in this file already uses. */
static void open_files_at(char *cwd, int cwd_cap, const char *target, struct window *windows, int *z_order,
                          struct fs_dirent *file_entries, unsigned int *file_entry_count, uint32_t *files_selected_mask) {
    int pos = 0;
    str_append(cwd, &pos, cwd_cap, target);
    *files_selected_mask = 0;
    if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, file_entry_count) != 0) {
        *file_entry_count = 0;
    }
    windows[WIN_KIND_FILES].state = WINDOW_OPEN;
    raise_window(z_order, WIN_KIND_FILES);
}

/* The one place that dispatches "draw whatever's inside window index
 * idx" -- both draw_scene() and update_and_present() go through this
 * instead of each hand-rolling their own kind check. */
static void draw_window_by_index(int idx, const struct window *windows, const struct window_content *wc) {
    if (idx == WIN_KIND_FORTH) {
        draw_forth_group(&windows[idx], wc->co, wc->ci);
    } else if (idx == WIN_KIND_FILES) {
        draw_files_group(&windows[idx], wc);
    } else if (idx == WIN_KIND_SHELL) {
        draw_shell_group(&windows[idx], wc->shell_co, wc->shell_ci);
    } else if (idx == WIN_KIND_EDITOR) {
        draw_editor_group(&windows[idx], wc->ed, wc->save_btn);
    } else {
        draw_paint_group(&windows[idx], wc->pt, wc->paint_name_input, wc->paint_save_btn, wc->paint_load_btn);
    }
}

/* Full redraw of everything into the backbuffer: background, every open
 * window back-to-front in z-order, and the cursor. Only used for the
 * very first frame, where there's no prior state to diff against --
 * correct by full reconstruction, same reasoning the whole scene used to
 * be redrawn this way every frame before damage tracking (see
 * update_and_present() below). */
static void draw_scene(int w, int h, const struct window *windows, int fx_enabled, const struct window_content *wc,
                       const int *z_order, const struct taskbar *bar, int hovered_entry, const struct startmenu *menu,
                       int menu_hovered_item, int mx, int my, uint32_t cursor_color) {
    int x, y, i;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            gfx_put_pixel(x, y, backdrop_color(x, y, w, h, fx_enabled));
        }
    }

    draw_title_subtitle(w);

    for (i = MAX_WINDOWS - 1; i >= 0; i--) {
        int idx = z_order[i];
        if (windows[idx].state == WINDOW_OPEN) {
            draw_window_by_index(idx, windows, wc);
        }
    }

    taskbar_draw(bar, windows, z_order, MAX_WINDOWS, hovered_entry);
    startmenu_draw(menu, menu_hovered_item, fx_enabled);

    gfx_fill_rect(mx, my, CURSOR_SIZE, CURSOR_SIZE, cursor_color);
}

/* Region index constants for update_and_present()'s damage array: one
 * slot per window, plus the title block, the taskbar, and the start
 * menu. */
#define TITLE_REGION (MAX_WINDOWS)
#define TASKBAR_REGION (MAX_WINDOWS + 1)
#define MENU_REGION (MAX_WINDOWS + 2)
#define DAMAGE_REGIONS (MAX_WINDOWS + 3)

/* Per-event redraw: repaints only a damage rectangle instead of the whole
 * screen, then presents just that rectangle (see the previous
 * damage-rect stage in docs/BUILD_LOG.md for why -- redrawing everything
 * per event is what let a real mouse's packet rate outrun the redraw
 * loop and overflow the ring buffer in an earlier stage). The damage rect
 * has to track DAMAGE_REGIONS independent regions -- each window, the
 * title block, the taskbar, and the start menu -- rather than a fixed few
 * named ones, now that windows are a real array:
 *
 * - It starts as the union of the cursor's old and new position, since
 *   the cursor moves on nearly every event.
 * - A region that moved, changed contents, or was involved in a z-order
 *   change this event is unconditionally redrawn, with its old position
 *   (if it moved) folded into the damage too, so what's left behind at
 *   the old spot gets properly repainted.
 * - After that seed, a fixed-point loop catches everything else: any
 *   region not already marked dirty that the accumulated damage happens
 *   to overlap also needs a redraw -- otherwise repainting the backdrop
 *   (or a window drawn on top of it) over that region would erase pixels
 *   that nothing ever restores. This is what correctly handles the
 *   cursor sweeping across a window it didn't otherwise touch, and a
 *   window that moved away revealing whatever (window or backdrop) was
 *   underneath it. DAMAGE_REGIONS regions total, so this converges in at
 *   most that many passes.
 *
 * Whatever ends up dirty is still redrawn as a whole reconstructed unit,
 * not diffed pixel-by-pixel -- same approach the very first whole-screen
 * redraw used, just scoped down to whatever actually needs it, and
 * painted back-to-front in z-order so the topmost window correctly wins
 * wherever two windows overlap. */
static void update_and_present(int w, int h, const struct window *windows, int fx_enabled,
                               const struct window_content *wc,
                               const int *z_order, const int *old_z, int old_mx, int old_my, int mx, int my,
                               uint32_t cursor_color, const int *old_x, const int *old_y, const int *touched,
                               int fx_changed, const struct taskbar *bar, int hovered_entry, int old_hovered_entry,
                               const struct startmenu *menu, int menu_hovered_item, int menu_touched) {
    int dx0, dy0, dx1, dy1;
    int rx0[DAMAGE_REGIONS], ry0[DAMAGE_REGIONS], rx1[DAMAGE_REGIONS], ry1[DAMAGE_REGIONS];
    int redraw[DAMAGE_REGIONS];
    int changed;
    int i, x, y;
    int z_reordered = 0;
    int taskbar_touched;

    for (i = 0; i < MAX_WINDOWS; i++) {
        if (z_order[i] != old_z[i]) {
            z_reordered = 1;
        }
    }

    /* fx_enabled changes what backdrop_color() returns everywhere on
     * screen, not just near the start menu -- toggling FX has to repaint
     * the whole backdrop, so the damage rect starts at the full screen
     * instead of just the cursor's motion. Every open window's rect then
     * overlaps that damage automatically, so the usual fixed-point loop
     * below redraws them correctly without needing a special case. */
    if (fx_changed) {
        dx0 = 0;
        dy0 = 0;
        dx1 = w;
        dy1 = h;
    } else {
        dx0 = old_mx < mx ? old_mx : mx;
        dy0 = old_my < my ? old_my : my;
        dx1 = (old_mx > mx ? old_mx : mx) + CURSOR_SIZE;
        dy1 = (old_my > my ? old_my : my) + CURSOR_SIZE;
    }

    /* The taskbar's own appearance only depends on state/z-order (which
     * window is "active") and hover, not window position -- but folding
     * any window's touched flag in too (position included) is a harmless
     * superset, not worth a separate finer-grained check for a 24px bar. */
    taskbar_touched = z_reordered || (hovered_entry != old_hovered_entry);
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (touched[i]) {
            taskbar_touched = 1;
        }
    }

    for (i = 0; i < MAX_WINDOWS; i++) {
        window_outer_rect(&windows[i], &rx0[i], &ry0[i], &rx1[i], &ry1[i]);
        redraw[i] = touched[i] || z_reordered;
    }
    title_block_rect(w, &rx0[TITLE_REGION], &ry0[TITLE_REGION], &rx1[TITLE_REGION], &ry1[TITLE_REGION]);
    redraw[TITLE_REGION] = 0;
    rx0[TASKBAR_REGION] = bar->x;
    ry0[TASKBAR_REGION] = bar->y;
    rx1[TASKBAR_REGION] = bar->x + bar->w;
    ry1[TASKBAR_REGION] = bar->y + bar->h;
    redraw[TASKBAR_REGION] = taskbar_touched;
    startmenu_bounds(menu, &rx0[MENU_REGION], &ry0[MENU_REGION], &rx1[MENU_REGION], &ry1[MENU_REGION]);
    redraw[MENU_REGION] = menu_touched;

    for (i = 0; i < MAX_WINDOWS; i++) {
        if (touched[i]) {
            struct window old_win = windows[i];
            int ox0, oy0, ox1, oy1;

            old_win.x = old_x[i];
            old_win.y = old_y[i];
            window_outer_rect(&old_win, &ox0, &oy0, &ox1, &oy1);
            rect_union(&dx0, &dy0, &dx1, &dy1, ox0, oy0, ox1, oy1);
        }
        if (redraw[i]) {
            rect_union(&dx0, &dy0, &dx1, &dy1, rx0[i], ry0[i], rx1[i], ry1[i]);
        }
    }
    if (redraw[TASKBAR_REGION]) {
        rect_union(&dx0, &dy0, &dx1, &dy1, rx0[TASKBAR_REGION], ry0[TASKBAR_REGION], rx1[TASKBAR_REGION],
                  ry1[TASKBAR_REGION]);
    }
    if (redraw[MENU_REGION]) {
        rect_union(&dx0, &dy0, &dx1, &dy1, rx0[MENU_REGION], ry0[MENU_REGION], rx1[MENU_REGION], ry1[MENU_REGION]);
    }

    do {
        changed = 0;
        for (i = 0; i < DAMAGE_REGIONS; i++) {
            if (!redraw[i] && rects_overlap(dx0, dy0, dx1, dy1, rx0[i], ry0[i], rx1[i], ry1[i])) {
                redraw[i] = 1;
                rect_union(&dx0, &dy0, &dx1, &dy1, rx0[i], ry0[i], rx1[i], ry1[i]);
                changed = 1;
            }
        }
    } while (changed);

    if (dx0 < 0) {
        dx0 = 0;
    }
    if (dy0 < 0) {
        dy0 = 0;
    }
    if (dx1 > w) {
        dx1 = w;
    }
    if (dy1 > h) {
        dy1 = h;
    }

    for (y = dy0; y < dy1; y++) {
        for (x = dx0; x < dx1; x++) {
            gfx_put_pixel(x, y, backdrop_color(x, y, w, h, fx_enabled));
        }
    }

    if (redraw[TITLE_REGION]) {
        draw_title_subtitle(w);
    }


    for (i = MAX_WINDOWS - 1; i >= 0; i--) {
        int idx = z_order[i];
        if (windows[idx].state == WINDOW_OPEN && redraw[idx]) {
            draw_window_by_index(idx, windows, wc);
        }
    }

    if (redraw[TASKBAR_REGION]) {
        taskbar_draw(bar, windows, z_order, MAX_WINDOWS, hovered_entry);
    }
    if (redraw[MENU_REGION]) {
        startmenu_draw(menu, menu_hovered_item, fx_enabled);
    }

    gfx_fill_rect(mx, my, CURSOR_SIZE, CURSOR_SIZE, cursor_color);

    gfx_present_rect(dx0, dy0, dx1 - dx0, dy1 - dy0);
}

void kmain(void) {
    int w, h;
    int prev_left_held = 0;
    int prev_right_held = 0;
    int dragging_window = -1;
    int taskbar_hovered = -1;
    int menu_hovered_item = -1;
    int fx_enabled = 0;
    uint32_t cursor_color = CURSOR_IDLE_COLOR;
    struct console_output co;
    struct console_input ci;
    struct console_history hist;
    struct forth_vm vm;
    struct console_output shell_co;
    struct console_input shell_ci;
    struct console_history shell_hist;
    struct shell sh;
    struct taskbar bar;
    struct startmenu menu;
    struct button delete_btn;
    struct button new_dir_btn;
    struct button cut_btn;
    struct button copy_btn;
    struct button paste_btn;
    struct files_clipboard clipboard;
    struct console_input name_input;
    struct editor ed;
    struct button save_btn;
    char editor_path[FS_PATH_MAX];
    char editor_title[FS_PATH_MAX + 16];
    const char *ata_status;
    const char *fs_status;
    char cwd[FILES_PATH_MAX];
    struct fs_dirent file_entries[FS_LIST_MAX];
    unsigned int file_entry_count;
    uint32_t files_selected_mask = 0;

    gfx_init();
    w = gfx_width();
    h = gfx_height();

    /* Sized/positioned clear of the taskbar strip below it. */
    windows[WIN_KIND_FORTH].x = 170;
    windows[WIN_KIND_FORTH].y = 230;
    windows[WIN_KIND_FORTH].w = 400;
    windows[WIN_KIND_FORTH].h = 180;
    windows[WIN_KIND_FORTH].title = "RAVE-OS FORTH";
    /* Each window kind gets its own accent_color (border + hovered-control
     * highlight, see window_draw()) so they read as visually distinct at a
     * glance -- reusing PAINT's own already-vetted palette colors rather
     * than inventing new ones. FORTH keeps the original androidacid.com
     * accent green since it's the OS's own first/primary window. */
    windows[WIN_KIND_FORTH].accent_color = 0x00FF66;
    /* Closed at boot, same as every other window now -- see the z_order
     * comment below for why. Opened via the start menu's FORTH item --
     * there's no desktop-icon fallback anymore (removed on request, since
     * every window now has a direct menu launcher, making the icon
     * column pure redundancy). */
    windows[WIN_KIND_FORTH].state = WINDOW_CLOSED;
    windows[WIN_KIND_FORTH].minimize_hovered = 0;
    windows[WIN_KIND_FORTH].close_hovered = 0;

    /* Sized for ~8 visible listing lines (see draw_files_group()) --
     * plenty for the current tree. Overlapping the other windows' corners
     * is fine, same as every other window here. */
    windows[WIN_KIND_FILES].x = 420;
    windows[WIN_KIND_FILES].y = 120;
    windows[WIN_KIND_FILES].w = 180;
    /* 26px taller than Stage D's height -- room for the new name-entry
     * field above the button footer, without shrinking the list's
     * existing ~8-row capacity (draw_files_group()'s row clip stops
     * before the footer, not at the old fixed window bottom, so growing
     * the footer here doesn't eat into the list either). */
    /* 28px taller than before -- room for a second button row
     * (CUT/COPY/PASTE) below NEW DIR/DELETE without shrinking the list's
     * existing visible-row capacity, since name_input/new_dir_btn/
     * delete_btn's own y positions (computed from this h below) end up
     * completely unchanged; only the new row appends below them. */
    windows[WIN_KIND_FILES].h = 278;
    windows[WIN_KIND_FILES].title = "RAVE-OS FILES";
    windows[WIN_KIND_FILES].accent_color = 0x2979FF; /* blue, same as PAINT's own palette index 6 */
    /* Closed at boot -- opened via the start menu's FILES item (a plain
     * launch) or CONFIG/GAMES (which also navigate cwd -- see
     * open_files_at()). */
    windows[WIN_KIND_FILES].state = WINDOW_CLOSED;
    windows[WIN_KIND_FILES].minimize_hovered = 0;
    windows[WIN_KIND_FILES].close_hovered = 0;

    /* Footer button row: NEW DIR (left half) and DELETE (right half),
     * side by side -- narrower than Stage D's full-width DELETE, but
     * still comfortably wide enough for either label at this font size.
     * NEW DIR creates name_input's typed name as a directory in cwd;
     * Enter inside name_input (below) creates it as a file instead, so
     * a file needs no button of its own. DELETE unchanged from Stage D
     * (right-click a row to select it, then this button removes it). */
    new_dir_btn.x = windows[WIN_KIND_FILES].x + 8;
    /* -58, not -30 -- the old "-30 from the bottom" position now belongs
     * to the new CUT/COPY/PASTE row below this one (Step 2). Since h grew
     * by exactly 28 to compensate, this still evaluates to the exact same
     * absolute y it always has, so name_input and the list area above it
     * are visually unchanged. */
    new_dir_btn.y = windows[WIN_KIND_FILES].y + windows[WIN_KIND_FILES].h - 58;
    new_dir_btn.w = (windows[WIN_KIND_FILES].w - 16 - 8) / 2;
    new_dir_btn.h = 22;
    new_dir_btn.label = "NEW DIR";
    new_dir_btn.hovered = 0;
    new_dir_btn.pressed = 0;

    delete_btn.x = new_dir_btn.x + new_dir_btn.w + 8;
    delete_btn.y = new_dir_btn.y;
    delete_btn.w = new_dir_btn.w;
    delete_btn.h = 22;
    delete_btn.label = "DELETE";
    delete_btn.hovered = 0;
    delete_btn.pressed = 0;

    /* Second footer row, CUT/COPY/PASTE, directly below NEW DIR/DELETE --
     * three buttons instead of two, so each gets a third of the same
     * margin/gap formula NEW DIR/DELETE already use rather than a new
     * layout scheme. paste_btn absorbs the integer-division remainder so
     * the row still fills edge-to-edge symmetrically (margins match on
     * both sides). */
    cut_btn.x = new_dir_btn.x;
    cut_btn.y = windows[WIN_KIND_FILES].y + windows[WIN_KIND_FILES].h - 30;
    cut_btn.w = (windows[WIN_KIND_FILES].w - 16 - 16) / 3;
    cut_btn.h = 22;
    cut_btn.label = "CUT";
    cut_btn.hovered = 0;
    cut_btn.pressed = 0;

    copy_btn.x = cut_btn.x + cut_btn.w + 8;
    copy_btn.y = cut_btn.y;
    copy_btn.w = cut_btn.w;
    copy_btn.h = 22;
    copy_btn.label = "COPY";
    copy_btn.hovered = 0;
    copy_btn.pressed = 0;

    paste_btn.x = copy_btn.x + copy_btn.w + 8;
    paste_btn.y = cut_btn.y;
    paste_btn.w = (windows[WIN_KIND_FILES].x + windows[WIN_KIND_FILES].w - 8) - paste_btn.x;
    paste_btn.h = 22;
    paste_btn.label = "PASTE";
    paste_btn.hovered = 0;
    paste_btn.pressed = 0;

    clipboard.count = 0;
    clipboard.is_cut = 0;
    clipboard.source_dir[0] = 0;

    /* Name-entry field for both NEW DIR and Enter-creates-file, sitting
     * just above the button row. */
    name_input.x = windows[WIN_KIND_FILES].x + 8;
    name_input.y = new_dir_btn.y - 20 - 6;
    name_input.w = windows[WIN_KIND_FILES].w - 16;
    name_input.h = 20;
    name_input.text[0] = 0;
    name_input.len = 0;
    name_input.cursor = 0;
    name_input.focused = 0;

    /* Same console-pane shape FORTH uses, at a different position so the
     * two don't land exactly on top of each other at boot (overlap
     * itself is harmless and expected -- every window here is
     * draggable). */
    windows[WIN_KIND_SHELL].x = 200;
    windows[WIN_KIND_SHELL].y = 260;
    windows[WIN_KIND_SHELL].w = 400;
    windows[WIN_KIND_SHELL].h = 180;
    windows[WIN_KIND_SHELL].title = "RAVE-OS SHELL";
    windows[WIN_KIND_SHELL].accent_color = 0xFF9500; /* orange, same as PAINT's own palette index 3 */
    /* Closed at boot, same as FORTH/FILES -- opened via the start
     * menu's SHELL item. */
    windows[WIN_KIND_SHELL].state = WINDOW_CLOSED;
    windows[WIN_KIND_SHELL].minimize_hovered = 0;
    windows[WIN_KIND_SHELL].close_hovered = 0;

    /* Same 400x180 footprint FORTH/SHELL's own console panes already
     * use -- this is a single-region editable pane, not a
     * windowed-list-plus-footer shape like FILES. */
    windows[WIN_KIND_EDITOR].x = 240;
    windows[WIN_KIND_EDITOR].y = 240;
    windows[WIN_KIND_EDITOR].w = 400;
    windows[WIN_KIND_EDITOR].h = 180;
    windows[WIN_KIND_EDITOR].title = "RAVE-OS EDIT";
    windows[WIN_KIND_EDITOR].accent_color = 0xB026FF; /* purple, same as PAINT's own palette index 7 */
    /* Closed at boot, same as every other window -- opened only via
     * SHELL's EDIT command (see handle_edit_command(), added in the
     * next task), no start-menu launcher (EDIT is deliberately
     * SHELL-only for v1). This literal string is never actually shown
     * on screen: the window stays closed until EDIT has already
     * overwritten .title with the real, dynamically-formatted one. */
    windows[WIN_KIND_EDITOR].state = WINDOW_CLOSED;
    windows[WIN_KIND_EDITOR].minimize_hovered = 0;
    windows[WIN_KIND_EDITOR].close_hovered = 0;

    /* 272x356 -- room for the 256x256 canvas (16px/cell x 16 cells),
     * the current-color swatch (the popup overlays the canvas itself
     * rather than needing its own row), a filename field, and a SAVE
     * button,
     * all with 8px margins (the filename field adds 26px over the
     * original 330 -- its own 20px height plus a 6px gap above SAVE,
     * same spacing FILES' name_input/NEW DIR pair already uses). y=80
     * keeps this comfortably inside clamp_window_to_screen()'s own
     * max_y for a window this tall (98, on a 640x480/24px-taskbar
     * screen) -- the same invariant a prior stage's default window
     * position violated and had to fix; checked deliberately this
     * time, including after this height change. */
    windows[WIN_KIND_PAINT].x = 340;
    windows[WIN_KIND_PAINT].y = 80;
    windows[WIN_KIND_PAINT].w = 272;
    windows[WIN_KIND_PAINT].h = 356;
    windows[WIN_KIND_PAINT].title = "RAVE-OS PAINT";
    windows[WIN_KIND_PAINT].accent_color = 0xFF3B30; /* red, same as PAINT's own palette index 2 */
    /* Closed at boot, opened only via the PAINT Forth word (Task 3) --
     * no start-menu launcher, matching EDIT's own SHELL-only
     * precedent. */
    windows[WIN_KIND_PAINT].state = WINDOW_CLOSED;
    windows[WIN_KIND_PAINT].minimize_hovered = 0;
    windows[WIN_KIND_PAINT].close_hovered = 0;

    /* z_order still needs a valid starting permutation even though every
     * window opens closed now -- topmost_window_at()/raise_window() both
     * assume it's always a full ordering of every window index, not just
     * the currently-open ones. Order is otherwise meaningless until the
     * user opens something. */
    z_order[0] = WIN_KIND_FORTH;
    z_order[1] = WIN_KIND_FILES;
    z_order[2] = WIN_KIND_SHELL;
    z_order[3] = WIN_KIND_EDITOR;
    z_order[4] = WIN_KIND_PAINT;

    /* Narrowed to leave room for the start menu's button at the same y,
     * so the two together read as one continuous bottom bar. */
    bar.x = STARTMENU_BUTTON_WIDTH;
    bar.y = h - TASKBAR_HEIGHT;
    bar.w = w - STARTMENU_BUTTON_WIDTH;
    bar.h = TASKBAR_HEIGHT;

    menu.x = 0;
    menu.y = h - TASKBAR_HEIGHT;
    menu.w = STARTMENU_BUTTON_WIDTH;
    menu.h = TASKBAR_HEIGHT;
    menu.open = 0;

    /* Console input line sits along the bottom of the Forth window's
     * body; the output pane fills the rest above it, with a small gap
     * separating the two. */
    {
        int body_x = windows[WIN_KIND_FORTH].x;
        int body_y = windows[WIN_KIND_FORTH].y;
        int body_w = windows[WIN_KIND_FORTH].w;
        int body_h = windows[WIN_KIND_FORTH].h;
        int input_h = 20;
        int input_margin = 10;

        ci.h = input_h;
        ci.y = body_y + body_h - input_margin - input_h;
        ci.x = body_x + 8;
        ci.w = body_w - 16;
        ci.text[0] = 0;
        ci.len = 0;
        ci.cursor = 0;
        ci.focused = 0;
        console_history_init(&hist);

        console_output_init(&co, body_x + 4, body_y + 6, body_w - 8, ci.y - (body_y + 6) - 8);
        console_output_append_line(&co, "RAVE-OS FORTH");
    }
    forth_init(&vm);

    /* Console input line sits along the bottom of the Shell window's
     * body, same layout FORTH's own block above already uses. */
    {
        int body_x = windows[WIN_KIND_SHELL].x;
        int body_y = windows[WIN_KIND_SHELL].y;
        int body_w = windows[WIN_KIND_SHELL].w;
        int body_h = windows[WIN_KIND_SHELL].h;
        int input_h = 20;
        int input_margin = 10;

        shell_ci.h = input_h;
        shell_ci.y = body_y + body_h - input_margin - input_h;
        shell_ci.x = body_x + 8;
        shell_ci.w = body_w - 16;
        shell_ci.text[0] = 0;
        shell_ci.len = 0;
        shell_ci.cursor = 0;
        shell_ci.focused = 0;
        console_history_init(&shell_hist);

        console_output_init(&shell_co, body_x + 4, body_y + 6, body_w - 8, shell_ci.y - (body_y + 6) - 8);
        console_output_append_line(&shell_co, "RAVE-OS SHELL");
    }
    shell_init(&sh);

    /* Full-window editable text region with a SAVE button along the
     * bottom -- same margin/gap formula FILES' own footer row already
     * uses (button.y = win.y + win.h - 30, 8px side margins), not
     * FORTH/SHELL's single-input-line-at-the-bottom shape, since
     * there's no separate command line here: the whole body is the
     * buffer. */
    {
        int body_x = windows[WIN_KIND_EDITOR].x;
        int body_y = windows[WIN_KIND_EDITOR].y;
        int body_w = windows[WIN_KIND_EDITOR].w;
        int body_h = windows[WIN_KIND_EDITOR].h;

        save_btn.x = body_x + 8;
        save_btn.y = body_y + body_h - 30;
        save_btn.w = body_w - 16;
        save_btn.h = 22;
        save_btn.label = "SAVE";
        save_btn.hovered = 0;
        save_btn.pressed = 0;

        editor_init(&ed, body_x + 4, body_y + 6, body_w - 8, save_btn.y - 6 - (body_y + 6));
    }
    editor_path[0] = 0;
    editor_title[0] = 0;

    /* Filename field, same widget FILES' own name_input already uses,
     * stacked directly above SAVE the same way FILES stacks name_input
     * above NEW DIR. Pre-filled with "SPRITE" so leaving it untouched
     * reproduces the exact fixed /HOME/SPRITE path this used to always
     * save to -- existing muscle memory (or a headless test) still
     * gets the same result without typing anything. */
    paint_name_input.x = windows[WIN_KIND_PAINT].x + 8;
    paint_name_input.y = windows[WIN_KIND_PAINT].y + 8 + PAINT_GRID_SIZE * PAINT_CELL_PX + 4 + 24 + 8;
    paint_name_input.w = PAINT_GRID_SIZE * PAINT_CELL_PX;
    paint_name_input.h = 20;
    console_input_set_text(&paint_name_input, "SPRITE");
    paint_name_input.focused = 0;

    /* SAVE/LOAD side by side, same "(total - 16 - 8) / 2, second.x =
     * first.x + first.w + 8" split FILES' own NEW DIR/DELETE pair
     * already uses -- no window resize needed, since this row already
     * had exactly one button's worth of width to spare. */
    paint_save_btn.x = windows[WIN_KIND_PAINT].x + 8;
    paint_save_btn.y = paint_name_input.y + paint_name_input.h + 6;
    paint_save_btn.w = (PAINT_GRID_SIZE * PAINT_CELL_PX - 8) / 2;
    paint_save_btn.h = 22;
    paint_save_btn.label = "SAVE";
    paint_save_btn.hovered = 0;
    paint_save_btn.pressed = 0;

    paint_load_btn.x = paint_save_btn.x + paint_save_btn.w + 8;
    paint_load_btn.y = paint_save_btn.y;
    paint_load_btn.w = paint_save_btn.w;
    paint_load_btn.h = 22;
    paint_load_btn.label = "LOAD";
    paint_load_btn.hovered = 0;
    paint_load_btn.pressed = 0;

    paint.current_color = 1; /* white -- a visible default against the near-black eraser color at index 0 */
    paint.opened_once = 0;

    mx = w / 2;
    my = h - 100; /* clear of the taskbar/start menu strip below it */

    /* IDT/PIC set up first (masked, no sti yet), then the mouse's polling
     * handshake runs with IRQ12 still masked so it can't race the new
     * interrupt handler for the same bytes, then interrupts are actually
     * enabled once both are ready. */
    serial_init();
    interrupts_init();
    mouse_init();
    interrupts_enable();

    /* No IRQ14 involved (see ata.h) -- this is a synchronous polling call,
     * safe to run any time after interrupts_enable(), not tied to the
     * masked-PIC ordering the line above exists for. */
    ata_status = ata_selftest();
    sb16_init();
    synth_init();
    fs_status = fs_selftest();
    fs_bootstrap_dirs();

    /* Real logging, not just a name on disk: one line per boot, appended
     * (not overwritten) to /VAR/LOG -- ata_status/fs_status already read
     * "ATA: ..."/"FS: ..." (ata.h/fs.h), so no extra labeling needed.
     * fs_append_file() creates the file on the first boot and genuinely
     * grows it on every boot after, proving the append path works
     * across real reboots, not just within one running session. */
    {
        char log_line[64];
        int pos = 0;
        str_append(log_line, &pos, (int)sizeof(log_line), ata_status);
        str_append(log_line, &pos, (int)sizeof(log_line), " ");
        str_append(log_line, &pos, (int)sizeof(log_line), fs_status);
        str_append(log_line, &pos, (int)sizeof(log_line), "\n");
        fs_append_file("/VAR/LOG", log_line, (unsigned int)pos);
    }

    /* fx_enabled was set to a hardcoded 0 above, before the filesystem
     * was even mounted -- overwritten here now that /ETC/CONFIG can
     * actually be read. Nothing reads fx_enabled before draw_scene()
     * further down, so this reassignment is safe. */
    fx_enabled = fx_default_from_config();

    seed_bin_paint_script();

    /* Seeds one real script into /BIN so RUN has something to actually
     * run -- there's no in-OS text editor yet, so this is the only way
     * a script with real content ends up on disk, same reasoning as
     * fs_selftest()'s own NESTED.TXT. fs_create_file() is write-once,
     * so this is a silent no-op every boot after the first. */
    {
        static const char demo_script[] = ": GREET 42 . CR ;\nGREET\n";
        fs_create_file("/BIN/HELLO", demo_script, (unsigned int)(sizeof(demo_script) - 1));
    }

    /* Read once here (and again only when a navigation click actually
     * changes cwd, below), not on every redraw -- this kernel's event loop
     * redraws on essentially any mouse movement, and re-reading the
     * directory sector that often would mean a PIO polling round-trip on
     * nearly every frame once this window exists. */
    /* /HOME, not root -- makes /HOME real in the way a Unix home
     * directory is, not just a name that exists (fs_bootstrap_dirs()
     * already guarantees it exists by this point in boot). ".."
     * navigation still reaches real root normally; this only changes
     * where the window starts. */
    {
        int pos = 0;
        str_append(cwd, &pos, (int)sizeof(cwd), "/HOME");
    }
    if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, &file_entry_count) != 0) {
        file_entry_count = 0;
    }

    {
        struct window_content wc = {
            .co = &co, .ci = &ci, .shell_co = &shell_co, .shell_ci = &shell_ci, .cwd = cwd,
            .file_entries = file_entries, .file_entry_count = file_entry_count,
            .files_selected_mask = files_selected_mask, .name_input = &name_input,
            .new_dir_btn = &new_dir_btn, .delete_btn = &delete_btn, .cut_btn = &cut_btn,
            .copy_btn = &copy_btn, .paste_btn = &paste_btn, .ed = &ed, .save_btn = &save_btn,
            .pt = &paint, .paint_name_input = &paint_name_input, .paint_save_btn = &paint_save_btn,
            .paint_load_btn = &paint_load_btn,
        };
        draw_scene(w, h, windows, fx_enabled, &wc, z_order, &bar, taskbar_hovered, &menu, menu_hovered_item, mx, my,
                  cursor_color);
    }
    gfx_present();

    for (;;) {
        int had_event = 0;
        int dx, dy, buttons;
        char c;
        int old_mx = mx;
        int old_my = my;
        int old_x[MAX_WINDOWS], old_y[MAX_WINDOWS], old_z[MAX_WINDOWS];
        int old_state[MAX_WINDOWS], old_min_hov[MAX_WINDOWS], old_close_hov[MAX_WINDOWS];
        int old_fx_enabled = fx_enabled;
        int old_taskbar_hovered = taskbar_hovered;
        int old_menu_open = menu.open;
        int old_menu_hovered_item = menu_hovered_item;
        int old_co_generation = co.generation;
        int old_ci_len = ci.len;
        int old_ci_cursor = ci.cursor;
        int old_ci_focused = ci.focused;
        char old_ci_text[CONSOLE_INPUT_MAX + 1];
        int old_shell_co_generation = shell_co.generation;
        int old_shell_ci_len = shell_ci.len;
        int old_shell_ci_cursor = shell_ci.cursor;
        int old_shell_ci_focused = shell_ci.focused;
        char old_shell_ci_text[CONSOLE_INPUT_MAX + 1];
        char old_cwd[FILES_PATH_MAX];
        uint32_t old_files_selected_mask = files_selected_mask;
        int old_delete_btn_hovered = delete_btn.hovered;
        int old_delete_btn_pressed = delete_btn.pressed;
        int old_new_dir_btn_hovered = new_dir_btn.hovered;
        int old_new_dir_btn_pressed = new_dir_btn.pressed;
        int old_cut_btn_hovered = cut_btn.hovered;
        int old_cut_btn_pressed = cut_btn.pressed;
        int old_copy_btn_hovered = copy_btn.hovered;
        int old_copy_btn_pressed = copy_btn.pressed;
        int old_paste_btn_hovered = paste_btn.hovered;
        int old_paste_btn_pressed = paste_btn.pressed;
        int old_name_input_len = name_input.len;
        int old_name_input_cursor = name_input.cursor;
        int old_name_input_focused = name_input.focused;
        char old_name_input_text[CONSOLE_INPUT_MAX + 1];
        unsigned int old_ed_len = ed.len;
        unsigned int old_ed_cursor = ed.cursor;
        int old_ed_focused = ed.focused;
        int old_save_btn_hovered = save_btn.hovered;
        int old_save_btn_pressed = save_btn.pressed;
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
        int i;

        for (i = 0; ci.text[i]; i++) {
            old_ci_text[i] = ci.text[i];
        }
        old_ci_text[i] = 0;

        for (i = 0; shell_ci.text[i]; i++) {
            old_shell_ci_text[i] = shell_ci.text[i];
        }
        old_shell_ci_text[i] = 0;

        for (i = 0; name_input.text[i]; i++) {
            old_name_input_text[i] = name_input.text[i];
        }
        old_name_input_text[i] = 0;

        for (i = 0; paint_name_input.text[i]; i++) {
            old_paint_name_input_text[i] = paint_name_input.text[i];
        }
        old_paint_name_input_text[i] = 0;

        for (i = 0; cwd[i]; i++) {
            old_cwd[i] = cwd[i];
        }
        old_cwd[i] = 0;

        for (i = 0; i < MAX_WINDOWS; i++) {
            old_x[i] = windows[i].x;
            old_y[i] = windows[i].y;
            old_z[i] = z_order[i];
            old_state[i] = windows[i].state;
            old_min_hov[i] = windows[i].minimize_hovered;
            old_close_hov[i] = windows[i].close_hovered;
        }

        /* Drain every mouse packet already queued before redrawing, rather
         * than redrawing once per packet: a real mouse streams packets far
         * faster than a full-screen software redraw can keep up with under
         * QEMU's TCG emulation, and the 32-byte ring buffer in mouse.c
         * drops individual bytes (not whole packets) once it backs up --
         * desyncing packet framing and producing garbage dx/dy decodes
         * (seen as the cursor jumping erratically). Collapsing a burst of
         * queued packets into one redraw keeps the loop caught up instead
         * of falling further behind with every packet. */
        while (mouse_poll_packet(&dx, &dy, &buttons)) {
            int left_held, right_held, cx, cy;

            mx += dx;
            my += dy;
            if (mx < 0) {
                mx = 0;
            }
            if (my < 0) {
                my = 0;
            }
            if (mx > w - CURSOR_SIZE) {
                mx = w - CURSOR_SIZE;
            }
            if (my > h - CURSOR_SIZE) {
                my = h - CURSOR_SIZE;
            }

            left_held = buttons & 0x01;
            right_held = buttons & 0x02;
            cx = mx + CURSOR_SIZE / 2;
            cy = my + CURSOR_SIZE / 2;

            /* Keeps mouse_buttons_live (poll_mouse_state()'s own output,
             * read by the PAINT Forth hooks) in sync even when kmain()'s
             * own loop -- not poll_mouse_state() -- is the one draining
             * this packet: without this, a button press consumed here
             * (e.g. the very mouse_button that's held right before RUN
             * PAINT starts a BEGIN...UNTIL loop, while kmain() is still
             * the thing running) would never reach mouse_buttons_live at
             * all, since poll_mouse_state() only updates it from packets
             * *it* personally drains -- leaving MOUSE-DOWN?/
             * MOUSE-RIGHT-DOWN? reading stale state the instant the loop
             * starts. */
            mouse_buttons_live = buttons;

            /* Dragging applies each packet's raw dx/dy to whichever window
             * is being dragged (and its content widgets, via
             * move_window_content()) the same way it's already applied to
             * the cursor above -- since PS/2 deltas are relative, this
             * keeps the cursor's position over the title bar constant for
             * the whole drag with no separate grab offset to track. A
             * press that isn't already dragging something looks up which
             * window (if any) is topmost under the cursor: that window is
             * raised to front, and if the press specifically landed on its
             * title bar, a drag starts too -- so grabbing a background
             * window's title bar both raises and starts moving it in one
             * motion, the same as any desktop. The start menu's button and
             * popup are checked first, ahead of the taskbar/window stack,
             * since they're the outermost layer of chrome -- a
             * click anywhere while the popup is open either hits an item
             * or closes it, never falls through to whatever's underneath. */
            if (dragging_window >= 0) {
                if (left_held) {
                    int drag_start_x = windows[dragging_window].x;
                    int drag_start_y = windows[dragging_window].y;
                    int applied_dx, applied_dy;

                    windows[dragging_window].x += dx;
                    windows[dragging_window].y += dy;
                    clamp_window_to_screen(&windows[dragging_window], w, h - TASKBAR_HEIGHT);

                    applied_dx = windows[dragging_window].x - drag_start_x;
                    applied_dy = windows[dragging_window].y - drag_start_y;
                    {
                        struct window_content wc = {
                            .co = &co, .ci = &ci, .shell_co = &shell_co, .shell_ci = &shell_ci, .cwd = cwd,
                            .file_entries = file_entries, .file_entry_count = file_entry_count,
                            .files_selected_mask = files_selected_mask, .name_input = &name_input,
                            .new_dir_btn = &new_dir_btn, .delete_btn = &delete_btn, .cut_btn = &cut_btn,
                            .copy_btn = &copy_btn, .paste_btn = &paste_btn, .ed = &ed, .save_btn = &save_btn,
                            .pt = &paint, .paint_name_input = &paint_name_input, .paint_save_btn = &paint_save_btn,
                            .paint_load_btn = &paint_load_btn,
                        };
                        move_window_content(dragging_window, &wc, applied_dx, applied_dy);
                    }
                } else {
                    dragging_window = -1;
                }
            } else if (left_held && !prev_left_held && menu.open && startmenu_hit_item(&menu, cx, cy) >= 0) {
                int item = startmenu_hit_item(&menu, cx, cy);

                if (item == STARTMENU_ITEM_EXIT) {
                    power_shutdown();
                } else if (item == STARTMENU_ITEM_FX) {
                    fx_enabled = !fx_enabled;
                } else if (item == STARTMENU_ITEM_FORTH) {
                    windows[WIN_KIND_FORTH].state = WINDOW_OPEN;
                    raise_window(z_order, WIN_KIND_FORTH);
                } else if (item == STARTMENU_ITEM_FILES) {
                    /* Real bug, found 2026-08-18 while verifying an
                     * unrelated feature: this used to just open+raise
                     * without ever calling fs_list_dir() again, so it
                     * always showed whatever was listed once at boot,
                     * stale regardless of what's since changed on disk
                     * -- unlike CONFIG/GAMES below, which already
                     * route through open_files_at() and refresh.
                     * Passing cwd as its own target re-lists the
                     * *current* directory instead of navigating away;
                     * safe even though cwd aliases itself as both
                     * dst and src here, since str_append()'s copy
                     * loop writes dst[i] = src[i] in lockstep, each
                     * byte set to itself, never reading ahead of what
                     * it's already written. */
                    open_files_at(cwd, (int)sizeof(cwd), cwd, windows, z_order, file_entries, &file_entry_count,
                                 &files_selected_mask);
                } else if (item == STARTMENU_ITEM_SHELL) {
                    windows[WIN_KIND_SHELL].state = WINDOW_OPEN;
                    raise_window(z_order, WIN_KIND_SHELL);
                } else if (item == STARTMENU_ITEM_CONFIG) {
                    open_files_at(cwd, (int)sizeof(cwd), "/ETC", windows, z_order, file_entries, &file_entry_count,
                                 &files_selected_mask);
                } else if (item == STARTMENU_ITEM_GAMES) {
                    open_files_at(cwd, (int)sizeof(cwd), "/GAMES", windows, z_order, file_entries, &file_entry_count,
                                 &files_selected_mask);
                }
                menu.open = 0;
            } else if (left_held && !prev_left_held && startmenu_hit_button(&menu, cx, cy)) {
                menu.open = !menu.open;
            } else if (left_held && !prev_left_held && menu.open) {
                /* Click landed somewhere else while the popup was open --
                 * closes it without acting on whatever's underneath, same
                 * as clicking away from a real start menu. */
                menu.open = 0;
            } else if (left_held && !prev_left_held && taskbar_hit_entry(&bar, windows, MAX_WINDOWS, cx, cy) >= 0) {
                /* The taskbar sits above every window (windows are
                 * clamped to never go under it), so its clicks are
                 * checked before window hit-testing rather than folded
                 * into topmost_window_at() -- it isn't part of the
                 * window stack at all. Clicking the already-active
                 * window's entry minimizes it (a toggle, same as
                 * clicking a real taskbar button twice); clicking any
                 * other entry (minimized, or open but not frontmost)
                 * restores/raises it. */
                int entry = taskbar_hit_entry(&bar, windows, MAX_WINDOWS, cx, cy);

                if (windows[entry].state == WINDOW_OPEN && z_order[0] == entry) {
                    windows[entry].state = WINDOW_MINIMIZED;
                } else {
                    windows[entry].state = WINDOW_OPEN;
                    raise_window(z_order, entry);
                }
            } else if (left_held && !prev_left_held) {
                int target = topmost_window_at(windows, z_order, cx, cy);

                if (target >= 0) {
                    /* Controls take priority over raising/dragging -- a
                     * click on close or minimize acts immediately rather
                     * than raising the window to front first, same as any
                     * real desktop. */
                    if (window_close_hit_test(&windows[target], cx, cy)) {
                        windows[target].state = WINDOW_CLOSED;
                        if (target == WIN_KIND_PAINT && paint_program_slot >= 0) {
                            scheduler_request_close(paint_program_slot);
                        }
                    } else if (window_minimize_hit_test(&windows[target], cx, cy)) {
                        windows[target].state = WINDOW_MINIMIZED;
                    } else {
                        raise_window(z_order, target);
                        if (window_titlebar_hit_test(&windows[target], cx, cy)) {
                            dragging_window = target;
                            /* Dragging any window while PAINT's popup is
                             * open would leave it rendered at a stale
                             * position relative to a window that just
                             * moved -- simplest fix is closing it
                             * outright rather than threading a second
                             * movable position through
                             * move_window_content(). */
                            paint.palette_popup_open = 0;
                        }
                    }
                }
            }

            /* Hover-highlight whichever window's controls are actually
             * under the cursor -- only the topmost window at that point,
             * so an occluded window's controls never light up. Runs every
             * packet (not just click edges), same as button hover
             * elsewhere. */
            {
                int hover_target = topmost_window_at(windows, z_order, cx, cy);
                int wi;

                for (wi = 0; wi < MAX_WINDOWS; wi++) {
                    int is_target = (wi == hover_target);
                    windows[wi].minimize_hovered = is_target && window_minimize_hit_test(&windows[wi], cx, cy);
                    windows[wi].close_hovered = is_target && window_close_hit_test(&windows[wi], cx, cy);
                }
            }

            taskbar_hovered = taskbar_hit_entry(&bar, windows, MAX_WINDOWS, cx, cy);
            menu_hovered_item = startmenu_hit_item(&menu, cx, cy);

            {
                int topmost = topmost_window_at(windows, z_order, cx, cy);
                int forth_is_topmost = topmost == WIN_KIND_FORTH;
                int files_is_topmost = topmost == WIN_KIND_FILES;
                int shell_is_topmost = topmost == WIN_KIND_SHELL;
                int editor_is_topmost = topmost == WIN_KIND_EDITOR;
                int paint_is_topmost = topmost == WIN_KIND_PAINT;
                int click_edge = left_held && !prev_left_held;

                /* Any click edge sets focus: hitting the field itself
                 * focuses it, anything else (another window, the
                 * backdrop, the start menu) defocuses it -- same
                 * single-focus-owner behavior as clicking around a normal
                 * desktop text field. */
                if (click_edge) {
                    ci.focused = forth_is_topmost && console_input_hit_test(&ci, cx, cy);
                    name_input.focused = files_is_topmost && console_input_hit_test(&name_input, cx, cy);
                    shell_ci.focused = shell_is_topmost && console_input_hit_test(&shell_ci, cx, cy);
                    ed.focused = editor_is_topmost && editor_hit_test(&ed, cx, cy);
                    paint_name_input.focused = paint_is_topmost && console_input_hit_test(&paint_name_input, cx, cy);
                }

                /* Clicking ".." or a directory row navigates and re-lists
                 * immediately (see the boot-time fs_list_dir() call for
                 * why this stays an explicit "only when cwd actually
                 * changes" call rather than something re-run every
                 * redraw). Clicking a file is a no-op -- there's no
                 * VIEWER window to show its content in anymore, same as
                 * every other not-yet-supported click in this window
                 * (e.g. the path header row). */
                if (files_is_topmost && click_edge) {
                    int hit = files_list_hit_test(&windows[WIN_KIND_FILES], cwd, file_entry_count, cx, cy, name_input.y);

                    if (hit == FILES_HIT_UP) {
                        fs_path_parent(cwd);
                        files_selected_mask = 0; /* stale relative to the new listing */
                        if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, &file_entry_count) != 0) {
                            file_entry_count = 0;
                        }
                    } else if (hit >= 0 && file_entries[hit].type == FS_TYPE_DIR) {
                        char new_cwd[FILES_PATH_MAX];
                        fs_path_join(new_cwd, (int)sizeof(new_cwd), cwd, file_entries[hit].name);
                        {
                            int ci2;
                            for (ci2 = 0; new_cwd[ci2] && ci2 < (int)sizeof(cwd) - 1; ci2++) {
                                cwd[ci2] = new_cwd[ci2];
                            }
                            cwd[ci2] = 0;
                        }
                        files_selected_mask = 0; /* stale relative to the new listing */
                        if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, &file_entry_count) != 0) {
                            file_entry_count = 0;
                        }
                    }
                }

                /* Multi-select: right-click toggles that row's own bit,
                 * leaving every other row's selection state alone --
                 * unlike the old single-index version, several rows can
                 * be selected at once now (needed for CUT/COPY/DELETE to
                 * act on more than one file per click). */
                if (files_is_topmost && right_held && !prev_right_held) {
                    int hit = files_list_hit_test(&windows[WIN_KIND_FILES], cwd, file_entry_count, cx, cy, name_input.y);
                    if (hit >= 0) {
                        files_selected_mask ^= (1u << hit);
                    }
                }

                /* Deletes every row named by a set bit in
                 * files_selected_mask -- a generalization of the old
                 * single-index delete, not a behavior change when only
                 * one bit is ever set. Each row is attempted
                 * independently (a non-empty directory or an
                 * already-stale entry just makes that one fs_delete()
                 * fail, silently, same as before); the mask always resets
                 * and the listing always re-reads afterward regardless of
                 * any individual failure, since row indices are stale
                 * either way once anything might have changed. */
                delete_btn.hovered = files_is_topmost && button_hit_test(&delete_btn, cx, cy);
                if (delete_btn.hovered && click_edge && files_selected_mask != 0) {
                    unsigned int di;
                    for (di = 0; di < file_entry_count; di++) {
                        if (files_selected_mask & (1u << di)) {
                            char del_path[FILES_PATH_MAX];
                            fs_path_join(del_path, (int)sizeof(del_path), cwd, file_entries[di].name);
                            fs_delete(del_path);
                        }
                    }
                    files_selected_mask = 0;
                    if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, &file_entry_count) != 0) {
                        file_entry_count = 0;
                    }
                }
                delete_btn.pressed = delete_btn.hovered && left_held;

                /* Creates name_input's typed name as a directory in cwd.
                 * An empty name or an already-existing/full-table failure
                 * from fs_create_dir() is a silent no-op, same as DELETE
                 * above -- but unlike a successful delete (which clears
                 * files_selected_mask), a failed create leaves the typed
                 * name in place so the user can see and fix it. Creating a
                 * plain file has no button of its own -- see Enter
                 * handling in the keyboard block below. */
                new_dir_btn.hovered = files_is_topmost && button_hit_test(&new_dir_btn, cx, cy);
                if (new_dir_btn.hovered && click_edge && name_input.text[0] != 0) {
                    char new_path[FILES_PATH_MAX];
                    fs_path_join(new_path, (int)sizeof(new_path), cwd, name_input.text);
                    if (fs_create_dir(new_path) == 0) {
                        console_input_clear(&name_input);
                        if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, &file_entry_count) != 0) {
                            file_entry_count = 0;
                        }
                    }
                }
                new_dir_btn.pressed = new_dir_btn.hovered && left_held;

                /* CUT/COPY stage the current selection into clipboard
                 * (files only -- a selected directory is silently
                 * skipped inside files_clipboard_stage()). An empty
                 * selection, or one covering only directories, leaves
                 * whatever the clipboard already held untouched. */
                cut_btn.hovered = files_is_topmost && button_hit_test(&cut_btn, cx, cy);
                if (cut_btn.hovered && click_edge && files_selected_mask != 0) {
                    files_clipboard_stage(&clipboard, cwd, file_entries, file_entry_count, files_selected_mask, 1);
                }
                cut_btn.pressed = cut_btn.hovered && left_held;

                copy_btn.hovered = files_is_topmost && button_hit_test(&copy_btn, cx, cy);
                if (copy_btn.hovered && click_edge && files_selected_mask != 0) {
                    files_clipboard_stage(&clipboard, cwd, file_entries, file_entry_count, files_selected_mask, 0);
                }
                copy_btn.pressed = copy_btn.hovered && left_held;

                /* PASTE applies every clipboard entry against cwd --
                 * wherever FILES has navigated to is the destination,
                 * which is what makes navigating double as picking where
                 * to paste. Each name is attempted independently; one
                 * failing (e.g. a name collision at the destination)
                 * doesn't stop the rest. A successful CUT's clipboard
                 * clears after paste, but only if at least one fs_move()
                 * in the batch actually succeeded -- if every one failed
                 * (e.g. every name collides at the destination), the
                 * originals are all still sitting at source_dir, so
                 * throwing away the cut selection would leave the user no
                 * way to retry it elsewhere. A COPY's clipboard is always
                 * left intact so the same files can be pasted into
                 * several directories in a row. */
                paste_btn.hovered = files_is_topmost && button_hit_test(&paste_btn, cx, cy);
                if (paste_btn.hovered && click_edge && clipboard.count > 0) {
                    unsigned int pi;
                    int any_succeeded = 0;
                    for (pi = 0; pi < clipboard.count; pi++) {
                        char paste_src[FILES_PATH_MAX];
                        fs_path_join(paste_src, (int)sizeof(paste_src), clipboard.source_dir, clipboard.names[pi]);
                        if (clipboard.is_cut) {
                            if (fs_move(paste_src, cwd) == 0) {
                                any_succeeded = 1;
                            }
                        } else {
                            fs_copy_file(paste_src, cwd);
                        }
                    }
                    if (clipboard.is_cut && any_succeeded) {
                        clipboard.count = 0;
                    }
                    files_selected_mask = 0; /* stale relative to the re-list below (pasted entries can shift indices) */
                    if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, &file_entry_count) != 0) {
                        file_entry_count = 0;
                    }
                }
                paste_btn.pressed = paste_btn.hovered && left_held;

                /* Writes the buffer back to disk: fs_delete() first
                 * (return value ignored -- "doesn't exist yet" is
                 * expected and fine, not an error), then
                 * fs_create_file() with the buffer's current content.
                 * Unifies "this file doesn't exist yet" and "overwrite
                 * this file's existing content" into one path, since
                 * after an unconditional delete attempt both cases look
                 * identical to fs_create_file(). A failure (parent
                 * directory disappeared, disk full, ...) is a silent
                 * no-op, same no-error-UI convention every other
                 * filesystem mutation in this kernel already follows. */
                save_btn.hovered = editor_is_topmost && button_hit_test(&save_btn, cx, cy);
                if (save_btn.hovered && click_edge && editor_path[0] != 0) {
                    /* fs.c's allocator is one-way (bump, never reclaims),
                     * so this fs_delete()+fs_create_file() pair leaks
                     * editor_path's previous on-disk allocation on every
                     * single SAVE -- unlike every other fs_create_file()
                     * caller in this kernel, which only ever creates a
                     * file once. Repeatedly saving the same path is what
                     * actually exhausts the disk over time, not one-time
                     * file creation elsewhere. */
                    fs_delete(editor_path);
                    fs_create_file(editor_path, ed.buf, ed.len);
                }
                save_btn.pressed = save_btn.hovered && left_held;

                /* Click routing for the palette-chooser popup: closed
                 * + click on the swatch opens it; open + click on a
                 * grid swatch selects it and closes the popup; open +
                 * click on any *other* window (or the backdrop) closes
                 * it without changing current_color, same "click
                 * outside dismisses" convention the start menu's own
                 * popup already uses. (Task 3 adds right-click
                 * hide/restore and the "hidden swatches are a no-op"
                 * rule to the grid-click branch below.) */
                if (click_edge) {
                    if (paint_is_topmost) {
                        if (!paint.palette_popup_open) {
                            if (paint_swatch_hit_test(&windows[WIN_KIND_PAINT], cx, cy)) {
                                paint.palette_popup_open = 1;
                            }
                        } else {
                            int swatch = paint_popup_grid_hit_test(&windows[WIN_KIND_PAINT], cx, cy);
                            /* A hidden swatch can't be selected -- same
                             * "no-error-UI, silent no-op" convention
                             * SAVE/LOAD's own failure paths already use
                             * -- but the popup stays open either way,
                             * so a miss or a hidden pick doesn't force
                             * the user to reopen it. */
                            if (swatch >= 0 && !(paint.palette_hidden_mask & (1u << swatch))) {
                                paint.current_color = swatch;
                                paint.palette_popup_open = 0;
                            } else if (swatch < 0) {
                                paint.palette_popup_open = 0;
                            }
                        }
                    } else if (paint.palette_popup_open) {
                        paint.palette_popup_open = 0;
                    }
                }

                if (paint_is_topmost && paint.palette_popup_open && right_held && !prev_right_held) {
                    int swatch = paint_popup_grid_hit_test(&windows[WIN_KIND_PAINT], cx, cy);
                    if (swatch >= 0) {
                        paint.palette_hidden_mask ^= 1u << swatch;
                    }
                }

                /* Writes the grid to /HOME/<name> (the filename field's
                 * own text, empty defaulting handled by
                 * paint_build_sprite_path()) as 256 raw bytes, one per
                 * cell, row-major -- same fs_delete()+fs_create_file()
                 * shape EDITOR's own SAVE already uses, and the same
                 * silent-no-op-on-failure convention. */
                paint_save_btn.hovered = paint_is_topmost && button_hit_test(&paint_save_btn, cx, cy);
                if (paint_save_btn.hovered && click_edge) {
                    char path[FS_PATH_MAX];
                    if (paint_build_sprite_path(path, (int)sizeof(path))) {
                        unsigned char sprite_bytes[PAINT_GRID_SIZE * PAINT_GRID_SIZE];
                        int prow, pcol;
                        for (prow = 0; prow < PAINT_GRID_SIZE; prow++) {
                            for (pcol = 0; pcol < PAINT_GRID_SIZE; pcol++) {
                                sprite_bytes[prow * PAINT_GRID_SIZE + pcol] = (unsigned char)paint.grid[prow][pcol];
                            }
                        }
                        fs_delete(path);
                        fs_create_file(path, sprite_bytes, sizeof(sprite_bytes));
                    }
                }
                paint_save_btn.pressed = paint_save_btn.hovered && left_held;

                /* The other half of SAVE's round-trip: reads /HOME/<name>
                 * back into the grid. Only accepts it if the file is
                 * exactly 256 bytes -- fs_read_file() only rejects "too
                 * big for buf_size", not "too small", so a truncated or
                 * wrong-format file would otherwise partially load with
                 * the rest of sprite_bytes left as stack garbage. Each
                 * byte is also clamped to a valid palette index
                 * (< PAINT_PALETTE_COLORS, else 0): draw_paint_group()
                 * indexes paint_palette[] with pt->grid[row][col]
                 * completely unchecked, so a stray out-of-range byte
                 * from a bad or hand-edited file would read past that
                 * array. Anything else (missing file, wrong size) is a
                 * silent no-op, same convention as SAVE with an empty
                 * name. */
                paint_load_btn.hovered = paint_is_topmost && button_hit_test(&paint_load_btn, cx, cy);
                if (paint_load_btn.hovered && click_edge) {
                    char path[FS_PATH_MAX];
                    if (paint_build_sprite_path(path, (int)sizeof(path))) {
                        unsigned char sprite_bytes[PAINT_GRID_SIZE * PAINT_GRID_SIZE];
                        unsigned int out_size;
                        if (fs_read_file(path, sprite_bytes, sizeof(sprite_bytes), &out_size) == 0 &&
                            out_size == sizeof(sprite_bytes)) {
                            int prow, pcol;
                            for (prow = 0; prow < PAINT_GRID_SIZE; prow++) {
                                for (pcol = 0; pcol < PAINT_GRID_SIZE; pcol++) {
                                    unsigned char v = sprite_bytes[prow * PAINT_GRID_SIZE + pcol];
                                    paint.grid[prow][pcol] = (v < PAINT_PALETTE_COLORS) ? v : 0;
                                }
                            }
                        }
                    }
                }
                paint_load_btn.pressed = paint_load_btn.hovered && left_held;
            }

            prev_left_held = left_held;
            prev_right_held = right_held;
            cursor_color = left_held ? CURSOR_CLICK_COLOR : CURSOR_IDLE_COLOR;

            had_event = 1;
        }

        if (keyboard_poll_char(&c)) {
            if (ci.focused && c == KEY_UP) {
                char recalled[CONSOLE_INPUT_MAX + 1];
                if (console_history_prev(&hist, recalled)) {
                    console_input_set_text(&ci, recalled);
                }
            } else if (ci.focused && c == KEY_DOWN) {
                char recalled[CONSOLE_INPUT_MAX + 1];
                if (console_history_next(&hist, recalled)) {
                    console_input_set_text(&ci, recalled);
                }
            } else if (ci.focused && c == KEY_LEFT) {
                console_input_move_cursor(&ci, -1);
            } else if (ci.focused && c == KEY_RIGHT) {
                console_input_move_cursor(&ci, 1);
            } else if (ci.focused) {
                /* A real edit (typing or backspace, not just moving the
                 * cursor) -- the next KEY_UP should start browsing over
                 * from the most recent entry again, not continue from
                 * wherever a previous recall left off. */
                console_history_reset_browse(&hist);
                if (console_input_feed_char(&ci, c)) {
                    char echoed[CONSOLE_INPUT_MAX + 4];
                    const char *run_arg;
                    int pos = 0;

                    str_append(echoed, &pos, (int)sizeof(echoed), "> ");
                    str_append(echoed, &pos, (int)sizeof(echoed), ci.text);
                    console_output_append_line(&co, echoed);

                    /* RUN is a console-level convenience, not real Forth
                     * syntax -- only intercepted outside compile mode, so
                     * a stray "RUN" token typed inside a ':'/';' body
                     * falls through to the compiler as normal (where it
                     * just errors UNKNOWN, same as any other undefined
                     * word). */
                    run_arg = vm.compiling ? 0 : match_run_command(ci.text);
                    if (run_arg) {
                        handle_run_command(&co, run_arg);
                    } else {
                        char out[128];
                        /* forth_eval_line() never touches console_output.h
                         * itself (see forth.h) -- it writes '\n'-separated
                         * output into out[], and splitting that into
                         * separate console lines is kernel.c's job, done
                         * here rather than inside forth.c. */
                        forth_eval_line(&vm, ci.text, out, sizeof(out));
                        append_split_lines(&co, out);
                    }

                    console_history_push(&hist, ci.text);
                    console_input_clear(&ci);
                }
            } else if (shell_ci.focused && c == KEY_UP) {
                char recalled[CONSOLE_INPUT_MAX + 1];
                if (console_history_prev(&shell_hist, recalled)) {
                    console_input_set_text(&shell_ci, recalled);
                }
            } else if (shell_ci.focused && c == KEY_DOWN) {
                char recalled[CONSOLE_INPUT_MAX + 1];
                if (console_history_next(&shell_hist, recalled)) {
                    console_input_set_text(&shell_ci, recalled);
                }
            } else if (shell_ci.focused && c == KEY_LEFT) {
                console_input_move_cursor(&shell_ci, -1);
            } else if (shell_ci.focused && c == KEY_RIGHT) {
                console_input_move_cursor(&shell_ci, 1);
            } else if (shell_ci.focused) {
                console_history_reset_browse(&shell_hist);
                if (console_input_feed_char(&shell_ci, c)) {
                    /* "<cwd> > <command>" -- unlike FORTH's bare "> "
                     * (Forth has no notion of a current location), the
                     * shell's whole state includes cwd, so it's worth
                     * always showing without a separate pwd. Sized for
                     * the worst case (a full FS_PATH_MAX cwd plus a full
                     * CONSOLE_INPUT_MAX line); console_output_append_line()
                     * still truncates to CONSOLE_LINE_MAX for display,
                     * same as any other long line in this console. */
                    char echoed[FS_PATH_MAX + CONSOLE_INPUT_MAX + 8];
                    const char *edit_arg;
                    const char *run_arg;
                    int pos = 0;

                    str_append(echoed, &pos, (int)sizeof(echoed), sh.cwd);
                    str_append(echoed, &pos, (int)sizeof(echoed), " > ");
                    str_append(echoed, &pos, (int)sizeof(echoed), shell_ci.text);
                    console_output_append_line(&shell_co, echoed);

                    /* EDIT and RUN are console-level conveniences, not
                     * real shell_eval_line() commands -- intercepted
                     * before falling through, same precedent RUN
                     * already set for FORTH. */
                    edit_arg = match_edit_command(shell_ci.text);
                    run_arg = match_run_command(shell_ci.text);
                    if (edit_arg) {
                        handle_edit_command(&sh, &ed, editor_path, (int)sizeof(editor_path), editor_title,
                                            (int)sizeof(editor_title), windows, z_order, &shell_co, edit_arg);
                    } else if (run_arg) {
                        handle_run_command(&shell_co, run_arg);
                    } else {
                        char shell_out[VIEWER_BUF_SIZE];
                        shell_eval_line(&sh, shell_ci.text, shell_out, (int)sizeof(shell_out));
                        append_split_lines(&shell_co, shell_out);
                    }

                    console_history_push(&shell_hist, shell_ci.text);
                    console_input_clear(&shell_ci);
                }
            } else if (name_input.focused && c == KEY_LEFT) {
                console_input_move_cursor(&name_input, -1);
            } else if (name_input.focused && c == KEY_RIGHT) {
                console_input_move_cursor(&name_input, 1);
            } else if (name_input.focused) {
                /* Enter's meaning depends on whether a row is selected
                 * (Stage D's right-click selection, files_selected_mask):
                 * with a selection, it renames that entry to the typed
                 * name; with none, it creates a plain file with that
                 * name, same as before -- NEW DIR (the click handler
                 * above) remains the only way to create a directory.
                 * Splitting the action on selection state, rather than
                 * adding a third RENAME button, follows the same
                 * one-field reasoning Stage E used for NEW DIR vs.
                 * Enter-creates-a-file. Either way, an empty name or a
                 * failure (fs_rename()/fs_create_file() returning
                 * nonzero -- not found, name taken, too long, ...) is a
                 * silent no-op that leaves the typed name in place, same
                 * as NEW DIR's failure case. A successful rename leaves
                 * files_selected_mask as-is: renaming is in place, so the
                 * same table slot -- and thus the same listing index --
                 * still names the (now renamed) entry. */
                if (console_input_feed_char(&name_input, c) && name_input.text[0] != 0) {
                    int ok = 0;
                    if (files_selected_mask == 0) {
                        char new_path[FILES_PATH_MAX];
                        fs_path_join(new_path, (int)sizeof(new_path), cwd, name_input.text);
                        ok = fs_create_file(new_path, 0, 0) == 0;
                    } else if ((files_selected_mask & (files_selected_mask - 1)) == 0) {
                        /* Exactly one bit set (a power of two, including
                         * this check itself being the standard
                         * single-bit test) -- rename that one entry,
                         * same meaning the old files_selected >= 0
                         * branch had before multi-select existed. */
                        unsigned int idx;
                        for (idx = 0; idx < file_entry_count; idx++) {
                            if (files_selected_mask & (1u << idx)) {
                                break;
                            }
                        }
                        /* idx can reach file_entry_count without matching
                         * only if file_entry_count shrank out from under
                         * a stale mask (e.g. a prior fs_list_dir()
                         * failure) -- guard against reading
                         * file_entries[idx] out of the currently-valid
                         * range; ok stays 0, a silent no-op. */
                        if (idx < file_entry_count) {
                            char old_path[FILES_PATH_MAX];
                            fs_path_join(old_path, (int)sizeof(old_path), cwd, file_entries[idx].name);
                            ok = fs_rename(old_path, name_input.text) == 0;
                        }
                    }
                    /* else: more than one row selected -- a single typed
                     * name can't unambiguously rename several entries,
                     * so Enter is a silent no-op here, same convention
                     * as every other unsupported action in this window. */
                    if (ok) {
                        console_input_clear(&name_input);
                        if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, &file_entry_count) != 0) {
                            file_entry_count = 0;
                        }
                    }
                }
            } else if (ed.focused && c == KEY_UP) {
                editor_move_line(&ed, -1);
            } else if (ed.focused && c == KEY_DOWN) {
                editor_move_line(&ed, 1);
            } else if (ed.focused && c == KEY_LEFT) {
                editor_move_cursor(&ed, -1);
            } else if (ed.focused && c == KEY_RIGHT) {
                editor_move_cursor(&ed, 1);
            } else if (ed.focused && c == KEY_HOME) {
                editor_move_home(&ed);
            } else if (ed.focused && c == KEY_END) {
                editor_move_end(&ed);
            } else if (ed.focused && c == KEY_DELETE) {
                editor_delete_forward(&ed);
            } else if (ed.focused) {
                editor_feed_char(&ed, c);
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
            }
            had_event = 1;
        }

        scheduler_tick();

        {
            unsigned char *refill_buf;
            unsigned int refill_len;
            if (sb16_stream_needs_refill(&refill_buf, &refill_len)) {
                synth_render_half(refill_buf, refill_len);
                sb16_stream_refill_done(refill_buf);
            }
        }

        if (had_event || scheduler_any_active()) {
            int touched[MAX_WINDOWS];
            int menu_touched = (menu.open != old_menu_open) || (menu_hovered_item != old_menu_hovered_item);

            /* Position, open/minimized/closed state, and control hover are
             * generic to every window regardless of content; the rest is
             * specific to each window's own content, folded in on top. */
            for (i = 0; i < MAX_WINDOWS; i++) {
                touched[i] = (windows[i].x != old_x[i]) || (windows[i].y != old_y[i]) ||
                             (windows[i].state != old_state[i]) ||
                             (windows[i].minimize_hovered != old_min_hov[i]) ||
                             (windows[i].close_hovered != old_close_hov[i]);
            }
            touched[WIN_KIND_FORTH] = touched[WIN_KIND_FORTH] || (co.generation != old_co_generation) ||
                                      (ci.len != old_ci_len) || (ci.cursor != old_ci_cursor) ||
                                      (ci.focused != old_ci_focused) || !str_eq(ci.text, old_ci_text);
            touched[WIN_KIND_FILES] = touched[WIN_KIND_FILES] || !str_eq(cwd, old_cwd) ||
                                      (files_selected_mask != old_files_selected_mask) ||
                                      (delete_btn.hovered != old_delete_btn_hovered) ||
                                      (delete_btn.pressed != old_delete_btn_pressed) ||
                                      (new_dir_btn.hovered != old_new_dir_btn_hovered) ||
                                      (new_dir_btn.pressed != old_new_dir_btn_pressed) ||
                                      (cut_btn.hovered != old_cut_btn_hovered) ||
                                      (cut_btn.pressed != old_cut_btn_pressed) ||
                                      (copy_btn.hovered != old_copy_btn_hovered) ||
                                      (copy_btn.pressed != old_copy_btn_pressed) ||
                                      (paste_btn.hovered != old_paste_btn_hovered) ||
                                      (paste_btn.pressed != old_paste_btn_pressed) ||
                                      (name_input.len != old_name_input_len) ||
                                      (name_input.cursor != old_name_input_cursor) ||
                                      (name_input.focused != old_name_input_focused) ||
                                      !str_eq(name_input.text, old_name_input_text);
            touched[WIN_KIND_SHELL] = touched[WIN_KIND_SHELL] || (shell_co.generation != old_shell_co_generation) ||
                                      (shell_ci.len != old_shell_ci_len) ||
                                      (shell_ci.cursor != old_shell_ci_cursor) ||
                                      (shell_ci.focused != old_shell_ci_focused) ||
                                      !str_eq(shell_ci.text, old_shell_ci_text);
            /* ed.len alone is a reliable proxy for "content changed" --
             * every edit (insert or backspace) changes len by exactly
             * one, and unlike console_input's history recall
             * (console_input_set_text(), which can replace a whole
             * field with a same-length different string in one call),
             * nothing in this widget can change buf's content while
             * leaving len identical -- editor_set_text()/editor_clear()
             * are only ever called from handle_edit_command() at
             * window-open time, already caught by the generic
             * per-window state-transition check above. */
            touched[WIN_KIND_EDITOR] = touched[WIN_KIND_EDITOR] || (ed.len != old_ed_len) ||
                                      (ed.cursor != old_ed_cursor) || (ed.focused != old_ed_focused) ||
                                      (save_btn.hovered != old_save_btn_hovered) ||
                                      (save_btn.pressed != old_save_btn_pressed);
            /* paint.grid[][]'s own content changes (via PIXEL) are
             * caught via paint.grid_generation (bumped unconditionally
             * by forth_hook_pixel() -- see its own comment) rather than
             * diffing all 256 cells here every frame. This used to rely
             * on REFRESH's own synchronous mid-loop redraw, but REFRESH
             * was deleted in the 2026-08-19 concurrency pass; investigating
             * that stale assumption (2026-08-22) found grid updates were
             * still visibly redrawing live in the common case only by
             * accident -- either another window's damage happened to
             * overlap PAINT's canvas, or (for real mouse-driven painting)
             * the cursor's own per-frame footprint sits on the affected
             * cell -- not because content changes were actually tracked.
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
            {
                struct window_content wc = {
                    .co = &co, .ci = &ci, .shell_co = &shell_co, .shell_ci = &shell_ci, .cwd = cwd,
                    .file_entries = file_entries, .file_entry_count = file_entry_count,
                    .files_selected_mask = files_selected_mask, .name_input = &name_input,
                    .new_dir_btn = &new_dir_btn, .delete_btn = &delete_btn, .cut_btn = &cut_btn,
                    .copy_btn = &copy_btn, .paste_btn = &paste_btn, .ed = &ed, .save_btn = &save_btn,
                    .pt = &paint, .paint_name_input = &paint_name_input, .paint_save_btn = &paint_save_btn,
                    .paint_load_btn = &paint_load_btn,
                };
                update_and_present(w, h, windows, fx_enabled, &wc, z_order, old_z, old_mx, old_my, mx, my,
                                   cursor_color, old_x, old_y, touched, fx_enabled != old_fx_enabled, &bar,
                                   taskbar_hovered, old_taskbar_hovered, &menu, menu_hovered_item, menu_touched);
            }
        } else {
            __asm__ volatile("hlt");
        }
    }
}
