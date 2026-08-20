#include "console_history.h"

void console_history_init(struct console_history *h) {
    h->count = 0;
    h->next = 0;
    h->browse_offset = -1;
}

void console_history_push(struct console_history *h, const char *line) {
    int i = 0;

    if (!line[0]) {
        return; /* empty line -- not worth remembering */
    }
    while (line[i] && i < CONSOLE_INPUT_MAX) {
        h->lines[h->next][i] = line[i];
        i++;
    }
    h->lines[h->next][i] = 0;
    h->next = (h->next + 1) % CONSOLE_HISTORY_MAX;
    if (h->count < CONSOLE_HISTORY_MAX) {
        h->count++;
    }
    h->browse_offset = -1;
}

void console_history_reset_browse(struct console_history *h) {
    h->browse_offset = -1;
}

/* index into lines[] for "browse_offset steps back from the most recent
 * entry" -- next - 1 is the most recent slot, each further step back
 * wraps around the ring. The "+ CONSOLE_HISTORY_MAX * 2" keeps the value
 * comfortably positive before the final modulo (next-1 can be -1,
 * browse_offset can be up to CONSOLE_HISTORY_MAX-1). */
static int history_index(const struct console_history *h, int offset) {
    return (h->next - 1 - offset + CONSOLE_HISTORY_MAX * 2) % CONSOLE_HISTORY_MAX;
}

static void copy_out(const char *src, char *out) {
    int i = 0;
    while (src[i]) {
        out[i] = src[i];
        i++;
    }
    out[i] = 0;
}

int console_history_prev(struct console_history *h, char *out) {
    if (h->count == 0) {
        return 0;
    }
    if (h->browse_offset < 0) {
        h->browse_offset = 0;
    } else if (h->browse_offset + 1 < h->count) {
        h->browse_offset++;
    } else {
        return 0; /* already at the oldest entry */
    }
    copy_out(h->lines[history_index(h, h->browse_offset)], out);
    return 1;
}

int console_history_next(struct console_history *h, char *out) {
    if (h->browse_offset < 0) {
        return 0; /* not browsing -- nothing to do */
    }
    if (h->browse_offset == 0) {
        h->browse_offset = -1;
        out[0] = 0;
        return 1;
    }
    h->browse_offset--;
    copy_out(h->lines[history_index(h, h->browse_offset)], out);
    return 1;
}
