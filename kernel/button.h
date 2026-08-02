#ifndef RAVEOS_BUTTON_H
#define RAVEOS_BUTTON_H

struct button {
    int x, y, w, h;
    const char *label;
    int hovered; /* cursor currently over the button */
    int pressed; /* cursor over it AND left button currently held */
};

/* Point-in-rect test against the button's bounds -- pass the cursor's
 * hotspot (kernel.c uses its center), not its top-left corner. */
int button_hit_test(const struct button *btn, int px, int py);

void button_draw(const struct button *btn);

#endif
