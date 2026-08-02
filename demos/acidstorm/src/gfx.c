#include "gfx.h"
#include "font.h"
#include <math.h>
#include <string.h>

Color g_fb[FB_H][FB_W];

void gfx_clear(Color c) {
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++)
            g_fb[y][x] = c;
}

void gfx_put(int x, int y, Color c) {
    if ((unsigned)x >= FB_W || (unsigned)y >= FB_H) return;
    g_fb[y][x] = c;
}

void gfx_fill_rect(int x, int y, int w, int h, Color c) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > FB_W) x1 = FB_W;
    int y1 = y + h; if (y1 > FB_H) y1 = FB_H;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            g_fb[yy][xx] = c;
}

void gfx_rect(int x, int y, int w, int h, Color c) {
    gfx_fill_rect(x, y, w, 1, c);
    gfx_fill_rect(x, y + h - 1, w, 1, c);
    gfx_fill_rect(x, y, 1, h, c);
    gfx_fill_rect(x + w - 1, y, 1, h, c);
}

void gfx_line(int x0, int y0, int x1, int y1, Color c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0; /* negative magnitude */
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_put(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_circle(int cx, int cy, int r, Color c) {
    int x = r, y = 0, err = 0;
    while (x >= y) {
        gfx_put(cx + x, cy + y, c); gfx_put(cx + y, cy + x, c);
        gfx_put(cx - y, cy + x, c); gfx_put(cx - x, cy + y, c);
        gfx_put(cx - x, cy - y, c); gfx_put(cx - y, cy - x, c);
        gfx_put(cx + y, cy - x, c); gfx_put(cx + x, cy - y, c);
        y++;
        if (err <= 0) err += 2 * y + 1;
        if (err > 0) { x--; err -= 2 * x + 1; }
    }
}

void gfx_fill_circle(int cx, int cy, int r, Color c) {
    for (int y = -r; y <= r; y++) {
        int span = (int)(sqrtf((float)(r * r - y * y)) + 0.5f);
        gfx_fill_rect(cx - span, cy + y, span * 2 + 1, 1, c);
    }
}

int gfx_text_width(const char *s, int scale) {
    int n = (int)strlen(s);
    if (n == 0) return 0;
    return n * 6 * scale - scale;
}

void gfx_text(int x, int y, const char *s, Color c, int scale) {
    int cx = x;
    for (const char *p = s; *p; p++) {
        const char *const *glyph = font_glyph(*p);
        if (glyph) {
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5; col++)
                    if (glyph[row][col] == 'X')
                        gfx_fill_rect(cx + col * scale, y + row * scale, scale, scale, c);
        }
        cx += 6 * scale;
    }
}

Color hsv_to_rgb(double h, double s, double v) {
    h = fmod(fmod(h, 1.0) + 1.0, 1.0);
    double i = floor(h * 6.0);
    double f = h * 6.0 - i;
    double p = v * (1.0 - s);
    double q = v * (1.0 - s * f);
    double t = v * (1.0 - s * (1.0 - f));
    double r, g, b;
    switch (((int)i) % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return RGB((int)(r * 255), (int)(g * 255), (int)(b * 255));
}

#define LUT_N 256
static float g_sinlut[LUT_N];
static int g_lut_ready = 0;

static void ensure_lut(void) {
    if (g_lut_ready) return;
    for (int i = 0; i < LUT_N; i++)
        g_sinlut[i] = sinf((2.0f * (float)M_PI * i) / LUT_N);
    g_lut_ready = 1;
}

static inline float lsin(float rad) {
    int idx = (int)(rad * (LUT_N / (2.0f * (float)M_PI)));
    idx &= (LUT_N - 1);
    return g_sinlut[idx];
}

void gfx_plasma(double t) {
    ensure_lut();
    float ft = (float)t;
    float cx = FB_W * 0.5f, cy = FB_H * 0.5f;
    for (int y = 0; y < FB_H; y++) {
        float dyc = (float)y - cy;
        for (int x = 0; x < FB_W; x++) {
            float dxc = (float)x - cx;
            float v = lsin(x * 0.045f + ft * 1.3f)
                    + lsin(y * 0.06f - ft * 1.0f)
                    + lsin((x + y) * 0.03f + ft * 0.7f)
                    + lsin(sqrtf(dxc * dxc + dyc * dyc) * 0.07f - ft * 1.6f);
            float hue = (v + 4.0f) / 8.0f + ft * 0.04f;
            g_fb[y][x] = hsv_to_rgb(hue, 0.85, 0.55 + 0.15f * (v / 4.0f));
        }
    }
}
