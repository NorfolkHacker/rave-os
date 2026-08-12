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
#include "textfield.h"
#include "checkbox.h"
#include "taskbar.h"
#include "desktop_icon.h"
#include "console_input.h"
#include "console_history.h"
#include "console_output.h"
#include "forth.h"
#include "io.h"
#include "ata.h"
#include "fs.h"

/* Note: stage2 switches the display into a VBE graphics mode before the
 * kernel even starts, so raw VGA text-mode writes at 0xB8000 don't apply
 * here -- text.c (built on font.c, ported from ACIDSTORM) is the real
 * text output path now. */

#define CURSOR_SIZE 8

/* Five windows exist right now (the interactive panel, a small static
 * info window, the Forth console, a navigable file manager -- which can
 * now delete a selected entry too, create/rename still later -- and a
 * read-only viewer for whatever file was last opened), tracked as a real
 * array/z-order list
 * rather than named locals so every window is treated uniformly
 * regardless of what's inside it. Content still differs per window, so
 * each window's content is drawn via a kind-indexed dispatch
 * (draw_window_by_index()) rather than a generic widget framework --
 * there are exactly five content kinds, not an open-ended number, so a
 * small switch is simpler than a real polymorphic app system. */
#define MAX_WINDOWS 5
#define WIN_KIND_PANEL 0
#define WIN_KIND_INFO 1
#define WIN_KIND_FORTH 2
#define WIN_KIND_FILES 3
#define WIN_KIND_VIEWER 4

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

/* fx_enabled gates the glow blob -- the panel's "FX" checkbox toggles it,
 * the one existing visual effect there was to wire a checkbox labeled
 * that to. Without it, the backdrop is flat near-black. */
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
 * window, not just the panel -- callers pass whichever one they mean. */
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

/* Moves a window's content widgets by the same delta already applied to
 * the window itself during a drag. This is the single place that
 * happens now, replacing what used to be a hand-written list of
 * `widget.x += dx` lines duplicated at each drag site -- exactly the
 * spot that twice forgot a widget earlier this session (the text field's
 * position, then almost the checkbox's) when a new one was added.
 * Centralizing it here means a future panel widget only needs to be
 * added in one place to drag correctly, not remembered at every call
 * site that moves the panel. */
static void move_window_content(int kind, struct button *btn, struct button *exit_btn, struct textfield *tf,
                                struct checkbox *cb, struct console_output *co, struct console_input *ci,
                                struct console_input *name_input, struct button *new_dir_btn,
                                struct button *delete_btn, struct console_output *viewer_co, int applied_dx,
                                int applied_dy) {
    if (kind == WIN_KIND_PANEL) {
        btn->x += applied_dx;
        btn->y += applied_dy;
        exit_btn->x += applied_dx;
        exit_btn->y += applied_dy;
        tf->x += applied_dx;
        tf->y += applied_dy;
        cb->x += applied_dx;
        cb->y += applied_dy;
    } else if (kind == WIN_KIND_FORTH) {
        co->x += applied_dx;
        co->y += applied_dy;
        ci->x += applied_dx;
        ci->y += applied_dy;
    } else if (kind == WIN_KIND_FILES) {
        name_input->x += applied_dx;
        name_input->y += applied_dy;
        new_dir_btn->x += applied_dx;
        new_dir_btn->y += applied_dy;
        delete_btn->x += applied_dx;
        delete_btn->y += applied_dy;
    } else if (kind == WIN_KIND_VIEWER) {
        viewer_co->x += applied_dx;
        viewer_co->y += applied_dy;
    }
    /* WIN_KIND_INFO has no content widgets to move. */
}

static void draw_window_group(const struct window *panel, const struct button *btn,
                              const struct button *exit_btn, int click_count, const struct textfield *tf,
                              const struct checkbox *cb) {
    char line[40];
    int pos;

    window_draw(panel);
    button_draw(btn);
    button_draw(exit_btn);

    pos = 0;
    str_append(line, &pos, (int)sizeof(line), "CLICKS: ");
    {
        char num[12];
        format_uint((unsigned int)click_count, num);
        str_append(line, &pos, (int)sizeof(line), num);
    }
    text_puts(btn->x, btn->y + btn->h + 16, line, TEXT_PRIMARY_COLOR, 1);

    text_puts(btn->x, btn->y + btn->h + 36, "TYPE:", TEXT_PRIMARY_COLOR, 1);
    textfield_draw(tf);

    checkbox_draw(cb);
}

/* The second window: no buttons, just enough content to prove it's a
 * real window (and a real drag target) rather than a decorative rect. */
static void draw_info_group(const struct window *info, const char *ata_status, const char *fs_status) {
    window_draw(info);
    text_puts(info->x + 8, info->y + 12, "DRAG ME TOO", TEXT_MUTED_COLOR, 1);
    text_puts(info->x + 8, info->y + 32, ata_status, TEXT_MUTED_COLOR, 1);
    text_puts(info->x + 8, info->y + 52, fs_status, TEXT_MUTED_COLOR, 1);
}

/* The third window: the Forth console. Stage A only -- an echo terminal,
 * no interpreter behind it yet (that's Stage B). */
static void draw_forth_group(const struct window *forth, const struct console_output *co,
                             const struct console_input *ci) {
    window_draw(forth);
    console_output_draw(co);
    console_input_draw(ci);
}

/* Room for FS_MAX_PATH_DEPTH (4) components at up to FS_NAME_MAX - 1 (15)
 * chars each, plus separating slashes and the terminator -- generous. */
#define FILES_PATH_MAX 80
#define FILES_ROW_HEIGHT 20
#define FILES_LIST_Y_OFFSET 12

/* Subtle green tint behind the row a right-click selected -- same value
 * as BUTTON_HOVER_COLOR (button.c) so a "highlighted" thing looks the
 * same everywhere in the UI, not a color invented just for this. */
