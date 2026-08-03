#ifndef RAVEOS_FORTH_H
#define RAVEOS_FORTH_H

#include <stdint.h>

/* Rave-OS's own Forth dialect, Stage B: a minimal interpreter core.
 * Immediate execution only -- no user-defined words yet (that's Stage
 * C's ':'/';'). No GUI dependencies at all (no window.h, no graphics.h,
 * nothing console_*) -- this file should be reasoned about entirely on
 * its own; kernel.c is the only thing that knows a Forth console window
 * exists.
 *
 * No malloc anywhere in this kernel (confirmed before this file was
 * written), so the data stack is a fixed-size static array, sized
 * generously but arbitrarily -- 128 cells is far more than any line
 * typed at a REPL needs, chosen the same way the panel's other fixed
 * buffers were: comfortably safe, not load-bearing precise. */
#define FORTH_DSTACK_SIZE 128
#define FORTH_TOKEN_MAX 32
#define FORTH_ERROR_MAX 32

struct forth_vm {
    int32_t dstack[FORTH_DSTACK_SIZE];
    int dsp; /* 0 = empty */
    char error[FORTH_ERROR_MAX]; /* empty string = no error */

    /* Transient per-eval output cursor, not persistent Forth state --
     * primitives like '.' and CR need somewhere to write, and every
     * primitive shares the same void(*)(struct forth_vm *) signature (so
     * Stage C can call them by table index from compiled word bodies),
     * so the output cursor rides along on the vm struct itself rather
     * than threading an extra parameter through every primitive. Reset
     * at the start of every forth_eval_line() call. */
    char *out;
    int out_pos;
    int out_max;
};

void forth_init(struct forth_vm *vm);

/* Tokenizes and executes one line immediately (no user-defined words
 * yet -- Stage C adds those). Writes human-readable output into out[]
 * (NUL-terminated, '\n'-separated per logical output line -- forth.c
 * never touches console_output.h; the caller splits out[] on '\n' and
 * appends each segment as its own console line). On error (unknown
 * word, stack underflow/overflow, divide by zero), execution of the
 * rest of the line stops, the data stack is reset to empty (real
 * Forth's ABORT behavior -- simpler to reason about than leaving a
 * partially-consumed stack for the next line), and an error message is
 * appended to out[]. */
void forth_eval_line(struct forth_vm *vm, const char *line, char *out, int out_max);

#endif
