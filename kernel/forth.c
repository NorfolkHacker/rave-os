/* Stage B's interpreter core: a tokenizer, a fixed-size data stack, and a
 * small table of built-in primitives. See forth.h for why this file has
 * no GUI dependencies at all -- it's meant to be understood entirely on
 * its own.
 *
 * Two things matter more here than in most of this kernel's code, because
 * this is the first place user-typed input reaches something that can
 * crash or corrupt the whole machine, not just misbehave locally:
 *
 * 1. `/`'s C implementation must never actually execute a division by
 *    zero. isr.c has a real vector-0 handler (`isr_divide_error` ->
 *    `panic("PANIC: DIVIDE ERROR")`) -- an unchecked `idiv` on this
 *    kernel doesn't raise something Forth can catch, it halts the whole
 *    OS. A user's typo `5 0 /` must produce a Forth-level error, not a
 *    kernel panic.
 * 2. Every stack access is bounds-checked before touching dstack[] --
 *    this kernel has no paging and no memory protection at all, so an
 *    unchecked overflow would silently corrupt whatever static data the
 *    linker happened to place next in .bss. */

#include "forth.h"

#define FORTH_NUM_PRIMITIVES ((int)(sizeof(primitives) / sizeof(primitives[0])))

static void forth_write(struct forth_vm *vm, const char *s) {
    while (*s && vm->out_pos < vm->out_max - 1) {
        vm->out[vm->out_pos++] = *s++;
    }
    vm->out[vm->out_pos] = 0;
}

/* First error in a line wins -- once one is set, forth_eval_line()'s
 * loop stops processing further tokens, so this is a defensive
 * guarantee, not something normally exercised. */
static void forth_set_error(struct forth_vm *vm, const char *msg) {
    int i = 0;
    if (vm->error[0]) {
        return;
    }
    while (msg[i] && i < FORTH_ERROR_MAX - 1) {
        vm->error[i] = msg[i];
        i++;
    }
    vm->error[i] = 0;
}

static void forth_push(struct forth_vm *vm, int32_t v) {
    if (vm->dsp >= FORTH_DSTACK_SIZE) {
        forth_set_error(vm, "STACK FULL");
        return;
    }
    vm->dstack[vm->dsp++] = v;
}

/* Returns 0 (and sets the error) on underflow instead of ever reading
 * dstack[-1] -- callers must check the return value before trusting
 * *out. */
static int forth_pop(struct forth_vm *vm, int32_t *out) {
    if (vm->dsp <= 0) {
        forth_set_error(vm, "STACK EMPTY");
        return 0;
    }
    *out = vm->dstack[--vm->dsp];
    return 1;
}

static void format_int(int32_t v, char *out) {
    char tmp[12];
    uint32_t uv;
    int neg = v < 0;
    int i = 0, j = 0;

    uv = neg ? (uint32_t)(-(v + 1)) + 1u : (uint32_t)v; /* avoids negating INT32_MIN, which overflows */
    if (uv == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (uv > 0) {
        tmp[i++] = (char)('0' + uv % 10);
        uv /= 10;
    }
    if (neg) {
        out[j++] = '-';
    }
    while (i > 0) {
        out[j++] = tmp[--i];
    }
    out[j] = 0;
}

/* Not a full integer parse -- just enough to tell a numeric token from a
 * word name. Returns 0 (leaving *out untouched) for anything that isn't
 * an optional '-' followed by at least one digit, so a bare "-" falls
 * through to the primitive table (it's the subtract word) instead of
 * being treated as a malformed number. */
static int parse_int(const char *tok, int32_t *out) {
    int i = 0;
    int neg = 0;
    int32_t val = 0;
    int digits = 0;

    if (tok[0] == '-') {
        neg = 1;
        i = 1;
    }
    for (; tok[i]; i++) {
        if (tok[i] < '0' || tok[i] > '9') {
            return 0;
        }
        val = val * 10 + (tok[i] - '0');
        digits++;
    }
    if (digits == 0) {
        return 0;
    }
    *out = neg ? -val : val;
    return 1;
}

static int str_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - 32);
        }
        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - 32);
        }
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static void prim_add(struct forth_vm *vm) {
    int32_t a, b;
    if (!forth_pop(vm, &b) || !forth_pop(vm, &a)) {
        return;
    }
    forth_push(vm, a + b);
}

static void prim_sub(struct forth_vm *vm) {
    int32_t a, b;
    if (!forth_pop(vm, &b) || !forth_pop(vm, &a)) {
        return;
    }
    forth_push(vm, a - b);
}

