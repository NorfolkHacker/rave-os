/* The interpreter core. See forth.h for why this file has no GUI
 * dependencies at all -- it's meant to be understood entirely on its
 * own.
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
 * 2. Every stack access -- data stack, return stack, dictionary, code
 *    space -- is bounds-checked before touching its array. This kernel
 *    has no paging and no memory protection at all, so an unchecked
 *    overflow anywhere would silently corrupt whatever static data the
 *    linker happened to place next in .bss. */

#include "forth.h"
#include "forth_hooks.h"

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
    /* x86's idiv traps with the same #DE (divide error) exception for
     * signed overflow, not just division by zero -- specifically
     * INT32_MIN / -1, whose true quotient (2^31) doesn't fit back into a
     * 32-bit signed result. Left unchecked, that trap is exactly the
     * kernel panic this file's header comment says `/` must never cause. */
    if (a == INT32_MIN && b == -1) {
        forth_set_error(vm, "DIV OVERFLOW");
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

/* Real Forth's boolean convention: -1 (all bits set) is true, 0 is
 * false -- not just an arbitrary choice, this is what IF/BEGIN's
 * OP_BRANCH_IF_ZERO actually tests against (zero is the only false
 * value; anything else, including -1, reads as true). */
static void prim_eq(struct forth_vm *vm) {
    int32_t a, b;
    if (!forth_pop(vm, &b) || !forth_pop(vm, &a)) {
        return;
    }
    forth_push(vm, a == b ? -1 : 0);
}

static void prim_lt(struct forth_vm *vm) {
    int32_t a, b;
    if (!forth_pop(vm, &b) || !forth_pop(vm, &a)) {
        return;
    }
    forth_push(vm, a < b ? -1 : 0);
}

static void prim_gt(struct forth_vm *vm) {
    int32_t a, b;
    if (!forth_pop(vm, &b) || !forth_pop(vm, &a)) {
        return;
    }
    forth_push(vm, a > b ? -1 : 0);
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

static void prim_fetch(struct forth_vm *vm) {
    int32_t addr;
    if (!forth_pop(vm, &addr)) {
        return;
    }
    if (addr < 0 || addr >= FORTH_MEM_SIZE) {
        forth_set_error(vm, "BAD ADDR");
        return;
    }
    forth_push(vm, vm->mem[addr]);
}

static void prim_store(struct forth_vm *vm) {
    int32_t val, addr;
    if (!forth_pop(vm, &addr) || !forth_pop(vm, &val)) {
        return;
    }
    if (addr < 0 || addr >= FORTH_MEM_SIZE) {
        forth_set_error(vm, "BAD ADDR");
        return;
    }
    vm->mem[addr] = val;
}

static void prim_paint(struct forth_vm *vm) {
    (void)vm;
    forth_hook_paint_open();
}

static void prim_beep(struct forth_vm *vm) {
    (void)vm;
    forth_hook_beep();
}

static void prim_pixel(struct forth_vm *vm) {
    int32_t x, y, color;
    if (!forth_pop(vm, &color) || !forth_pop(vm, &y) || !forth_pop(vm, &x)) {
        return;
    }
    if (x < 0 || x >= 16 || y < 0 || y >= 16 || color < 0 || color >= 16) {
        forth_set_error(vm, "BAD PIXEL");
        return;
    }
    forth_hook_pixel((int)x, (int)y, (int)color);
}

static void prim_mouse_x(struct forth_vm *vm) {
    forth_push(vm, (int32_t)forth_hook_mouse_x());
}

static void prim_mouse_y(struct forth_vm *vm) {
    forth_push(vm, (int32_t)forth_hook_mouse_y());
}

/* Forth's boolean convention here is -1 = true, 0 = false (same as
 * prim_eq()/prim_lt()/prim_gt() above) -- the hook itself returns a
 * plain C 0/1, converted at this boundary, not pushed raw. */
static void prim_mouse_down(struct forth_vm *vm) {
    forth_push(vm, forth_hook_mouse_down() ? -1 : 0);
}

static void prim_mouse_right_down(struct forth_vm *vm) {
    forth_push(vm, forth_hook_mouse_right_down() ? -1 : 0);
}

static void prim_window_closed(struct forth_vm *vm) {
    forth_push(vm, forth_hook_window_closed() ? -1 : 0);
}

static void prim_current_color(struct forth_vm *vm) {
    forth_push(vm, (int32_t)forth_hook_current_color());
}

struct forth_word {
    const char *name;
    void (*fn)(struct forth_vm *vm);
};

/* Referenced by index, not just by name -- a compiled word body calls a
 * primitive via OP_CALL_PRIMITIVE's index into this table rather than
 * re-searching by string every time. */
static const struct forth_word primitives[] = {
    {"+", prim_add},   {"-", prim_sub},  {"*", prim_mul},   {"/", prim_div}, {"DUP", prim_dup},
    {"DROP", prim_drop}, {"SWAP", prim_swap}, {"OVER", prim_over}, {"=", prim_eq}, {"<", prim_lt},
    {">", prim_gt}, {".", prim_dot}, {"CR", prim_cr}, {"@", prim_fetch}, {"!", prim_store},
    {"PAINT", prim_paint}, {"BEEP", prim_beep}, {"PIXEL", prim_pixel}, {"MOUSE-X", prim_mouse_x}, {"MOUSE-Y", prim_mouse_y},
    {"MOUSE-DOWN?", prim_mouse_down}, {"MOUSE-RIGHT-DOWN?", prim_mouse_right_down},
    {"WINDOW-CLOSED?", prim_window_closed},
    {"CURRENT-COLOR", prim_current_color},
};

void forth_init(struct forth_vm *vm) {
    vm->dsp = 0;
    vm->error[0] = 0;
    vm->code_len = 0;
    vm->dict_len = 0;
    vm->rsp = 0;
    vm->compiling = 0;
    vm->awaiting_name = 0;
    vm->compile_name[0] = 0;
    vm->compile_start = 0;
    vm->ctrl_sp = 0;
    vm->out = 0;
    vm->out_pos = 0;
    vm->out_max = 0;
}

/* Runs compiled code starting at code[start_ip], including any nested
 * OP_CALL_WORD calls -- the one loop shared by both "execute a
 * user-defined word typed at the prompt" and "execute a word called
 * from inside another word's body". rstack/rsp is what makes a single
 * loop correct for both: a top-level call starts with rsp wherever it
 * already was (0, if nothing else is mid-execution) and returns to
 * exactly that depth when its own OP_EXIT is reached. */
static void forth_exec(struct forth_vm *vm, int start_ip) {
    int ip = start_ip;
    int base_rsp = vm->rsp;

    for (;;) {
        struct forth_instr *instr = &vm->code[ip];

        switch (instr->op) {
        case OP_LITERAL:
            forth_push(vm, instr->arg);
            ip++;
            break;
        case OP_CALL_PRIMITIVE:
            primitives[instr->arg].fn(vm);
            ip++;
            break;
        case OP_CALL_WORD:
            if (vm->rsp >= FORTH_RSTACK_SIZE) {
                forth_set_error(vm, "RSTACK FULL");
                return;
            }
            vm->rstack[vm->rsp++] = ip + 1;
            ip = vm->dict[instr->arg].code_start;
            break;
        case OP_EXIT:
            if (vm->rsp == base_rsp) {
                return; /* back to the depth this call started at -- done */
            }
            ip = vm->rstack[--vm->rsp];
            break;
        case OP_BRANCH:
            ip = instr->arg;
            break;
        case OP_BRANCH_IF_ZERO: {
            int32_t cond;
            if (!forth_pop(vm, &cond)) {
                return;
            }
            ip = (cond == 0) ? (int)instr->arg : ip + 1;
            break;
        }
        case OP_CALL_YIELD:
            forth_hook_yield();
            ip++;
            break;
        }

        if (vm->error[0]) {
            return; /* a primitive or push/pop set an error mid-execution */
        }
    }
}

/* Appends one instruction to the shared code array, bounds-checked --
 * every compile-mode token handler goes through this rather than
 * touching vm->code[] directly. */
static void forth_emit(struct forth_vm *vm, int op, int32_t arg) {
    if (vm->code_len >= FORTH_CODE_SIZE) {
        forth_set_error(vm, "CODE FULL");
        return;
    }
    vm->code[vm->code_len].op = op;
    vm->code[vm->code_len].arg = arg;
    vm->code_len++;
}

static void ctrl_push(struct forth_vm *vm, int kind, int value) {
    if (vm->ctrl_sp >= FORTH_CTRL_STACK_SIZE) {
        forth_set_error(vm, "CTRL STACK FULL");
        return;
    }
    vm->ctrl_stack[vm->ctrl_sp].kind = kind;
    vm->ctrl_stack[vm->ctrl_sp].value = value;
    vm->ctrl_sp++;
}

/* Checks both "something is open" and "it's the kind this caller
 * expects" in one call -- IF/ELSE/THEN only ever want a CTRL_KIND_IF
 * entry, BEGIN/UNTIL only ever want CTRL_KIND_BEGIN. A caller doesn't
 * need to distinguish "nothing was open" from "the wrong thing was
 * open" -- either way the source has a stray or mismatched control
 * word, so one error message covers both. */
static int ctrl_pop(struct forth_vm *vm, int expected_kind, struct forth_ctrl_entry *entry,
                    const char *mismatch_msg) {
    if (vm->ctrl_sp <= 0 || vm->ctrl_stack[vm->ctrl_sp - 1].kind != expected_kind) {
        forth_set_error(vm, mismatch_msg);
        return 0;
    }
    vm->ctrl_sp--;
    *entry = vm->ctrl_stack[vm->ctrl_sp];
    return 1;
}

static void handle_compile_token(struct forth_vm *vm, const char *token) {
    int32_t num;
    int i;

    if (vm->awaiting_name) {
        int j = 0;
        while (token[j] && j < FORTH_WORD_NAME_MAX) {
            vm->compile_name[j] = token[j];
            j++;
        }
        vm->compile_name[j] = 0;
        vm->awaiting_name = 0;
        return;
    }

    if (str_eq_ci(token, "IF")) {
        int idx = vm->code_len;
        forth_emit(vm, OP_BRANCH_IF_ZERO, -1); /* patched by the matching ELSE or THEN */
        if (vm->error[0]) {
            return;
        }
        ctrl_push(vm, CTRL_KIND_IF, idx);
        return;
    }

    if (str_eq_ci(token, "ELSE")) {
        struct forth_ctrl_entry e;
        int idx;
        if (!ctrl_pop(vm, CTRL_KIND_IF, &e, "MISMATCHED ELSE")) {
            return;
        }
        vm->code[e.value].arg = vm->code_len + 1; /* IF's branch: skip past the unconditional one below */
        idx = vm->code_len;
        forth_emit(vm, OP_BRANCH, -1); /* skips the ELSE body when IF's condition was true; patched by THEN */
        if (vm->error[0]) {
            return;
        }
        ctrl_push(vm, CTRL_KIND_IF, idx);
        return;
    }

    if (str_eq_ci(token, "THEN")) {
        struct forth_ctrl_entry e;
        if (!ctrl_pop(vm, CTRL_KIND_IF, &e, "MISMATCHED THEN")) {
            return;
        }
        vm->code[e.value].arg = vm->code_len; /* "here" */
        return;
    }

    if (str_eq_ci(token, "BEGIN")) {
        ctrl_push(vm, CTRL_KIND_BEGIN, vm->code_len);
        return;
    }

    if (str_eq_ci(token, "UNTIL")) {
        struct forth_ctrl_entry e;
        if (!ctrl_pop(vm, CTRL_KIND_BEGIN, &e, "MISMATCHED UNTIL")) {
            return;
        }
        forth_emit(vm, OP_CALL_YIELD, 0);
        forth_emit(vm, OP_BRANCH_IF_ZERO, e.value); /* loop back if false; falls through if true */
        return;
    }

    if (str_eq_ci(token, ";")) {
        forth_emit(vm, OP_EXIT, 0);
        if (vm->error[0]) {
            return;
        }
        if (vm->ctrl_sp != 0) {
            /* An IF never closed with THEN, or a BEGIN never closed with
             * UNTIL -- registering this word anyway would mean a body
             * that branches to an unpatched (still -1) target. */
            forth_set_error(vm, "UNBALANCED CONTROL");
            return;
        }
        if (vm->dict_len >= FORTH_MAX_WORDS) {
            forth_set_error(vm, "DICT FULL");
            return;
        }
        {
            int j = 0;
            while (vm->compile_name[j] && j < FORTH_WORD_NAME_MAX) {
                vm->dict[vm->dict_len].name[j] = vm->compile_name[j];
                j++;
            }
            vm->dict[vm->dict_len].name[j] = 0;
        }
        vm->dict[vm->dict_len].code_start = vm->compile_start;
        vm->dict_len++;
        vm->compiling = 0;
        return;
    }

    if (parse_int(token, &num)) {
        forth_emit(vm, OP_LITERAL, num);
        return;
    }

    for (i = 0; i < FORTH_NUM_PRIMITIVES; i++) {
        if (str_eq_ci(token, primitives[i].name)) {
            forth_emit(vm, OP_CALL_PRIMITIVE, i);
            return;
        }
    }

    /* Only words defined strictly before this one are visible -- a
     * word can't call itself or anything defined later, since dict_len
     * hasn't grown to include them yet. Not a limitation being worked
     * around; real Forth dictionaries have always worked this way. */
    for (i = 0; i < vm->dict_len; i++) {
        if (str_eq_ci(token, vm->dict[i].name)) {
            forth_emit(vm, OP_CALL_WORD, i);
            return;
        }
    }

    forth_set_error(vm, "UNKNOWN");
}

static void handle_immediate_token(struct forth_vm *vm, const char *token) {
    int32_t num;
    int i;

    if (str_eq_ci(token, ":")) {
        vm->compiling = 1;
        vm->awaiting_name = 1;
        vm->compile_start = vm->code_len;
        return;
    }

    if (parse_int(token, &num)) {
        forth_push(vm, num);
        return;
    }

    for (i = 0; i < FORTH_NUM_PRIMITIVES; i++) {
        if (str_eq_ci(token, primitives[i].name)) {
            primitives[i].fn(vm);
            return;
        }
    }

    for (i = 0; i < vm->dict_len; i++) {
        if (str_eq_ci(token, vm->dict[i].name)) {
            forth_exec(vm, vm->dict[i].code_start);
            return;
        }
    }

    /* Not "?" -- font.c has no glyph for it (falls through to a blank
     * space), confirmed while headlessly verifying Stage B. UNKNOWN uses
     * only characters the font actually renders. */
    forth_set_error(vm, "UNKNOWN");
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

        if (vm->compiling) {
            handle_compile_token(vm, token);
        } else {
            handle_immediate_token(vm, token);
        }
    }

    if (vm->error[0]) {
        /* ABORT: reset the data stack, return stack, and any in-progress
         * compilation rather than leave partial state for the next
         * line. code_len is deliberately NOT rewound -- the half-
         * compiled instructions are simply never pointed to by a
         * dictionary entry (that's only added at a successful ';'), so
         * they're just inert, unreachable, append-only waste, not a
         * correctness problem. */
        vm->dsp = 0;
        vm->rsp = 0;
        vm->compiling = 0;
        vm->awaiting_name = 0;
        vm->ctrl_sp = 0;
        forth_write(vm, "\n");
        forth_write(vm, vm->error);
    }
}
