#!/usr/bin/env python3
"""Regenerates the acid OS v2 desktop wallpaper.

Draws a pixel-art synthwave scene (grid floor, gradient-banded sun, star
field) sized to the sim's fixed 640x360 screen (KERNEL_SCREEN_W/H), writes
it to v2/fsroot/wallpaper.png, then RLE-encodes it into
v2/core/gfx/wallpaper_data.h -- the table wallpaper.c replays with
gfx_fill_rect to build the on-screen background once at boot (the gfx stack
has no PNG/image decoder wired in, so nothing loads the PNG file itself at
runtime -- see wallpaper.c's own comment).

Color choice: window CHROME (title bars, borders, text) stays strictly the
5 fixed colors in kernel_theme.h -- but this is desktop CONTENT, the same
category acid_palette.rb's hue wheel exists for ("so any app can pull a
genuinely distinct, vivid color per item instead" of the same few chrome
shades -- see that file's own comment). So the wallpaper uses its own small
fixed neon palette (classic outrun/acid-house: yellow-orange-pink-purple
sun, cyan grid) instead of being limited to kernel_theme.h's greens -- only
the grid itself stays acid green, to visually tie the background back to
the OS's own accent color.

The sun/horizon/grid are packed into the band from y=204
(KERNEL_DESKTOP_STRIP_H + desktop.rb's own dropdown height) downward --
below the 24px taskbar strip, that's what always composites directly onto
the real screen. Above y=204 is desktop.rb's own registered window (kept
that tall so it can hit-test a click anywhere the dropdown might open --
see TOTAL_H's own comment in desktop.rb), which repaints real wallpaper
pixels into its own canvas there whenever the dropdown is closed
(acid_repaint_region/wallpaper_draw_into), so that band is fully visible
too -- just repainted rather than composited directly. A full, unclipped
sun matters more than a big one, so its radius is sized to fit entirely
below y=204 rather than spilling up past it, and stars are deliberately
sparser above that line than below it, an intentional density gradient
rather than an oversight. Rerun after changing the palette, resolution, or
artwork; requires Pillow (`pip install pillow`).
"""

import random

from PIL import Image

REPO_ROOT = __file__.rsplit("/v2/tools/", 1)[0]
PNG_OUT = f"{REPO_ROOT}/v2/fsroot/wallpaper.png"
HEADER_OUT = f"{REPO_ROOT}/v2/core/gfx/wallpaper_data.h"

random.seed(7)

# Index order must match g_wallpaper_palette in the generated header.
BG       = (0x05, 0x06, 0x0A)  # night sky / above the visible band
GRID     = (0x00, 0xFF, 0x66)  # acid green -- ties back to the OS's own accent
GROUND   = (0x18, 0x0A, 0x2A)  # deep purple ground plane between grid lines
MOUNTAIN = (0x3A, 0x17, 0x50)  # mid-purple distant silhouette
SUN1     = (0xFF, 0xD4, 0x00)  # sun band 1 (top): yellow
SUN2     = (0xFF, 0x7A, 0x00)  # sun band 2: orange
SUN3     = (0xFF, 0x2D, 0x78)  # sun band 3: hot pink
SUN4     = (0xA0, 0x20, 0xF0)  # sun band 4 (bottom): purple
STAR_W   = (0xD4, 0xE6, 0xDB)  # white-ish star
STAR_C   = (0x00, 0xE5, 0xFF)  # cyan star

PALETTE = [BG, GRID, GROUND, MOUNTAIN, SUN1, SUN2, SUN3, SUN4, STAR_W, STAR_C]
SUN_BANDS = [SUN1, SUN2, SUN3, SUN4]

W, H = 160, 90  # low-res canvas, scaled 4x -> 640x360
SCALE = 4

# Visible band starts at row 51 (y=204 at 4x scale) -- see module docstring.
VISIBLE_TOP = 51
SUN_R = 9
SUN_CX, SUN_CY = 80, VISIBLE_TOP + 3 + SUN_R  # 3-row gap, full disc below it
HORIZON = SUN_CY + SUN_R + 1