#define FILES_SELECTED_BG_COLOR 0x123322

/* One sector's worth -- every current test file is far smaller. Read
 * calls use VIEWER_BUF_SIZE - 1 so there's always room for a manual nul
 * terminator, since fs_read_file() copies exactly out_size raw bytes and
 * doesn't add one itself. */
#define VIEWER_BUF_SIZE 512

/* Appends name onto cwd ("/" gets name appended directly, without a
 * doubled leading slash; anything else gets a separating '/' first).
 * cap is dst's real size (callers pass FILES_PATH_MAX, not a guess) --
 * cwd is only ever built by this same bounded machinery, but name comes
 * straight from an on-disk directory entry (fs_dirent.name), which fs.c
 * only guarantees fits FS_NAME_MAX, not that cwd+"/"+name fits in
 * FILES_PATH_MAX. A corrupted/crafted directory entry deep enough in the
 * tree must truncate here, not overflow dst. */
static void path_join(char *dst, int cap, const char *cwd, const char *name) {
    int pos = 0;
    if (!str_eq(cwd, "/")) {
        str_append(dst, &pos, cap, cwd);
    }
    str_append(dst, &pos, cap, "/");
    str_append(dst, &pos, cap, name);
}

/* In place: truncates cwd at its last '/', or resets to "/" if that was
 * the leading slash (going up from a top-level directory). */
static void path_parent(char *cwd) {
    int i;
    int last_slash = -1;
    for (i = 0; cwd[i]; i++) {
        if (cwd[i] == '/') {
            last_slash = i;
        }
    }
    if (last_slash <= 0) {
        cwd[0] = '/';
        cwd[1] = 0;
    } else {
        cwd[last_slash] = 0;
    }
}

/* The fourth window: a listing of the current directory (cwd), navigable
 * -- Stage B of the file manager, plus (Stage D) a right-click-selected
 * entry and a DELETE button, plus (Stage E) a name-entry field and a NEW
 * DIR button. Row 0 is always the path itself (not clickable); row 1 is
 * ".." if cwd isn't root; real entries follow. files_list_hit_test()
 * below mirrors this exact row numbering so the two can never disagree
 * about what a given pixel row means. files_selected is an index into
 * file_entries (FILES_HIT_NONE for "nothing selected") -- the row it
 * names, if any, is drawn with a highlight background. */
static void draw_files_group(const struct window *files, const char *cwd, const struct fs_dirent *file_entries,
                             unsigned int file_entry_count, int files_selected, const struct console_input *name_input,
                             const struct button *new_dir_btn, const struct button *delete_btn) {
    unsigned int i;
    int row = 1;

    window_draw(files);
    text_puts(files->x + 8, files->y + FILES_LIST_Y_OFFSET, cwd, TEXT_ACCENT_COLOR, 1);

    if (!str_eq(cwd, "/")) {
        text_puts(files->x + 8, files->y + FILES_LIST_Y_OFFSET + row * FILES_ROW_HEIGHT, "..", TEXT_PRIMARY_COLOR, 1);
        row++;
    }

    if (file_entry_count == 0) {
        text_puts(files->x + 8, files->y + FILES_LIST_Y_OFFSET + row * FILES_ROW_HEIGHT, "(EMPTY)", TEXT_MUTED_COLOR,
                  1);
    }

    for (i = 0; i < file_entry_count; i++) {
        char line[FS_NAME_MAX + 16];
        int pos = 0;
        int row_y = files->y + FILES_LIST_Y_OFFSET + row * FILES_ROW_HEIGHT;
        uint32_t text_color = TEXT_PRIMARY_COLOR;

        /* file_entry_count can be as large as FS_MAX_FILES (20), but the
         * window is only ever sized for a handful of visible rows. Stop
         * drawing once a row would run past name_input's top edge (the
         * start of the footer chrome, not the window's own bottom edge --
         * Stage E added a name field and button row below the list)
         * instead of walking gfx_fill_rect/text_puts over them -- rows
         * this far down are already unreachable by
         * files_list_hit_test()'s own py bounds check, so nothing here
         * needs to become clickable, just stop being drawn. */
        if (row_y + FILES_ROW_HEIGHT > name_input->y) {
            break;
        }

        str_append(line, &pos, (int)sizeof(line), file_entries[i].name);
        if (file_entries[i].type == FS_TYPE_DIR) {
            str_append(line, &pos, (int)sizeof(line), "/");
        } else {
            char num[12];
            str_append(line, &pos, (int)sizeof(line), " ");
            format_uint(file_entries[i].size_bytes, num);
            str_append(line, &pos, (int)sizeof(line), num);
            str_append(line, &pos, (int)sizeof(line), "B");
        }

        if ((int)i == files_selected) {
            gfx_fill_rect(files->x + 4, row_y - 2, files->w - 8, FILES_ROW_HEIGHT - 2, FILES_SELECTED_BG_COLOR);
            text_color = TEXT_ACCENT_COLOR;
        }

        text_puts(files->x + 8, row_y, line, text_color, 1);
        row++;
    }

    console_input_draw(name_input);
    button_draw(new_dir_btn);
    button_draw(delete_btn);
}

#define FILES_HIT_NONE (-1)
#define FILES_HIT_UP (-2)

/* Converts a click position into the same row numbering
 * draw_files_group() just drew -- FILES_HIT_NONE for the path header (row
 * 0) or outside the window, FILES_HIT_UP for "..", otherwise an index
 * into file_entries[]. */
