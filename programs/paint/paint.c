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
