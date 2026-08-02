/* A clickable rectangle with a label -- the first real widget, and the
 * thing that turns "graphics + mouse" into "a GUI": hit-testing is what
 * lets the mouse cursor's position mean something beyond just drawing
 * where it is. */

#include "button.h"
#include "graphics.h"
#include "text.h"

/* androidacid.com's "black and acid green" palette -- see window.c's and
 * kernel.c's backdrop_color() comments for how these flat-RGB values were
 * derived from the site's actual CSS. Idle/hover follow its .btn/.btn:hover
 * (a subtle green tint bump, not a full color change); pressed inverts to
 * a solid fill of their --hard accent, dark label on top for contrast,
 * echoing how the site uses that color as a hard highlight elsewhere. */
#define BUTTON_BORDER_COLOR 0x00FF66  /* --hard */
#define BUTTON_FILL_COLOR 0x0A1A12    /* rgba(0,255,102,0.09) over --bg */
#define BUTTON_HOVER_COLOR 0x123322   /* rgba(0,255,102,0.13) over --bg, brightened slightly like the panel colors for low-res legibility */
#define BUTTON_PRESSED_COLOR 0x00FF66 /* --hard, solid */
#define BUTTON_LABEL_COLOR 0xD4E6DB   /* --text at 0.9 alpha over --bg */
#define BUTTON_LABEL_COLOR_PRESSED 0x050607 /* --bg -- dark label against the solid-green pressed fill */
#define BUTTON_LABEL_SCALE 1
#define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */
#define BUTTON_CORNER_RADIUS 6

int button_hit_test(const struct button *btn, int px, int py) {
    return px >= btn->x && px < btn->x + btn->w && py >= btn->y && py < btn->y + btn->h;
}

void button_draw(const struct button *btn) {
    uint32_t fill = btn->pressed ? BUTTON_PRESSED_COLOR : (btn->hovered ? BUTTON_HOVER_COLOR : BUTTON_FILL_COLOR);
    uint32_t label_color = btn->pressed ? BUTTON_LABEL_COLOR_PRESSED : BUTTON_LABEL_COLOR;
    int text_x = btn->x + (btn->w - text_width(btn->label, BUTTON_LABEL_SCALE)) / 2;
    int text_y = btn->y + (btn->h - GLYPH_HEIGHT * BUTTON_LABEL_SCALE) / 2;

    gfx_fill_rounded_rect(btn->x - 1, btn->y - 1, btn->w + 2, btn->h + 2, BUTTON_CORNER_RADIUS, BUTTON_BORDER_COLOR);
    gfx_fill_rounded_rect(btn->x, btn->y, btn->w, btn->h, BUTTON_CORNER_RADIUS, fill);
    text_puts(text_x, text_y, btn->label, label_color, BUTTON_LABEL_SCALE);
}