static int files_list_hit_test(const struct window *files, const char *cwd, unsigned int file_entry_count, int px,
                               int py) {
    int up_present = !str_eq(cwd, "/");
    int rel_row;

    if (px < files->x || px >= files->x + files->w || py < files->y || py >= files->y + files->h) {
        return FILES_HIT_NONE;
    }

    rel_row = (py - (files->y + FILES_LIST_Y_OFFSET)) / FILES_ROW_HEIGHT;
    if (rel_row <= 0) {
        return FILES_HIT_NONE; /* the path header row, or above it */
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

/* The fifth window: a read-only view of whatever file was last opened
 * from the FILES window. Same console_output pane draw_forth_group()
 * already uses for the Forth console's scrollback, minus the input line
 * -- no new text-rendering code needed for a read-only pane. */
static void draw_viewer_group(const struct window *viewer, const struct console_output *viewer_co) {
    window_draw(viewer);
    console_output_draw(viewer_co);
}

/* The one place that dispatches "draw whatever's inside window index
 * idx" -- both draw_scene() and update_and_present() go through this
 * instead of each hand-rolling their own kind check. */
static void draw_window_by_index(int idx, const struct window *windows, const struct button *btn,
                                 const struct button *exit_btn, int click_count, const struct textfield *tf,
                                 const struct checkbox *cb, const struct console_output *co,
                                 const struct console_input *ci, const char *ata_status, const char *fs_status,
                                 const char *cwd, const struct fs_dirent *file_entries, unsigned int file_entry_count,
                                 int files_selected, const struct console_input *name_input,
                                 const struct button *new_dir_btn, const struct button *delete_btn,
                                 const struct console_output *viewer_co) {
    if (idx == WIN_KIND_PANEL) {
        draw_window_group(&windows[idx], btn, exit_btn, click_count, tf, cb);
    } else if (idx == WIN_KIND_FORTH) {
        draw_forth_group(&windows[idx], co, ci);
    } else if (idx == WIN_KIND_FILES) {
        draw_files_group(&windows[idx], cwd, file_entries, file_entry_count, files_selected, name_input, new_dir_btn,
                         delete_btn);
    } else if (idx == WIN_KIND_VIEWER) {
        draw_viewer_group(&windows[idx], viewer_co);
    } else {
        draw_info_group(&windows[idx], ata_status, fs_status);
    }
}

/* Full redraw of everything into the backbuffer: background, every open
 * window back-to-front in z-order, and the cursor. Only used for the
 * very first frame, where there's no prior state to diff against --
 * correct by full reconstruction, same reasoning the whole scene used to
 * be redrawn this way every frame before damage tracking (see
 * update_and_present() below). */
static void draw_scene(int w, int h, const struct window *windows, const struct button *btn,
                       const struct button *exit_btn, int click_count, const struct textfield *tf,
                       const struct checkbox *cb, const struct console_output *co, const struct console_input *ci,
                       const int *z_order, const struct taskbar *bar, int hovered_entry,
                       const struct desktop_icons *icons, int icon_hovered, int mx, int my, uint32_t cursor_color,
                       const char *ata_status, const char *fs_status, const char *cwd,
                       const struct fs_dirent *file_entries, unsigned int file_entry_count, int files_selected,
                       const struct console_input *name_input, const struct button *new_dir_btn,
                       const struct button *delete_btn, const struct console_output *viewer_co) {
    int x, y, i;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            gfx_put_pixel(x, y, backdrop_color(x, y, w, h, cb->checked));
        }
    }

    draw_title_subtitle(w);

    /* Icons are part of the desktop, underneath every window -- drawn
     * before the z-order loop so a window dragged over the icon column
     * correctly paints over it, same as a real desktop. */
    desktop_icons_draw(icons, windows, MAX_WINDOWS, icon_hovered);

    for (i = MAX_WINDOWS - 1; i >= 0; i--) {
        int idx = z_order[i];
        if (windows[idx].state == WINDOW_OPEN) {
            draw_window_by_index(idx, windows, btn, exit_btn, click_count, tf, cb, co, ci, ata_status, fs_status,
                                 cwd, file_entries, file_entry_count, files_selected, name_input, new_dir_btn,
                                 delete_btn, viewer_co);
        }
    }

    taskbar_draw(bar, windows, z_order, MAX_WINDOWS, hovered_entry);

    gfx_fill_rect(mx, my, CURSOR_SIZE, CURSOR_SIZE, cursor_color);
}

/* Region index constants for update_and_present()'s damage array: one
 * slot per window, plus the title block, the taskbar, and the desktop
 * icon column. */
#define TITLE_REGION (MAX_WINDOWS)
#define TASKBAR_REGION (MAX_WINDOWS + 1)
#define ICON_REGION (MAX_WINDOWS + 2)
#define DAMAGE_REGIONS (MAX_WINDOWS + 3)