static void prim_mul(struct forth_vm *vm) {
    int32_t a, b;
    if (!forth_pop(vm, &b) || !forth_pop(vm, &a)) {
        return;
    }
    forth_push(vm, a * b);
}

static void prim_div(struct forth_vm *vm) {
    int32_t a, b;
    if (!forth_pop(vm, &b) || !forth_pop(vm, &a)) {
        return;
    }
    if (b == 0) {
        forth_set_error(vm, "DIV BY ZERO");
        return;
    }
    forth_push(vm, a / b);
}

static void prim_dup(struct forth_vm *vm) {
    int32_t a;
    if (!forth_pop(vm, &a)) {
        return;
    }
    forth_push(vm, a);
    forth_push(vm, a);
}

static void prim_drop(struct forth_vm *vm) {
    int32_t a;
    forth_pop(vm, &a); /* forth_pop() already sets the error on underflow */
}

static void prim_swap(struct forth_vm *vm) {
    int32_t a, b;
    if (!forth_pop(vm, &b) || !forth_pop(vm, &a)) {
        return;
    }
    forth_push(vm, b);
    forth_push(vm, a);
}

static void prim_over(struct forth_vm *vm) {
    int32_t a, b;
    if (!forth_pop(vm, &b) || !forth_pop(vm, &a)) {
        return;
    }
    forth_push(vm, a);
    forth_push(vm, b);
    forth_push(vm, a);
}

static void prim_dot(struct forth_vm *vm) {
    int32_t a;
    char buf[16];
    if (!forth_pop(vm, &a)) {
        return;
    }
    format_int(a, buf);
    forth_write(vm, buf);
    forth_write(vm, " ");
}

static void prim_cr(struct forth_vm *vm) {
    forth_write(vm, "\n");
}

struct forth_word {
    const char *name;
    void (*fn)(struct forth_vm *vm);
};

/* Referenced by index (not just by name) starting in Stage C, where a
 * compiled word body needs to call a primitive by a small integer stored
 * in its instruction stream rather than re-searching by string every
 * time -- the table shape here is chosen with that reuse in mind. */
static const struct forth_word primitives[] = {
    {"+", prim_add},   {"-", prim_sub},  {"*", prim_mul},   {"/", prim_div}, {"DUP", prim_dup},
    {"DROP", prim_drop}, {"SWAP", prim_swap}, {"OVER", prim_over}, {".", prim_dot}, {"CR", prim_cr},
};

void forth_init(struct forth_vm *vm) {
    vm->dsp = 0;
    vm->error[0] = 0;
    vm->out = 0;
    vm->out_pos = 0;
    vm->out_max = 0;
}

void forth_eval_line(struct forth_vm *vm, const char *line, char *out, int out_max) {
    int li = 0;

    vm->error[0] = 0;
    vm->out = out;
    vm->out_pos = 0;
    vm->out_max = out_max;
    if (out_max > 0) {
        out[0] = 0;
    }

    while (line[li] && !vm->error[0]) {
        char token[FORTH_TOKEN_MAX];
        int ti = 0;
        int32_t num;

        while (line[li] == ' ' || line[li] == '\t') {
            li++;
        }
        if (!line[li]) {
            break;
        }
        while (line[li] && line[li] != ' ' && line[li] != '\t' && ti < FORTH_TOKEN_MAX - 1) {
            token[ti++] = line[li++];
        }
        /* A token longer than the buffer still needs its remaining
         * characters consumed from the line, or the next iteration
         * would resume mid-token and misparse the tail as a new one. */
        while (line[li] && line[li] != ' ' && line[li] != '\t') {
            li++;
        }
        token[ti] = 0;

        if (parse_int(token, &num)) {
            forth_push(vm, num);
        } else {
            int i, found = 0;

            for (i = 0; i < FORTH_NUM_PRIMITIVES; i++) {
                if (str_eq_ci(token, primitives[i].name)) {
                    primitives[i].fn(vm);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                /* Not "?" -- font.c has no glyph for it (falls through to
                 * a blank space), confirmed while headlessly verifying
                 * this stage. UNKNOWN uses only characters the font
                 * actually renders. */
                forth_set_error(vm, "UNKNOWN");
            }
        }
    }

    if (vm->error[0]) {
        vm->dsp = 0; /* ABORT: reset the stack rather than leave it half-consumed for the next line */
        forth_write(vm, "\n");
        forth_write(vm, vm->error);
    }
}
