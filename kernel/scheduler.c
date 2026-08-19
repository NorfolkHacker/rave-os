#include "scheduler.h"
#include <stdint.h>

#define PROGRAM_STACK_SIZE 8192
#define STACK_CANARY 0xC0FFEEu

enum program_state { PROGRAM_EMPTY, PROGRAM_RESERVED, PROGRAM_READY };

struct program {
    uint32_t saved_esp;
    enum program_state state;
    const char *name;
    void (*entry)(void *arg);
    void *arg;
    int close_requested_frame; /* -1 = no close requested */
};

static uint8_t program_stacks[MAX_PROGRAMS][PROGRAM_STACK_SIZE];
static struct program programs[MAX_PROGRAMS];
static uint32_t kernel_esp;
static int running_slot = -1;

extern void context_switch(uint32_t *save_esp_here, uint32_t new_esp);

static uint32_t *stack_canary_ptr(int slot) {
    return (uint32_t *)&program_stacks[slot][0];
}

/* "Returned into" by the first context_switch() into a freshly
 * activated slot (see scheduler_activate()'s priming below) -- runs
 * the program's real entry point, then marks the slot done and
 * switches back to the kernel side for good. running_slot is already
 * correct by the time this runs: scheduler_tick() sets it immediately
 * before the context_switch() call that lands here. */
static void program_trampoline(void) {
    struct program *self = &programs[running_slot];
    self->entry(self->arg);
    self->state = PROGRAM_EMPTY;
    context_switch(&self->saved_esp, kernel_esp); /* never returns */
}

int scheduler_reserve(const char *name) {
    int i;
    for (i = 0; i < MAX_PROGRAMS; i++) {
        if (programs[i].state == PROGRAM_EMPTY) {
            programs[i].state = PROGRAM_RESERVED;
            programs[i].name = name;
            programs[i].close_requested_frame = -1;
            *stack_canary_ptr(i) = STACK_CANARY;
            return i;
        }
    }
    return -1;
}

void scheduler_activate(int slot, void (*entry)(void *arg), void *arg) {
    uint32_t top;
    uint32_t *frame;

    /* Bounds-check slot to prevent out-of-bounds write into programs[] and program_stacks[] */
    if (slot < 0 || slot >= MAX_PROGRAMS) {
        return;
    }

    programs[slot].entry = entry;
    programs[slot].arg = arg;

    /* The first context_switch() into this slot must "return" into
     * program_trampoline() rather than resuming a real prior call.
     * context_switch()'s epilogue pops 4 registers then ret's, so the
     * primed stack (low to high address) must be: 4 dummy
     * callee-saved register values, then the return address --
     * exactly what a real context_switch() call would have left
     * behind, just hand-built instead of pushed by real code. */
    top = (uint32_t)&program_stacks[slot][PROGRAM_STACK_SIZE];
    top &= ~0xFu;
    frame = ((uint32_t *)top) - 5;
    frame[0] = 0; /* ebx */
    frame[1] = 0; /* esi */
    frame[2] = 0; /* edi */
    frame[3] = 0; /* ebp */
    frame[4] = (uint32_t)program_trampoline;
    programs[slot].saved_esp = (uint32_t)frame;
    programs[slot].state = PROGRAM_READY;
}

void scheduler_yield(void) {
    struct program *self = &programs[running_slot];
    context_switch(&self->saved_esp, kernel_esp);
}

void scheduler_tick(void) {
    int i;
    for (i = 0; i < MAX_PROGRAMS; i++) {
        if (programs[i].state != PROGRAM_READY) {
            continue;
        }

        if (programs[i].close_requested_frame >= 0) {
            programs[i].close_requested_frame++;
            if (programs[i].close_requested_frame > SCHEDULER_CLOSE_GRACE_FRAMES) {
                programs[i].state = PROGRAM_EMPTY;
                continue;
            }
        }

        if (*stack_canary_ptr(i) != STACK_CANARY) {
            /* Stack overflow smashed into the canary -- can't safely
             * resume this slot. No paging means no way to prevent the
             * corruption itself, only to stop compounding it. */
            programs[i].state = PROGRAM_EMPTY;
            continue;
        }

        running_slot = i;
        context_switch(&kernel_esp, programs[i].saved_esp);
        running_slot = -1;
    }
}

int scheduler_current_slot(void) {
    return running_slot;
}

void scheduler_request_close(int slot) {
    /* Bounds-check slot to prevent out-of-bounds read/write into programs[] */
    if (slot < 0 || slot >= MAX_PROGRAMS) {
        return;
    }
    if (programs[slot].state == PROGRAM_READY && programs[slot].close_requested_frame < 0) {
        programs[slot].close_requested_frame = 0;
    }
}

int scheduler_any_active(void) {
    int i;
    for (i = 0; i < MAX_PROGRAMS; i++) {
        if (programs[i].state != PROGRAM_EMPTY) {
            return 1;
        }
    }
    return 0;
}

/* Test-only helper: corrupt the canary of a given slot. Used by test_scheduler.c
 * to verify that canary corruption is detected and the slot is force-freed. */
void scheduler_test_corrupt_stack(int slot) {
    if (slot >= 0 && slot < MAX_PROGRAMS) {
        *stack_canary_ptr(slot) = 0xDEADBEEFu;
    }
}