/* Per-event redraw: repaints only a damage rectangle instead of the whole
 * screen, then presents just that rectangle (see the previous
 * damage-rect stage in docs/BUILD_LOG.md for why -- redrawing everything
 * per event is what let a real mouse's packet rate outrun the redraw
 * loop and overflow the ring buffer in an earlier stage). The damage rect
 * has to track DAMAGE_REGIONS independent regions -- each window, the
 * title block, and the taskbar -- rather than a fixed few named ones, now
 * that windows are a real array:
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
static void update_and_present(int w, int h, const struct window *windows, const struct button *btn,
                               const struct button *exit_btn, int click_count, const struct textfield *tf,
                               const struct checkbox *cb, const struct console_output *co,
                               const struct console_input *ci, const int *z_order, const int *old_z, int old_mx,
                               int old_my, int mx, int my, uint32_t cursor_color, const int *old_x, const int *old_y,
                               const int *touched, int fx_changed, const struct taskbar *bar, int hovered_entry,
                               int old_hovered_entry, const struct desktop_icons *icons, int icon_hovered,
                               int old_icon_hovered, const char *ata_status, const char *fs_status, const char *cwd,
                               const struct fs_dirent *file_entries, unsigned int file_entry_count,
                               int files_selected, const struct console_input *name_input,
                               const struct button *new_dir_btn, const struct button *delete_btn,
                               const struct console_output *viewer_co) {
    int dx0, dy0, dx1, dy1;
    int rx0[DAMAGE_REGIONS], ry0[DAMAGE_REGIONS], rx1[DAMAGE_REGIONS], ry1[DAMAGE_REGIONS];
    int redraw[DAMAGE_REGIONS];
    int changed;
    int i, x, y;
    int z_reordered = 0;
    int taskbar_touched;
    int icons_touched;

    for (i = 0; i < MAX_WINDOWS; i++) {
        if (z_order[i] != old_z[i]) {
            z_reordered = 1;
        }
    }

    /* fx_enabled changes what backdrop_color() returns everywhere on
     * screen, not just near the panel -- toggling FX has to repaint the
     * whole backdrop, so the damage rect starts at the full screen
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
    /* The icon column's appearance only depends on window state (closed
     * or not) and hover, not position/z-order -- but folding every
     * window's touched flag in too is the same harmless superset taskbar
     * touched above already accepts, not worth a finer-grained check for
     * a couple of small icons. */
    icons_touched = (icon_hovered != old_icon_hovered);
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (touched[i]) {
            taskbar_touched = 1;
            icons_touched = 1;
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
    rx0[ICON_REGION] = icons->x;
    ry0[ICON_REGION] = icons->y;
    rx1[ICON_REGION] = icons->x + icons->w;
    ry1[ICON_REGION] = icons->y + icons->h;
    redraw[ICON_REGION] = icons_touched;

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
    if (redraw[ICON_REGION]) {
        rect_union(&dx0, &dy0, &dx1, &dy1, rx0[ICON_REGION], ry0[ICON_REGION], rx1[ICON_REGION], ry1[ICON_REGION]);
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
            gfx_put_pixel(x, y, backdrop_color(x, y, w, h, cb->checked));
        }
    }

    if (redraw[TITLE_REGION]) {
        draw_title_subtitle(w);
    }

    if (redraw[ICON_REGION]) {
        desktop_icons_draw(icons, windows, MAX_WINDOWS, icon_hovered);
    }

    for (i = MAX_WINDOWS - 1; i >= 0; i--) {
        int idx = z_order[i];
        if (windows[idx].state == WINDOW_OPEN && redraw[idx]) {
            draw_window_by_index(idx, windows, btn, exit_btn, click_count, tf, cb, co, ci, ata_status, fs_status, cwd,
                                 file_entries, file_entry_count, files_selected, name_input, new_dir_btn, delete_btn,
                                 viewer_co);
        }
    }

    if (redraw[TASKBAR_REGION]) {
        taskbar_draw(bar, windows, z_order, MAX_WINDOWS, hovered_entry);
    }

    gfx_fill_rect(mx, my, CURSOR_SIZE, CURSOR_SIZE, cursor_color);

    gfx_present_rect(dx0, dy0, dx1 - dx0, dy1 - dy0);
}

