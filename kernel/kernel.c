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
#include "io.h"

/* Note: stage2 switches the display into a VBE graphics mode before the
 * kernel even starts, so raw VGA text-mode writes at 0xB8000 don't apply
 * here -- text.c (built on font.c, ported from ACIDSTORM) is the real
 * text output path now. */

#define CURSOR_SIZE 8
#define TYPED_MAX 24

/* Exactly two windows exist right now (the interactive panel and a small
 * static info window), so a fixed pair of named identities is simpler
 * than a generic N-window array/list -- same reasoning already used for
 * having named `btn`/`exit_btn` structs instead of an array of buttons.
 * If a third window is ever added, that's the point to generalize. */
#define WIN_PANEL 0
#define WIN_INFO 1

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

static uint32_t backdrop_color(int x, int y, int w, int h) {
    int gx = w * 3 / 10;
    int gy = h / 8;
    int dx = x - gx;
    int dy = y - gy;
    int dist2 = dx * dx + dy * dy;
    int radius = w * 3 / 5;
    int radius2 = radius * radius;
    int g = BACKDROP_G;
    int b = BACKDROP_B;

    if (dist2 < radius2) {
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

static void str_append(char *dst, int *pos, const char *src) {
    while (*src) {
        dst[(*pos)++] = *src++;
    }
    dst[*pos] = 0;
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

/* Which window (if either) is visually on top at a screen point, given
 * the current stacking order -- what hit-testing (drag-start, button
 * clicks, raise-on-click) needs to check before acting, so a click on a
 * point where windows overlap only ever affects whichever one is
 * actually visible there. Returns WIN_PANEL, WIN_INFO, or -1. */
static int topmost_window_at(int px, int py, int panel_on_top, const struct window *panel,
                             const struct window *info) {
    if (panel_on_top) {
        if (window_outer_contains(panel, px, py)) {
            return WIN_PANEL;
        }
        if (window_outer_contains(info, px, py)) {
            return WIN_INFO;
        }
    } else {
        if (window_outer_contains(info, px, py)) {
            return WIN_INFO;
        }
        if (window_outer_contains(panel, px, py)) {
            return WIN_PANEL;
        }
    }
    return -1;
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

static void draw_window_group(const struct window *panel, const struct button *btn,
                              const struct button *exit_btn, int click_count, const char *typed) {
    char line[40];
    int pos;

    window_draw(panel);
    button_draw(btn);
    button_draw(exit_btn);

    pos = 0;
    str_append(line, &pos, "CLICKS: ");
    {
        char num[12];
        format_uint((unsigned int)click_count, num);
        str_append(line, &pos, num);
    }
    text_puts(btn->x, btn->y + btn->h + 16, line, TEXT_PRIMARY_COLOR, 1);

    pos = 0;
    str_append(line, &pos, "TYPE: ");
    str_append(line, &pos, typed);
    text_puts(btn->x, btn->y + btn->h + 36, line, TEXT_PRIMARY_COLOR, 1);
}

/* The second window: no buttons, just enough content to prove it's a
 * real window (and a real drag target) rather than a decorative rect. */
static void draw_info_group(const struct window *info) {
    window_draw(info);
    text_puts(info->x + 8, info->y + 12, "DRAG ME TOO", TEXT_MUTED_COLOR, 1);
}

/* Full redraw of everything into the backbuffer: background, both
 * windows in the given stacking order, and the cursor. Only used for the
 * very first frame, where there's no prior state to diff against --
 * correct by full reconstruction, same reasoning the whole scene used to
 * be redrawn this way every frame before damage tracking (see
 * update_and_present() below). */
static void draw_scene(int w, int h, const struct window *panel, const struct button *btn,
                       const struct button *exit_btn, int click_count, const char *typed,
                       const struct window *info, int panel_on_top, int mx, int my, uint32_t cursor_color) {
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            gfx_put_pixel(x, y, backdrop_color(x, y, w, h));
        }
    }

    draw_title_subtitle(w);

    if (panel_on_top) {
        draw_info_group(info);
        draw_window_group(panel, btn, exit_btn, click_count, typed);
    } else {
        draw_window_group(panel, btn, exit_btn, click_count, typed);
        draw_info_group(info);
    }

    gfx_fill_rect(mx, my, CURSOR_SIZE, CURSOR_SIZE, cursor_color);
}

/* Per-event redraw: repaints only a damage rectangle instead of the whole
 * screen, then presents just that rectangle (see the previous
 * damage-rect stage in docs/BUILD_LOG.md for why -- redrawing everything
 * per event is what let a real mouse's packet rate outrun the redraw
 * loop and overflow the ring buffer in an earlier stage). With two
 * windows now able to overlap and reorder, the damage rect has to track
 * three independent regions -- the panel, the info window, and the
 * title block -- rather than just one:
 *
 * - It starts as the union of the cursor's old and new position, since
 *   the cursor moves on nearly every event.
 * - A region that moved, changed contents, or was involved in a z-order
 *   swap this event is unconditionally redrawn, with its old position
 *   (if it moved) folded into the damage too, so what's left behind at
 *   the old spot gets properly repainted.
 * - After that seed, a fixed-point loop catches everything else: any
 *   region not already marked dirty that the accumulated damage happens
 *   to overlap also needs a redraw -- otherwise repainting the backdrop
 *   (or a window drawn on top of it) over that region would erase pixels
 *   that nothing ever restores. This is what correctly handles the
 *   cursor sweeping across a window it didn't otherwise touch, and a
 *   window that moved away revealing whatever (window or backdrop) was
 *   underneath it. Three regions total, so this converges in at most
 *   three passes.
 *
 * Whatever ends up dirty is still redrawn as a whole reconstructed unit,
 * not diffed pixel-by-pixel -- same approach the very first whole-screen
 * redraw used, just scoped down to whatever actually needs it, and
 * painted back-to-front in the current stacking order so the topmost
 * window correctly wins wherever the two overlap. */
static void update_and_present(int w, int h, const struct window *panel, const struct button *btn,
                               const struct button *exit_btn, int click_count, const char *typed,
                               const struct window *info, int panel_on_top, int old_mx, int old_my, int mx, int my,
                               uint32_t cursor_color, int old_panel_x, int old_panel_y, int old_info_x,
                               int old_info_y, int panel_touched, int info_touched, int z_reordered) {
    int dx0, dy0, dx1, dy1;
    int px0, py0, px1, py1;
    int ix0, iy0, ix1, iy1;
    int tx0, ty0, tx1, ty1;
    int redraw_panel, redraw_info, redraw_titles;
    int changed;
    int x, y;

    dx0 = old_mx < mx ? old_mx : mx;
    dy0 = old_my < my ? old_my : my;
    dx1 = (old_mx > mx ? old_mx : mx) + CURSOR_SIZE;
    dy1 = (old_my > my ? old_my : my) + CURSOR_SIZE;

    window_outer_rect(panel, &px0, &py0, &px1, &py1);
    window_outer_rect(info, &ix0, &iy0, &ix1, &iy1);
    title_block_rect(w, &tx0, &ty0, &tx1, &ty1);

    redraw_panel = panel_touched || z_reordered;
    redraw_info = info_touched || z_reordered;
    redraw_titles = 0;

    if (panel_touched) {
        struct window old_win = *panel;
        int ox0, oy0, ox1, oy1;

        old_win.x = old_panel_x;
        old_win.y = old_panel_y;
        window_outer_rect(&old_win, &ox0, &oy0, &ox1, &oy1);
        rect_union(&dx0, &dy0, &dx1, &dy1, ox0, oy0, ox1, oy1);
    }
    if (info_touched) {
        struct window old_win = *info;
        int ox0, oy0, ox1, oy1;

        old_win.x = old_info_x;
        old_win.y = old_info_y;
        window_outer_rect(&old_win, &ox0, &oy0, &ox1, &oy1);
        rect_union(&dx0, &dy0, &dx1, &dy1, ox0, oy0, ox1, oy1);
    }
    if (redraw_panel) {
        rect_union(&dx0, &dy0, &dx1, &dy1, px0, py0, px1, py1);
    }
    if (redraw_info) {
        rect_union(&dx0, &dy0, &dx1, &dy1, ix0, iy0, ix1, iy1);
    }

    do {
        changed = 0;
        if (!redraw_panel && rects_overlap(dx0, dy0, dx1, dy1, px0, py0, px1, py1)) {
            redraw_panel = 1;
            rect_union(&dx0, &dy0, &dx1, &dy1, px0, py0, px1, py1);
            changed = 1;
        }
        if (!redraw_info && rects_overlap(dx0, dy0, dx1, dy1, ix0, iy0, ix1, iy1)) {
            redraw_info = 1;
            rect_union(&dx0, &dy0, &dx1, &dy1, ix0, iy0, ix1, iy1);
            changed = 1;
        }
        if (!redraw_titles && rects_overlap(dx0, dy0, dx1, dy1, tx0, ty0, tx1, ty1)) {
            redraw_titles = 1;
            rect_union(&dx0, &dy0, &dx1, &dy1, tx0, ty0, tx1, ty1);
            changed = 1;
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
            gfx_put_pixel(x, y, backdrop_color(x, y, w, h));
        }
    }

    if (redraw_titles) {
        draw_title_subtitle(w);
    }

    if (panel_on_top) {
        if (redraw_info) {
            draw_info_group(info);
        }
        if (redraw_panel) {
            draw_window_group(panel, btn, exit_btn, click_count, typed);
        }
    } else {
        if (redraw_panel) {
            draw_window_group(panel, btn, exit_btn, click_count, typed);
        }
        if (redraw_info) {
            draw_info_group(info);
        }
    }

    gfx_fill_rect(mx, my, CURSOR_SIZE, CURSOR_SIZE, cursor_color);

    gfx_present_rect(dx0, dy0, dx1 - dx0, dy1 - dy0);
}

void kmain(void) {
    int w, h;
    int mx, my;
    int click_count = 0;
    int prev_left_held = 0;
    int dragging_window = -1;
    int panel_on_top = 1;
    uint32_t cursor_color = CURSOR_IDLE_COLOR;
    char typed[TYPED_MAX + 1];
    int typed_len = 0;
    struct window panel;
    struct window info_panel;
    struct button btn;
    struct button exit_btn;

    gfx_init();
    w = gfx_width();
    h = gfx_height();

    panel.x = 140;
    panel.y = 160;
    panel.w = 360;
    panel.h = 200;
    panel.title = "RAVE-OS PANEL";

    btn.x = panel.x + 20;
    btn.y = panel.y + 20;
    btn.w = 140;
    btn.h = 30;
    btn.label = "CLICK ME";
    btn.hovered = 0;
    btn.pressed = 0;

    exit_btn.x = btn.x + btn.w + 20;
    exit_btn.y = btn.y;
    exit_btn.w = panel.x + panel.w - 20 - exit_btn.x;
    exit_btn.h = 30;
    exit_btn.label = "EXIT";
    exit_btn.hovered = 0;
    exit_btn.pressed = 0;

    /* Positioned to overlap the panel's bottom-right corner from the
     * start, so the two windows' stacking order is visibly meaningful
     * the moment this boots, not just after someone drags one on top of
     * the other. */
    info_panel.x = 380;
    info_panel.y = 300;
    info_panel.w = 200;
    info_panel.h = 90;
    info_panel.title = "RAVE-OS INFO";

    typed[0] = 0;

    mx = w / 2;
    my = h - 100; /* start clear of the panels above */

    /* IDT/PIC set up first (masked, no sti yet), then the mouse's polling
     * handshake runs with IRQ12 still masked so it can't race the new
     * interrupt handler for the same bytes, then interrupts are actually
     * enabled once both are ready. */
    interrupts_init();
    mouse_init();
    interrupts_enable();

    draw_scene(w, h, &panel, &btn, &exit_btn, click_count, typed, &info_panel, panel_on_top, mx, my, cursor_color);
    gfx_present();

    for (;;) {
        int had_event = 0;
        int dx, dy, buttons;
        char c;
        int old_mx = mx;
        int old_my = my;
        int old_panel_x = panel.x;
        int old_panel_y = panel.y;
        int old_info_x = info_panel.x;
        int old_info_y = info_panel.y;
        int old_btn_hovered = btn.hovered;
        int old_btn_pressed = btn.pressed;
        int old_exit_hovered = exit_btn.hovered;
        int old_exit_pressed = exit_btn.pressed;
        int old_click_count = click_count;
        int old_typed_len = typed_len;
        int old_panel_on_top = panel_on_top;

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
            int left_held, cx, cy;

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
            cx = mx + CURSOR_SIZE / 2;
            cy = my + CURSOR_SIZE / 2;

            /* Dragging applies each packet's raw dx/dy to whichever window
             * is being dragged (and, for the panel, its buttons) the same
             * way it's already applied to the cursor above -- since PS/2
             * deltas are relative, this keeps the cursor's position over
             * the title bar constant for the whole drag with no separate
             * grab offset to track. A press that isn't already dragging
             * something looks up which window (if either) is topmost
             * under the cursor: that window is raised to front, and if
             * the press specifically landed on its title bar, a drag
             * starts too -- so grabbing a background window's title bar
             * both raises and starts moving it in one motion, the same
             * as any desktop. */
            if (dragging_window == WIN_PANEL) {
                if (left_held) {
                    int drag_start_x = panel.x;
                    int drag_start_y = panel.y;
                    int applied_dx, applied_dy;

                    panel.x += dx;
                    panel.y += dy;
                    clamp_window_to_screen(&panel, w, h);

                    applied_dx = panel.x - drag_start_x;
                    applied_dy = panel.y - drag_start_y;
                    btn.x += applied_dx;
                    btn.y += applied_dy;
                    exit_btn.x += applied_dx;
                    exit_btn.y += applied_dy;
                } else {
                    dragging_window = -1;
                }
            } else if (dragging_window == WIN_INFO) {
                if (left_held) {
                    info_panel.x += dx;
                    info_panel.y += dy;
                    clamp_window_to_screen(&info_panel, w, h);
                } else {
                    dragging_window = -1;
                }
            } else if (left_held && !prev_left_held) {
                int target = topmost_window_at(cx, cy, panel_on_top, &panel, &info_panel);

                if (target == WIN_PANEL) {
                    panel_on_top = 1;
                    if (window_titlebar_hit_test(&panel, cx, cy)) {
                        dragging_window = WIN_PANEL;
                    }
                } else if (target == WIN_INFO) {
                    panel_on_top = 0;
                    if (window_titlebar_hit_test(&info_panel, cx, cy)) {
                        dragging_window = WIN_INFO;
                    }
                }
            }

            /* The panel's buttons only respond if the panel is actually
             * the topmost thing under the cursor -- otherwise a click on
             * a point where the info window covers the panel would click
             * straight through to a button the user can't even see. */
            {
                int panel_is_topmost = topmost_window_at(cx, cy, panel_on_top, &panel, &info_panel) == WIN_PANEL;

                btn.hovered = panel_is_topmost && button_hit_test(&btn, cx, cy);
                if (btn.hovered && left_held && !prev_left_held) {
                    click_count++;
                }
                btn.pressed = btn.hovered && left_held;

                exit_btn.hovered = panel_is_topmost && button_hit_test(&exit_btn, cx, cy);
                if (exit_btn.hovered && left_held && !prev_left_held) {
                    power_shutdown();
                }
                exit_btn.pressed = exit_btn.hovered && left_held;
            }

            prev_left_held = left_held;
            cursor_color = left_held ? CURSOR_CLICK_COLOR : CURSOR_IDLE_COLOR;

            had_event = 1;
        }

        if (keyboard_poll_char(&c)) {
            if (c == '\b') {
                if (typed_len > 0) {
                    typed[--typed_len] = 0;
                }
            } else if (c == '\n') {
                typed_len = 0;
                typed[0] = 0;
            } else if (typed_len < TYPED_MAX) {
                typed[typed_len++] = c;
                typed[typed_len] = 0;
            }
            had_event = 1;
        }

        if (had_event) {
            int panel_touched = (panel.x != old_panel_x) || (panel.y != old_panel_y) ||
                                 (btn.hovered != old_btn_hovered) || (btn.pressed != old_btn_pressed) ||
                                 (exit_btn.hovered != old_exit_hovered) || (exit_btn.pressed != old_exit_pressed) ||
                                 (click_count != old_click_count) || (typed_len != old_typed_len);
            int info_touched = (info_panel.x != old_info_x) || (info_panel.y != old_info_y);
            int z_reordered = (panel_on_top != old_panel_on_top);

            update_and_present(w, h, &panel, &btn, &exit_btn, click_count, typed, &info_panel, panel_on_top, old_mx,
                               old_my, mx, my, cursor_color, old_panel_x, old_panel_y, old_info_x, old_info_y,
                               panel_touched, info_touched, z_reordered);
        } else {
            __asm__ volatile("hlt");
        }
    }
}
