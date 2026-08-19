/* kernel/tests/test_context_switch.c -- host-side only, never linked
 * into kernel.bin. Proves context_switch() both (a) can "start" a
 * hand-primed stack (the same priming scheduler.c's
 * scheduler_activate() will do) and (b) can yield back and forth
 * between two contexts repeatedly. */
#include <stdio.h>
#include <stdint.h>

extern void context_switch(uint32_t *save_esp_here, uint32_t new_esp);

#define STACK_SIZE 4096
static uint8_t fiber_stack[STACK_SIZE];
static uint32_t main_esp;
static uint32_t fiber_esp;
static int step;

static void fiber_entry(void) {
    step = 1;
    context_switch(&fiber_esp, main_esp);
    step = 3;
    context_switch(&fiber_esp, main_esp);
    for (;;) { } /* never reached by this test */
}

int main(void) {
    uint32_t top;
    uint32_t *frame;

    top = (uint32_t)(fiber_stack + STACK_SIZE);
    top &= ~0xFu;
    frame = ((uint32_t *)top) - 5; /* ebx, esi, edi, ebp, return-address */
    frame[0] = 0;
    frame[1] = 0;
    frame[2] = 0;
    frame[3] = 0;
    frame[4] = (uint32_t)fiber_entry;
    fiber_esp = (uint32_t)frame;

    step = 0;
    context_switch(&main_esp, fiber_esp);
    if (step != 1) {
        printf("FAIL: expected step==1 after first switch, got %d\n", step);
        return 1;
    }

    step = 2;
    context_switch(&main_esp, fiber_esp);
    if (step != 3) {
        printf("FAIL: expected step==3 after second switch, got %d\n", step);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
