/* A clickable rectangle with a label -- the first real widget, and the
 * thing that turns "graphics + mouse" into "a GUI": hit-testing is what
 * lets the mouse cursor's position mean something beyond just drawing
 * where it is. */

#include "button.h"
#include "graphics.h"
#include "text.h"

#define BUTTON_BORDER_COLOR 0xFFFFFF
#define BUTTON_FILL_COLOR 0x303048
#define BUTTON_HOVER_COLOR 0x4040A8
#define BUTTON_PRESSED_COLOR 0x8000E0
#define BUTTON_LABEL_COLOR 0xFFFFFF
#define BUTTON_LABEL_SCALE 1
#define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */

int button_hit_test(const struct button *btn, int px, int py) {
    return px >= btn->x && px < btn->x + btn->w && py >= btn->y && py < btn->y + btn->h;
}

void button_draw(const struct button *btn) {
    uint32_t fill = btn->pressed ? BUTTON_PRESSED_COLOR : (btn->hovered ? BUTTON_HOVER_COLOR : BUTTON_FILL_COLOR);
    int text_x = btn->x + (btn->w - text_width(btn->label, BUTTON_LABEL_SCALE)) / 2;
    int text_y = btn->y + (btn->h - GLYPH_HEIGHT * BUTTON_LABEL_SCALE) / 2;

    gfx_fill_rect(btn->x - 1, btn->y - 1, btn->w + 2, btn->h + 2, BUTTON_BORDER_COLOR);
    gfx_fill_rect(btn->x, btn->y, btn->w, btn->h, fill);
    text_puts(text_x, text_y, btn->label, BUTTON_LABEL_COLOR, BUTTON_LABEL_SCALE);
}
