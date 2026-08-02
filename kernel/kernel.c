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

/* Redraws the whole scene into the backbuffer every call: background,
 * chrome, and the cursor. The scene is cheap enough (a formula-driven
 * backdrop plus a handful of small fixed-size draws) that full redraw is
 * simpler and just as correct as incremental save/restore compositing --
 * it doesn't need to know or guess what's under the cursor, because
 * everything gets redrawn in the right order (background, then window,
 * then button, then text, then cursor on top) every time. gfx_present()
 * still blits the entire backbuffer regardless (a known, deferred
 * performance tradeoff -- see docs/BUILD_LOG.md). */
static void draw_scene(int w, int h, const struct window *panel, const struct button *btn,
                       const struct button *exit_btn, int click_count,
                       const char *typed, int mx, int my, uint32_t cursor_color) {
    int x, y;
    const char *title = "RAVE-OS";
    const char *subtitle = "KERNEL: GUI PRIMITIVES ONLINE";
    int title_scale = 4;
    int subtitle_scale = 2;
    char line[40];
    int pos;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            gfx_put_pixel(x, y, backdrop_color(x, y, w, h));
        }
    }

    text_puts((w - text_width(title, title_scale)) / 2, 40, title, TEXT_ACCENT_COLOR, title_scale);
    text_puts((w - text_width(subtitle, subtitle_scale)) / 2, 90, subtitle, TEXT_MUTED_COLOR, subtitle_scale);

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

    gfx_fill_rect(mx, my, CURSOR_SIZE, CURSOR_SIZE, cursor_color);
}

void kmain(void) {
    int w, h;
    int mx, my;
    int click_count = 0;
    int prev_left_held = 0;
    int dragging_titlebar = 0;
    uint32_t cursor_color = CURSOR_IDLE_COLOR;
    char typed[TYPED_MAX + 1];
    int typed_len = 0;
    struct window panel;
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

    typed[0] = 0;

    mx = w / 2;
    my = h - 100; /* start clear of the panel above */

    /* IDT/PIC set up first (masked, no sti yet), then the mouse's polling
     * handshake runs with IRQ12 still masked so it can't race the new
     * interrupt handler for the same bytes, then interrupts are actually
     * enabled once both are ready. */
    interrupts_init();
    mouse_init();
    interrupts_enable();

    draw_scene(w, h, &panel, &btn, &exit_btn, click_count, typed, mx, my, cursor_color);
    gfx_present();

    for (;;) {
        int had_event = 0;
        int dx, dy, buttons;
        char c;

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

            /* Dragging just applies each packet's raw dx/dy to the window
             * and its widgets the same way it was already applied to the
             * cursor above -- since PS/2 deltas are relative, this keeps
             * the cursor's position over the title bar constant for the
             * whole drag without needing to track a separate grab offset.
             * The press that starts a drag only sets the flag; movement
             * begins on the next packet, so the initial click doesn't also
             * nudge the window by that same packet's motion. */
            if (dragging_titlebar) {
                if (left_held) {
                    panel.x += dx;
                    panel.y += dy;
                    btn.x += dx;
                    btn.y += dy;
                    exit_btn.x += dx;
                    exit_btn.y += dy;
                } else {
                    dragging_titlebar = 0;
                }
            } else if (window_titlebar_hit_test(&panel, cx, cy) && left_held && !prev_left_held) {
                dragging_titlebar = 1;
            }

            btn.hovered = button_hit_test(&btn, cx, cy);
            if (btn.hovered && left_held && !prev_left_held) {
                click_count++;
            }
            btn.pressed = btn.hovered && left_held;

            exit_btn.hovered = button_hit_test(&exit_btn, cx, cy);
            if (exit_btn.hovered && left_held && !prev_left_held) {
                power_shutdown();
            }
            exit_btn.pressed = exit_btn.hovered && left_held;

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
            draw_scene(w, h, &panel, &btn, &exit_btn, click_count, typed, mx, my, cursor_color);
            gfx_present();
        } else {
            __asm__ volatile("hlt");
        }
    }
}