def draw_wallpaper():
    img = Image.new("RGB", (W, H), BG)
    px = img.load()

    # stars -- a thin band right above the sun plus a sparser field further
    # up into the rest of the sky (see module docstring: desktop.rb now
    # repaints real wallpaper pixels behind its own closed dropdown, via
    # acid_repaint_region/wallpaper_draw_into, so this whole sky is
    # actually visible live, not just the grid-floor band below it)
    for _ in range(8):
        x = random.randint(0, W - 1)
        y = random.randint(VISIBLE_TOP, SUN_CY - SUN_R - 1)
        if (x - SUN_CX) ** 2 + (y - SUN_CY) ** 2 < (SUN_R + 3) ** 2:
            continue
        px[x, y] = random.choice([STAR_W, STAR_C, STAR_C])
    for _ in range(32):
        x = random.randint(0, W - 1)
        y = random.randint(0, VISIBLE_TOP - 1)
        px[x, y] = random.choice([STAR_W, STAR_C, STAR_C])

    # distant mountain silhouette, jagged pixel skyline just above horizon.
    # Built from explicit peaks placed across the FULL width (not a random
    # walk from x=0, which -- see this file's own git history -- could
    # wander and stay low for a long stretch, leaving one whole side flat;
    # placing a peak near every span of the width guarantees tall ridges
    # both left and right, not just wherever the walk happened to climb).
    mountain_h = [0] * W
    peaks = []
    x = 0
    while x < W:
        peak_w = random.randint(10, 22)
        peak_h = random.randint(8, 14)
        peaks.append((x + peak_w // 2, peak_h, peak_w))
        x += random.randint(int(peak_w * 0.4), int(peak_w * 0.75))
    for x in range(W):
        h = 0
        for peak_x, peak_h, peak_w in peaks:
            half = peak_w // 2
            d = abs(x - peak_x)
            if d < half:
                hh = peak_h - ( peak_h * d ) // ( half + 1 )
                if hh > h:
                    h = hh
        mountain_h[x] = max(0, min(14, h))
    for x in range(W):
        for y in range(HORIZON - mountain_h[x], HORIZON):
            px[x, y] = MOUNTAIN

    # sun: filled circle, banded yellow->orange->pink->purple top to bottom,
    # with horizontal retro slit-lines (alternating gaps)
    gap_rows = set()
    y = SUN_CY - SUN_R
    row = 0
    while y <= SUN_CY + SUN_R:
        if row % 4 in (2, 3):
            gap_rows.add(y)
        y += 1
        row += 1

    for y in range(SUN_CY - SUN_R, SUN_CY + SUN_R + 1):
        if y >= HORIZON:
            continue
        dy = y - SUN_CY
        span = int((SUN_R * SUN_R - dy * dy) ** 0.5)
        band = min(len(SUN_BANDS) - 1,
                    (dy + SUN_R) * len(SUN_BANDS) // (2 * SUN_R + 1))
        color = SUN_BANDS[band]
        for x in range(SUN_CX - span, SUN_CX + span + 1):
            if y in gap_rows:
                continue
            px[x, y] = color

    # ground plane fill
    for y in range(HORIZON, H):
        for x in range(W):
            px[x, y] = GROUND

    # perspective grid: horizontal lines converging toward horizon
    gy = float(HORIZON)
    step = 1.0
    while gy < H:
        yy = int(gy)
        if yy < H:
            for x in range(W):
                px[x, yy] = GRID if (x % 2 == 0 or step > 3) else GROUND
        step += 1
        gy += step * 0.9

    # vertical grid lines radiating from the vanishing point (sun center)
    # to the bottom edge
    n_lines = 15
    for k in range(-n_lines, n_lines + 1):
        fx = SUN_CX + k * 6
        for y in range(HORIZON, H):
            t = (y - HORIZON) / float(H - HORIZON)
            x = int(SUN_CX + (fx - SUN_CX) * (1 + t * 3))
            if 0 <= x < W:
                px[x, y] = GRID
            x2 = x + 1
            if 0 <= x2 < W and t > 0.4:
                px[x2, y] = GRID

    # horizon glow line
    for x in range(W):
        px[x, HORIZON] = GRID
        if HORIZON + 1 < H:
            px[x, HORIZON + 1] = MOUNTAIN

    return img.resize((W * SCALE, H * SCALE), Image.NEAREST)


def encode_header(img):
    idx_of = {c: i for i, c in enumerate(PALETTE)}
    px = img.load()
    Wf, Hf = img.size

    runs = []
    for y in range(Hf):
        x = 0
        while x < Wf:
            c = px[x, y]
            if c not in idx_of:
                raise SystemExit(f"unexpected color {c} at {x},{y}")
            ci = idx_of[c]
            start = x
            while x < Wf and px[x, y] == c:
                x += 1
            runs.append((y, start, x - start, ci))

    with open(HEADER_OUT, "w") as f:
        f.write("#ifndef ACID_WALLPAPER_DATA_H\n#define ACID_WALLPAPER_DATA_H\n\n")
        f.write("/* Generated by v2/tools/gen_wallpaper.py -- do not hand-edit.\n")
        f.write(" * Rerun that script to regenerate after changing the artwork. Each\n")
        f.write(" * run is one horizontal, single-color span (y, x, length, palette\n")
        f.write(" * index); wallpaper.c replays these with gfx_fill_rect once, into a\n")
        f.write(" * canvas built the first time it composites, not decoded from the\n")
        f.write(" * PNG at runtime (this codebase's gfx stack has no image/PNG decoder\n")
        f.write(" * wired in -- see wallpaper.c's own comment). This is its own small\n")
        f.write(" * neon palette, not kernel_theme.h's 5 chrome colors -- see this\n")
        f.write(" * script's own module docstring for why. */\n\n")
        f.write(f"#define WALLPAPER_W {Wf}\n")
        f.write(f"#define WALLPAPER_H {Hf}\n\n")
        f.write("struct wallpaper_run { unsigned short y; unsigned short x; "
                 "unsigned short len; unsigned char color; };\n\n")
        f.write(f"static const unsigned int g_wallpaper_palette[{len(PALETTE)}] = {{\n")
        for r, g, b in PALETTE:
            f.write(f"    0x{(r << 16) | (g << 8) | b:06X}u,\n")
        f.write("};\n\n")
        f.write("static const struct wallpaper_run g_wallpaper_runs[] = {\n")
        for y, x, length, ci in runs:
            f.write(f"{{{y},{x},{length},{ci}}},")
        f.write("\n};\n\n")
        f.write("#define WALLPAPER_RUN_COUNT "
                 "( sizeof( g_wallpaper_runs ) / sizeof( g_wallpaper_runs[0] ) )\n\n")
        f.write("#endif\n")

    print(f"wrote {HEADER_OUT} ({len(runs)} runs)")


if __name__ == "__main__":
    image = draw_wallpaper()
    image.save(PNG_OUT)
    print(f"wrote {PNG_OUT} ({image.size[0]}x{image.size[1]})")
    encode_header(image)
