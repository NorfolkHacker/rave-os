/* A toggle box with a label -- third widget after button.c and
 * textfield.c, same androidacid.com palette and the same rounded-rect
 * primitive the re-theme and rounding passes gave the others. */

#include "checkbox.h"
#include "graphics.h"
#include "text.h"

#define CHECKBOX_BORDER_COLOR 0x00FF66      /* --hard, checked or hovered */
#define CHECKBOX_BORDER_COLOR_IDLE 0x1F2E27 /* dim, unchecked and not hovered */
#define CHECKBOX_FILL_COLOR 0x0A1A12        /* same as button idle fill */
#define CHECKBOX_HOVER_COLOR 0x123322       /* same as button hover fill */
#define CHECKBOX_MARK_COLOR 0x00FF66        /* --hard */
#define CHECKBOX_LABEL_COLOR 0xD4E6DB       /* --text at 0.9 alpha over --bg */
#define CHECKBOX_LABEL_SCALE 1
#define GLYPH_HEIGHT 7 /* font.c's glyphs are 7 rows tall at scale 1 */
#define CHECKBOX_CORNER_RADIUS 3
#define CHECKBOX_LABEL_GAP 6
#define CHECKBOX_MARK_INSET 4

static int checkbox_label_width(const struct checkbox *cb) {
    return cb->label ? text_width(cb->label, CHECKBOX_LABEL_SCALE) : 0;
}

int checkbox_hit_test(const struct checkbox *cb, int px, int py) {
    int total_w = cb->size + (cb->label ? CHECKBOX_LABEL_GAP + checkbox_label_width(cb) : 0);
    int box_center_y = cb->y + cb->size / 2;
    int label_top = box_center_y - (GLYPH_HEIGHT * CHECKBOX_LABEL_SCALE) / 2;
    int label_bottom = label_top + GLYPH_HEIGHT * CHECKBOX_LABEL_SCALE;
    int top = cb->y < label_top ? cb->y : label_top;
    int box_bottom = cb->y + cb->size;
    int bottom = box_bottom > label_bottom ? box_bottom : label_bottom;

    return px >= cb->x && px < cb->x + total_w && py >= top && py < bottom;
}

void checkbox_draw(const struct checkbox *cb) {
    uint32_t border = (cb->checked || cb->hovered) ? CHECKBOX_BORDER_COLOR : CHECKBOX_BORDER_COLOR_IDLE;
    uint32_t fill = (cb->hovered && !cb->checked) ? CHECKBOX_HOVER_COLOR : CHECKBOX_FILL_COLOR;
    int box_center_y = cb->y + cb->size / 2;
    int label_y = box_center_y - (GLYPH_HEIGHT * CHECKBOX_LABEL_SCALE) / 2;

    gfx_fill_rounded_rect(cb->x - 1, cb->y - 1, cb->size + 2, cb->size + 2, CHECKBOX_CORNER_RADIUS, border);
    gfx_fill_rounded_rect(cb->x, cb->y, cb->size, cb->size, CHECKBOX_CORNER_RADIUS, fill);

    if (cb->checked) {
        int inset = CHECKBOX_MARK_INSET;
        int inner_r = CHECKBOX_CORNER_RADIUS > 1 ? CHECKBOX_CORNER_RADIUS - 1 : 0;
        gfx_fill_rounded_rect(cb->x + inset, cb->y + inset, cb->size - 2 * inset, cb->size - 2 * inset, inner_r,
                              CHECKBOX_MARK_COLOR);
    }

    if (cb->label) {
        text_puts(cb->x + cb->size + CHECKBOX_LABEL_GAP, label_y, cb->label, CHECKBOX_LABEL_COLOR,
                  CHECKBOX_LABEL_SCALE);
    }
}