void kmain(void) {
    int w, h;
    int mx, my;
    int click_count = 0;
    int prev_left_held = 0;
    int prev_right_held = 0;
    int dragging_window = -1;
    int taskbar_hovered = -1;
    int icon_hovered = -1;
    uint32_t cursor_color = CURSOR_IDLE_COLOR;
    struct textfield tf;
    struct checkbox cb;
    struct console_output co;
    struct console_input ci;
    struct console_output viewer_co;
    struct console_history hist;
    struct forth_vm vm;
    struct window windows[MAX_WINDOWS];
    int z_order[MAX_WINDOWS];
    struct taskbar bar;
    struct desktop_icons icons;
    struct button btn;
    struct button exit_btn;
    struct button delete_btn;
    struct button new_dir_btn;
    struct console_input name_input;
    const char *ata_status;
    const char *fs_status;
    char cwd[FILES_PATH_MAX];
    struct fs_dirent file_entries[FS_MAX_FILES];
    unsigned int file_entry_count;
    int files_selected = FILES_HIT_NONE;

    gfx_init();
    w = gfx_width();
    h = gfx_height();

    windows[WIN_KIND_PANEL].x = 140;
    windows[WIN_KIND_PANEL].y = 160;
    windows[WIN_KIND_PANEL].w = 360;
    windows[WIN_KIND_PANEL].h = 200;
    windows[WIN_KIND_PANEL].title = "RAVE-OS PANEL";
    windows[WIN_KIND_PANEL].state = WINDOW_OPEN;
    windows[WIN_KIND_PANEL].minimize_hovered = 0;
    windows[WIN_KIND_PANEL].close_hovered = 0;

    btn.x = windows[WIN_KIND_PANEL].x + 20;
    btn.y = windows[WIN_KIND_PANEL].y + 20;
    btn.w = 140;
    btn.h = 30;
    btn.label = "CLICK ME";
    btn.hovered = 0;
    btn.pressed = 0;

    exit_btn.x = btn.x + btn.w + 20;
    exit_btn.y = btn.y;
    exit_btn.w = windows[WIN_KIND_PANEL].x + windows[WIN_KIND_PANEL].w - 20 - exit_btn.x;
    exit_btn.h = 30;
    exit_btn.label = "EXIT";
    exit_btn.hovered = 0;
    exit_btn.pressed = 0;

    /* Positioned to overlap the panel's bottom-right corner from the
     * start, so the two windows' stacking order is visibly meaningful
     * the moment this boots, not just after someone drags one on top of
     * the other. */
    windows[WIN_KIND_INFO].x = 380;
    windows[WIN_KIND_INFO].y = 300;
    windows[WIN_KIND_INFO].w = 200;
    windows[WIN_KIND_INFO].h = 90;
    windows[WIN_KIND_INFO].title = "RAVE-OS INFO";
    windows[WIN_KIND_INFO].state = WINDOW_OPEN;
    windows[WIN_KIND_INFO].minimize_hovered = 0;
    windows[WIN_KIND_INFO].close_hovered = 0;

    /* Sized/positioned clear of the taskbar strip below it; overlapping
     * the other two windows' corners is fine (expected, even -- INFO
     * already overlaps PANEL by the same design choice). */
    windows[WIN_KIND_FORTH].x = 170;
    windows[WIN_KIND_FORTH].y = 230;
    windows[WIN_KIND_FORTH].w = 400;
    windows[WIN_KIND_FORTH].h = 180;
    windows[WIN_KIND_FORTH].title = "RAVE-OS FORTH";
    windows[WIN_KIND_FORTH].state = WINDOW_OPEN;
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
    windows[WIN_KIND_FILES].h = 250;
    windows[WIN_KIND_FILES].title = "RAVE-OS FILES";
    windows[WIN_KIND_FILES].state = WINDOW_OPEN;
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
    new_dir_btn.y = windows[WIN_KIND_FILES].y + windows[WIN_KIND_FILES].h - 30;
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

    /* Name-entry field for both NEW DIR and Enter-creates-file, sitting
     * just above the button row. console_input, not textfield -- the
     * panel's textfield clears itself on Enter (fine for a demo widget
     * with nowhere to submit to), which would silently eat the typed
     * name before this code ever saw it. */
    name_input.x = windows[WIN_KIND_FILES].x + 8;
    name_input.y = new_dir_btn.y - 20 - 6;
    name_input.w = windows[WIN_KIND_FILES].w - 16;
    name_input.h = 20;
    name_input.text[0] = 0;
    name_input.len = 0;
    name_input.cursor = 0;
    name_input.focused = 0;

    /* Wide and short, suited to a handful of text lines rather than a
     * long listing -- sits below the other windows, clear of the taskbar. */
    windows[WIN_KIND_VIEWER].x = 220;
    windows[WIN_KIND_VIEWER].y = 330;
    windows[WIN_KIND_VIEWER].w = 340;
    windows[WIN_KIND_VIEWER].h = 110;
    windows[WIN_KIND_VIEWER].title = "RAVE-OS VIEWER";
    windows[WIN_KIND_VIEWER].state = WINDOW_OPEN;
    windows[WIN_KIND_VIEWER].minimize_hovered = 0;
    windows[WIN_KIND_VIEWER].close_hovered = 0;
    console_output_init(&viewer_co, windows[WIN_KIND_VIEWER].x + 4, windows[WIN_KIND_VIEWER].y + 6,
                        windows[WIN_KIND_VIEWER].w - 8, windows[WIN_KIND_VIEWER].h - 12);
    console_output_append_line(&viewer_co, "(NO FILE OPEN)");

    /* Panel starts frontmost, matching the old panel_on_top = 1 default. */
    z_order[0] = WIN_KIND_PANEL;
    z_order[1] = WIN_KIND_INFO;
    z_order[2] = WIN_KIND_FORTH;
    z_order[3] = WIN_KIND_FILES;
    z_order[4] = WIN_KIND_VIEWER;

    bar.x = 0;
    bar.y = h - TASKBAR_HEIGHT;
    bar.w = w;
    bar.h = TASKBAR_HEIGHT;

    /* Column sits entirely left of both windows' default x (140), clear
     * of the centered title/subtitle text above it -- so on first boot
     * nothing overlaps it at all; a window dragged over it later is
     * expected to occlude it, same as any real desktop. */
    desktop_icons_init(&icons, DESKTOP_ICON_MARGIN, SUBTITLE_Y + GLYPH_HEIGHT * SUBTITLE_SCALE + 16, MAX_WINDOWS);

    /* Sits to the right of the "TYPE:" label drawn by draw_window_group(),
     * on the same baseline (see textfield_draw()'s vertical-centering math
     * for why tf.y is offset by -3), extending to the same right margin
     * exit_btn already uses. */
    {
        int type_line_y = btn.y + btn.h + 36;
        tf.x = btn.x + text_width("TYPE:", 1) + 6;
        tf.y = type_line_y - 3;
        tf.w = windows[WIN_KIND_PANEL].x + windows[WIN_KIND_PANEL].w - 20 - tf.x;
        tf.h = 14;
    }
    tf.text[0] = 0;
    tf.len = 0;
    tf.focused = 0;

    /* Below the field, same left margin as the buttons above it. */
    cb.x = btn.x;
    cb.y = tf.y + tf.h + 14;
    cb.size = 14;
    cb.label = "FX";
    cb.checked = 0;
    cb.hovered = 0;

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

    mx = w / 2;
    my = h - 100; /* start clear of the panels above */

    /* IDT/PIC set up first (masked, no sti yet), then the mouse's polling
     * handshake runs with IRQ12 still masked so it can't race the new
     * interrupt handler for the same bytes, then interrupts are actually
     * enabled once both are ready. */
    interrupts_init();
    mouse_init();
    interrupts_enable();

    /* No IRQ14 involved (see ata.h) -- this is a synchronous polling call,
     * safe to run any time after interrupts_enable(), not tied to the
     * masked-PIC ordering the line above exists for. */
    ata_status = ata_selftest();
    fs_status = fs_selftest();

    /* Read once here (and again only when a navigation click actually
     * changes cwd, below), not on every redraw -- this kernel's event loop
     * redraws on essentially any mouse movement, and re-reading the
     * directory sector that often would mean a PIO polling round-trip on
     * nearly every frame once this window exists. */
    cwd[0] = '/';
    cwd[1] = 0;
    if (fs_list_dir(cwd, file_entries, FS_MAX_FILES, &file_entry_count) != 0) {
        file_entry_count = 0;
    }

    draw_scene(w, h, windows, &btn, &exit_btn, click_count, &tf, &cb, &co, &ci, z_order, &bar, taskbar_hovered,
              &icons, icon_hovered, mx, my, cursor_color, ata_status, fs_status, cwd, file_entries, file_entry_count,
              files_selected, &name_input, &new_dir_btn, &delete_btn, &viewer_co);
    gfx_present();

    for (;;) {
        int had_event = 0;
        int dx, dy, buttons;
        char c;
        int old_mx = mx;
        int old_my = my;
        int old_x[MAX_WINDOWS], old_y[MAX_WINDOWS], old_z[MAX_WINDOWS];
        int old_state[MAX_WINDOWS], old_min_hov[MAX_WINDOWS], old_close_hov[MAX_WINDOWS];
        int old_btn_hovered = btn.hovered;
        int old_btn_pressed = btn.pressed;
        int old_exit_hovered = exit_btn.hovered;
        int old_exit_pressed = exit_btn.pressed;
        int old_click_count = click_count;
        int old_tf_len = tf.len;
        int old_tf_focused = tf.focused;
        int old_cb_checked = cb.checked;
        int old_cb_hovered = cb.hovered;
        int old_taskbar_hovered = taskbar_hovered;
        int old_icon_hovered = icon_hovered;
        int old_co_generation = co.generation;
        int old_viewer_co_generation = viewer_co.generation;
        int old_ci_len = ci.len;
        int old_ci_cursor = ci.cursor;
        int old_ci_focused = ci.focused;
        char old_ci_text[CONSOLE_INPUT_MAX + 1];
        char old_cwd[FILES_PATH_MAX];
        int old_files_selected = files_selected;
        int old_delete_btn_hovered = delete_btn.hovered;
        int old_delete_btn_pressed = delete_btn.pressed;
        int old_new_dir_btn_hovered = new_dir_btn.hovered;
        int old_new_dir_btn_pressed = new_dir_btn.pressed;
        int old_name_input_len = name_input.len;
        int old_name_input_cursor = name_input.cursor;
        int old_name_input_focused = name_input.focused;
        char old_name_input_text[CONSOLE_INPUT_MAX + 1];
        int i;

        for (i = 0; ci.text[i]; i++) {
            old_ci_text[i] = ci.text[i];
        }
        old_ci_text[i] = 0;

        for (i = 0; name_input.text[i]; i++) {
            old_name_input_text[i] = name_input.text[i];
        }
        old_name_input_text[i] = 0;

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
             * motion, the same as any desktop. */
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
                    move_window_content(dragging_window, &btn, &exit_btn, &tf, &cb, &co, &ci, &name_input,
                                        &new_dir_btn, &delete_btn, &viewer_co, applied_dx, applied_dy);
                } else {
                    dragging_window = -1;
                }
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
            } else if (left_held && !prev_left_held && desktop_icon_hit(&icons, windows, MAX_WINDOWS, cx, cy) >= 0) {
                /* Same reasoning as the taskbar branch above -- icons
                 * aren't part of the window stack, so they're checked
                 * before generic window hit-testing. A closed window's
                 * only icon click behavior is reopening it: there's no
                 * "already active" case to toggle, unlike the taskbar,
                 * since a closed window is never the active one. */
                int icon = desktop_icon_hit(&icons, windows, MAX_WINDOWS, cx, cy);

                windows[icon].state = WINDOW_OPEN;
                raise_window(z_order, icon);
            } else if (left_held && !prev_left_held) {
                int target = topmost_window_at(windows, z_order, cx, cy);

                if (target >= 0) {
                    /* Controls take priority over raising/dragging -- a
                     * click on close or minimize acts immediately rather
                     * than raising the window to front first, same as any
                     * real desktop. */
                    if (window_close_hit_test(&windows[target], cx, cy)) {
                        windows[target].state = WINDOW_CLOSED;
                    } else if (window_minimize_hit_test(&windows[target], cx, cy)) {
                        windows[target].state = WINDOW_MINIMIZED;
                    } else {
                        raise_window(z_order, target);
                        if (window_titlebar_hit_test(&windows[target], cx, cy)) {
                            dragging_window = target;
                        }
                    }
                }
            }

            /* Hover-highlight whichever window's controls are actually
             * under the cursor -- only the topmost window at that point,
             * so an occluded window's controls never light up. Runs every
             * packet (not just click edges), same as button/checkbox
             * hover elsewhere. */
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
            icon_hovered = desktop_icon_hit(&icons, windows, MAX_WINDOWS, cx, cy);

            /* The panel's buttons only respond if the panel is actually
             * the topmost thing under the cursor -- otherwise a click on
             * a point where the info window covers the panel would click
             * straight through to a button the user can't even see. */
            {
                int topmost = topmost_window_at(windows, z_order, cx, cy);
                int panel_is_topmost = topmost == WIN_KIND_PANEL;
                int forth_is_topmost = topmost == WIN_KIND_FORTH;
                int files_is_topmost = topmost == WIN_KIND_FILES;
                int click_edge = left_held && !prev_left_held;

                btn.hovered = panel_is_topmost && button_hit_test(&btn, cx, cy);
                if (btn.hovered && click_edge) {
                    click_count++;
                }
                btn.pressed = btn.hovered && left_held;

                exit_btn.hovered = panel_is_topmost && button_hit_test(&exit_btn, cx, cy);
                if (exit_btn.hovered && click_edge) {
                    power_shutdown();
                }
                exit_btn.pressed = exit_btn.hovered && left_held;

                /* Any click edge sets focus: hitting the field itself
                 * focuses it, anything else (a button, empty panel space,
                 * another window, the backdrop) defocuses it -- same
                 * single-focus-owner behavior as clicking around a normal
                 * desktop text field. */
                if (click_edge) {
                    tf.focused = panel_is_topmost && textfield_hit_test(&tf, cx, cy);
                    ci.focused = forth_is_topmost && console_input_hit_test(&ci, cx, cy);
                    name_input.focused = files_is_topmost && console_input_hit_test(&name_input, cx, cy);
                }

                cb.hovered = panel_is_topmost && checkbox_hit_test(&cb, cx, cy);
                if (cb.hovered && click_edge) {
                    cb.checked = !cb.checked;
                }

                /* Clicking ".." or a directory row navigates and re-lists
                 * immediately (see the boot-time fs_list_dir() call for
                 * why this stays an explicit "only when cwd actually
                 * changes" call rather than something re-run every
                 * redraw). Clicking a file reads it into the viewer and
                 * raises that window so the result is immediately
                 * visible. */
                if (files_is_topmost && click_edge) {
                    int hit = files_list_hit_test(&windows[WIN_KIND_FILES], cwd, file_entry_count, cx, cy);

                    if (hit == FILES_HIT_UP) {
                        path_parent(cwd);
                        files_selected = FILES_HIT_NONE; /* stale relative to the new listing */
                        if (fs_list_dir(cwd, file_entries, FS_MAX_FILES, &file_entry_count) != 0) {
                            file_entry_count = 0;
                        }
                    } else if (hit >= 0 && file_entries[hit].type == FS_TYPE_DIR) {
                        char new_cwd[FILES_PATH_MAX];
                        path_join(new_cwd, (int)sizeof(new_cwd), cwd, file_entries[hit].name);
                        {
                            int ci2;
                            for (ci2 = 0; new_cwd[ci2] && ci2 < (int)sizeof(cwd) - 1; ci2++) {
                                cwd[ci2] = new_cwd[ci2];
                            }
                            cwd[ci2] = 0;
                        }
                        files_selected = FILES_HIT_NONE; /* stale relative to the new listing */
                        if (fs_list_dir(cwd, file_entries, FS_MAX_FILES, &file_entry_count) != 0) {
                            file_entry_count = 0;
                        }
                    } else if (hit >= 0 && file_entries[hit].type == FS_TYPE_FILE) {
                        char file_path[FILES_PATH_MAX];
                        char buf[VIEWER_BUF_SIZE];
                        unsigned int out_size;

                        path_join(file_path, (int)sizeof(file_path), cwd, file_entries[hit].name);
                        console_output_clear(&viewer_co);

                        if (fs_read_file(file_path, buf, VIEWER_BUF_SIZE - 1, &out_size) != 0) {
                            console_output_append_line(&viewer_co, "(READ FAILED)");
                        } else {
                            /* Same '\n'-splitting idiom already used below
                             * for forth_eval_line()'s output -- fs_read_file()
                             * doesn't nul-terminate (it copies exactly
                             * out_size raw bytes), so that's done here first,
                             * guaranteed to fit within VIEWER_BUF_SIZE. */
                            int oi = 0, line_start = 0;

                            buf[out_size] = 0;
                            while (buf[oi]) {
                                if (buf[oi] == '\n') {
                                    buf[oi] = 0;
                                    console_output_append_line(&viewer_co, &buf[line_start]);
                                    line_start = oi + 1;
                                }
                                oi++;
                            }
                            if (line_start < oi) {
                                console_output_append_line(&viewer_co, &buf[line_start]);
                            }
                        }

                        raise_window(z_order, WIN_KIND_VIEWER);
                    }
                }

                /* Right-click selects a row for deletion without
                 * triggering the left-click navigate/open behavior above
                 * -- the mouse driver already decodes this bit
                 * (mouse.h's buttons bitmask), just unused until now.
                 * Right-clicking the already-selected row deselects it. */
                if (files_is_topmost && right_held && !prev_right_held) {
                    int hit = files_list_hit_test(&windows[WIN_KIND_FILES], cwd, file_entry_count, cx, cy);
                    if (hit >= 0) {
                        files_selected = (files_selected == hit) ? FILES_HIT_NONE : hit;
                    }
                }

                /* Deletes whatever files_selected names. A non-empty
                 * directory or an already-stale selection just makes
                 * fs_delete() fail, left as a silent no-op -- no
                 * error-message UI in the FILES window yet, same
                 * deferred-for-now choice as this window's other no-ops
                 * (e.g. clicking the header row). */
                delete_btn.hovered = files_is_topmost && button_hit_test(&delete_btn, cx, cy);
                if (delete_btn.hovered && click_edge && files_selected >= 0) {
                    char del_path[FILES_PATH_MAX];
                    path_join(del_path, (int)sizeof(del_path), cwd, file_entries[files_selected].name);
                    if (fs_delete(del_path) == 0) {
                        files_selected = FILES_HIT_NONE;
                        if (fs_list_dir(cwd, file_entries, FS_MAX_FILES, &file_entry_count) != 0) {
                            file_entry_count = 0;
                        }
                    }
                }
                delete_btn.pressed = delete_btn.hovered && left_held;

                /* Creates name_input's typed name as a directory in cwd.
                 * An empty name or an already-existing/full-table failure
                 * from fs_create_dir() is a silent no-op, same as DELETE
                 * above -- but unlike a successful delete (which clears
                 * files_selected), a failed create leaves the typed name
                 * in place so the user can see and fix it. Creating a
                 * plain file has no button of its own -- see Enter
                 * handling in the keyboard block below. */
                new_dir_btn.hovered = files_is_topmost && button_hit_test(&new_dir_btn, cx, cy);
                if (new_dir_btn.hovered && click_edge && name_input.text[0] != 0) {
                    char new_path[FILES_PATH_MAX];
                    path_join(new_path, (int)sizeof(new_path), cwd, name_input.text);
                    if (fs_create_dir(new_path) == 0) {
                        console_input_clear(&name_input);
                        if (fs_list_dir(cwd, file_entries, FS_MAX_FILES, &file_entry_count) != 0) {
                            file_entry_count = 0;
                        }
                    }
                }
                new_dir_btn.pressed = new_dir_btn.hovered && left_held;
            }

            prev_left_held = left_held;
            prev_right_held = right_held;
            cursor_color = left_held ? CURSOR_CLICK_COLOR : CURSOR_IDLE_COLOR;

            had_event = 1;
        }

        if (keyboard_poll_char(&c)) {
            if (tf.focused) {
                textfield_feed_char(&tf, c);
            } else if (ci.focused && c == KEY_UP) {
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
                    char out[128];
                    int pos = 0, oi = 0, line_start = 0;

                    str_append(echoed, &pos, (int)sizeof(echoed), "> ");
                    str_append(echoed, &pos, (int)sizeof(echoed), ci.text);
                    console_output_append_line(&co, echoed);

                    /* forth_eval_line() never touches console_output.h
                     * itself (see forth.h) -- it writes '\n'-separated
                     * output into out[], and splitting that into
                     * separate console lines is kernel.c's job, done
                     * here rather than inside forth.c. */
                    forth_eval_line(&vm, ci.text, out, sizeof(out));
                    while (out[oi]) {
                        if (out[oi] == '\n') {
                            out[oi] = 0;
                            console_output_append_line(&co, &out[line_start]);
                            line_start = oi + 1;
                        }
                        oi++;
                    }
                    if (line_start < oi) {
                        console_output_append_line(&co, &out[line_start]);
                    }

                    console_history_push(&hist, ci.text);
                    console_input_clear(&ci);
                }
            } else if (name_input.focused && c == KEY_LEFT) {
                console_input_move_cursor(&name_input, -1);
            } else if (name_input.focused && c == KEY_RIGHT) {
                console_input_move_cursor(&name_input, 1);
            } else if (name_input.focused) {
                /* Enter creates a plain file with the typed name -- NEW
                 * DIR (the click handler above) is the only way to
                 * create a directory, so Enter unambiguously means
                 * "file" here, the same Enter-submits convention the
                 * Forth console already uses. An empty name, or an
                 * already-existing/full-table failure from
                 * fs_create_file(), is a silent no-op that leaves the
                 * typed name in place, same as NEW DIR's failure case. */
                if (console_input_feed_char(&name_input, c) && name_input.text[0] != 0) {
                    char new_path[FILES_PATH_MAX];
                    path_join(new_path, (int)sizeof(new_path), cwd, name_input.text);
                    if (fs_create_file(new_path, 0, 0) == 0) {
                        console_input_clear(&name_input);
                        if (fs_list_dir(cwd, file_entries, FS_MAX_FILES, &file_entry_count) != 0) {
                            file_entry_count = 0;
                        }
                    }
                }
            }
            had_event = 1;
        }

        if (had_event) {
            int touched[MAX_WINDOWS];

            /* Position, open/minimized/closed state, and control hover are
             * generic to every window regardless of content; click_count,
             * the widgets' hover/pressed/focus/text state are specific to
             * the panel's content, folded in on top. */
            for (i = 0; i < MAX_WINDOWS; i++) {
                touched[i] = (windows[i].x != old_x[i]) || (windows[i].y != old_y[i]) ||
                             (windows[i].state != old_state[i]) ||
                             (windows[i].minimize_hovered != old_min_hov[i]) ||
                             (windows[i].close_hovered != old_close_hov[i]);
            }
            touched[WIN_KIND_PANEL] = touched[WIN_KIND_PANEL] || (btn.hovered != old_btn_hovered) ||
                                      (btn.pressed != old_btn_pressed) || (exit_btn.hovered != old_exit_hovered) ||
                                      (exit_btn.pressed != old_exit_pressed) || (click_count != old_click_count) ||
                                      (tf.len != old_tf_len) || (tf.focused != old_tf_focused) ||
                                      (cb.checked != old_cb_checked) || (cb.hovered != old_cb_hovered);
            touched[WIN_KIND_FORTH] = touched[WIN_KIND_FORTH] || (co.generation != old_co_generation) ||
                                      (ci.len != old_ci_len) || (ci.cursor != old_ci_cursor) ||
                                      (ci.focused != old_ci_focused) || !str_eq(ci.text, old_ci_text);
            touched[WIN_KIND_FILES] = touched[WIN_KIND_FILES] || !str_eq(cwd, old_cwd) ||
                                      (files_selected != old_files_selected) ||
                                      (delete_btn.hovered != old_delete_btn_hovered) ||
                                      (delete_btn.pressed != old_delete_btn_pressed) ||
                                      (new_dir_btn.hovered != old_new_dir_btn_hovered) ||
                                      (new_dir_btn.pressed != old_new_dir_btn_pressed) ||
                                      (name_input.len != old_name_input_len) ||
                                      (name_input.cursor != old_name_input_cursor) ||
                                      (name_input.focused != old_name_input_focused) ||
                                      !str_eq(name_input.text, old_name_input_text);
            touched[WIN_KIND_VIEWER] = touched[WIN_KIND_VIEWER] || (viewer_co.generation != old_viewer_co_generation);

            update_and_present(w, h, windows, &btn, &exit_btn, click_count, &tf, &cb, &co, &ci, z_order, old_z,
                               old_mx, old_my, mx, my, cursor_color, old_x, old_y, touched,
                               cb.checked != old_cb_checked, &bar, taskbar_hovered, old_taskbar_hovered, &icons,
                               icon_hovered, old_icon_hovered, ata_status, fs_status, cwd, file_entries,
                               file_entry_count, files_selected, &name_input, &new_dir_btn, &delete_btn, &viewer_co);
        } else {
            __asm__ volatile("hlt");
        }
    }
}
