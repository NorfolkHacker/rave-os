#ifndef RAVEOS_CHECKBOX_H
#define RAVEOS_CHECKBOX_H

struct checkbox {
    int x, y, size;
    const char *label;
    int checked;
    int hovered;
};

/* Hit region covers the box and its label together, not just the box --
 * clicking the label toggles it too, same as a typical desktop checkbox. */
int checkbox_hit_test(const struct checkbox *cb, int px, int py);

void checkbox_draw(const struct checkbox *cb);

#endif
