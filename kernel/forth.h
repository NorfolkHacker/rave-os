#ifndef RAVEOS_FORTH_H
#define RAVEOS_FORTH_H

#include <stdint.h>

/* Rave-OS's own Forth dialect. No GUI dependencies at all (no window.h,
 * no graphics.h, nothing console_*) -- this file should be reasoned
 * about entirely on its own; kernel.c is the only thing that knows a
 * Forth console window exists.
 *
 * No malloc anywhere in this kernel (confirmed before this file was
 * written), so every structure here is a fixed-size static array, sized
 * generously but arbitrarily -- comfortably safe for anything typed at
 * a REPL, not load-bearing precise. */
#define FORTH_DSTACK_SIZE 128
#define FORTH_TOKEN_MAX 32
#define FORTH_ERROR_MAX 32

/* @/! memory access: deliberately NOT raw addresses into kernel RAM --
 * this kernel has no paging, so a typo'd address typed at the console
 * would silently corrupt whatever .bss the linker happened to place
 * nearby. mem[] is its own fixed-size scratch array instead, cell-
 * indexed (0..FORTH_MEM_SIZE-1) to match dstack's int32_t cell width;
 * @/! bounds-check the index exactly like every other array access in
 * this file, so an out-of-range address is a Forth-level error, never
 * memory corruption. */
#define FORTH_MEM_SIZE 64

/* Stage C: user-defined words (':'/';'), compiled into a shared,
 * append-only threaded-code array -- the same technique classic Forth
 * implementations use, minus the assembly-level indirect-threading
 * trick. A word can only call words already defined earlier (the
 * dictionary is searched up to, but not including, the definition
 * currently being compiled) -- not a limitation, the same rule real
 * Forth dictionaries have always had. */
#define FORTH_MAX_WORDS 64
#define FORTH_CODE_SIZE 2048
#define FORTH_WORD_NAME_MAX 16
#define FORTH_RSTACK_SIZE 32

/* Stage D: control flow (IF/ELSE/THEN, BEGIN/UNTIL), compiled as two
 * more instruction kinds -- an unconditional jump and a
 * pop-and-jump-if-zero -- into the same code[] array Stage C's word
 * bodies already use. No new opcode is needed for BEGIN/UNTIL beyond
 * what IF already needed; a loop-back branch is just a
 * branch-if-zero whose target happens to be earlier in code[] than
 * where it's emitted. */
enum forth_op { OP_LITERAL, OP_CALL_PRIMITIVE, OP_CALL_WORD, OP_EXIT, OP_BRANCH, OP_BRANCH_IF_ZERO, OP_CALL_YIELD };

/* Compile-time-only bookkeeping for control-flow words: a small stack
 * of not-yet-resolved branches (IF/ELSE, kind CTRL_KIND_IF -- code[]
 * index of an instruction whose arg still needs patching once THEN/ELSE
 * is seen) and loop-back targets (BEGIN, kind CTRL_KIND_BEGIN -- a
 * code[] position used directly, no patching needed since the branch
 * that jumps there doesn't exist until UNTIL). Tagged with `kind`
 * rather than left as bare ints so a mismatched pair (e.g. a stray
 * "BEGIN ... THEN") is caught as an error at compile time instead of
 * silently branching to the wrong place -- the same reasoning behind
 * every other bounds/consistency check in this file. */
#define FORTH_CTRL_STACK_SIZE 8
#define CTRL_KIND_IF 0
#define CTRL_KIND_BEGIN 1

struct forth_ctrl_entry {
    int kind;
    int value;
};

struct forth_instr {
    int op;
    int32_t arg; /* literal value, primitive index, or user-word index, depending on op */
};

struct forth_dict_entry {
    char name[FORTH_WORD_NAME_MAX + 1];
    int code_start;
};

struct forth_vm {
    int32_t dstack[FORTH_DSTACK_SIZE];
    int dsp; /* 0 = empty */
    char error[FORTH_ERROR_MAX]; /* empty string = no error */

    int32_t mem[FORTH_MEM_SIZE]; /* @/! scratch array, see FORTH_MEM_SIZE comment above */

    struct forth_instr code[FORTH_CODE_SIZE];
    int code_len; /* next free slot -- append-only, never reclaimed */
    struct forth_dict_entry dict[FORTH_MAX_WORDS];
    int dict_len;
    int32_t rstack[FORTH_RSTACK_SIZE]; /* nested word-call return addresses */
    int rsp;

    /* Compile-mode state, persists across forth_eval_line() calls (not
     * reset each call, unlike out/out_pos/out_max below) so a
     * definition can legally span more than one submitted line -- Enter
     * mid-definition just means "keep compiling on the next line",
     * exactly like a real interactive Forth. */
    int compiling;     /* 1 between ':' and its matching ';' */
    int awaiting_name; /* 1 for exactly the one token right after ':' */
    char compile_name[FORTH_WORD_NAME_MAX + 1];
    int compile_start; /* code_len at the moment ':' was seen */

    /* Also compile-mode-only state, same persist-across-calls reasoning
     * as compiling/awaiting_name above -- an open IF or BEGIN can
     * legally still be pending when a line ends. */
    struct forth_ctrl_entry ctrl_stack[FORTH_CTRL_STACK_SIZE];
    int ctrl_sp;

    /* Transient per-eval output cursor, not persistent Forth state --
     * primitives like '.' and CR need somewhere to write, and every
     * primitive shares the same void(*)(struct forth_vm *) signature (so
     * a compiled word body can call one by table index), so the output
     * cursor rides along on the vm struct itself rather than threading
     * an extra parameter through every primitive. Reset at the start of
     * every forth_eval_line() call. */
    char *out;
    int out_pos;
    int out_max;
};

void forth_init(struct forth_vm *vm);

/* Tokenizes and executes (or compiles, inside a ':'/';' definition) one
 * line. Writes human-readable output into out[] (NUL-terminated,
 * '\n'-separated per logical output line -- forth.c never touches
 * console_output.h; the caller splits out[] on '\n' and appends each
 * segment as its own console line). On error (unknown word, stack
 * underflow/overflow, divide by zero, dictionary/code-space full),
 * execution of the rest of the line stops, the data stack and any
 * in-progress compilation are reset (real Forth's ABORT behavior --
 * simpler to reason about than leaving a partially-consumed stack or a
 * half-compiled word for the next line), and an error message is
 * appended to out[]. */
void forth_eval_line(struct forth_vm *vm, const char *line, char *out, int out_max);

#endif
