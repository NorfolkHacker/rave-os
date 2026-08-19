/* kernel/tests/test_scheduler.c -- host-side only. scheduler.c has no
 * freestanding-only dependencies, so it compiles and runs natively
 * here exactly as it will inside kernel.bin. */
#include <stdio.h>
#include "../scheduler.h"

static int counter_a;
static int counter_b;

static void fiber_a(void *arg) {
    int i;
    (void)arg;
    for (i = 0; i < 3; i++) {
        counter_a++;
        scheduler_yield();
    }
}

static void fiber_b(void *arg) {
    int i;
    (void)arg;
    for (i = 0; i < 3; i++) {
        counter_b++;
        scheduler_yield();
    }
}

int main(void) {
    int slot_a, slot_b, i;

    slot_a = scheduler_reserve("A");
    scheduler_activate(slot_a, fiber_a, 0);
    slot_b = scheduler_reserve("B");
    scheduler_activate(slot_b, fiber_b, 0);

    for (i = 0; i < 4; i++) {
        scheduler_tick();
    }

    if (counter_a != 3 || counter_b != 3) {
        printf("FAIL: counter_a=%d counter_b=%d (expected 3, 3)\n", counter_a, counter_b);
        return 1;
    }
    if (scheduler_current_slot() != -1) {
        printf("FAIL: scheduler_current_slot() should be -1 between ticks, got %d\n",
               scheduler_current_slot());
        return 1;
    }
    if (scheduler_any_active()) {
        printf("FAIL: scheduler_any_active() should be 0 once both fibers finished\n");
        return 1;
    }

    printf("PASS\n");
    return 0;
}
