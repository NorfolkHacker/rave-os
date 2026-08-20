/* kernel/tests/test_scheduler.c -- host-side only. scheduler.c has no
 * freestanding-only dependencies, so it compiles and runs natively
 * here exactly as it will inside kernel.bin. */
#include <stdio.h>
#include "../sched/scheduler.h"

/* Test-only helper from scheduler.c */
extern void scheduler_test_corrupt_stack(int slot);

static int counter_a;
static int counter_b;
static int counter_loop;
static int counter_canary_test;

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

/* Fiber that loops forever (for close-timeout test) */
static void fiber_loop_forever(void *arg) {
    (void)arg;
    while (1) {
        counter_loop++;
        scheduler_yield();
    }
}

/* Fiber that increments counter (for canary test) */
static void fiber_canary_test(void *arg) {
    (void)arg;
    counter_canary_test++;
    scheduler_yield();
}

int main(void) {
    int slot_a, slot_b, slot_loop, slot_canary, i;

    /* Test 1: Happy-path round-robin scheduling */
    slot_a = scheduler_reserve("A");
    scheduler_activate(slot_a, fiber_a, 0);
    slot_b = scheduler_reserve("B");
    scheduler_activate(slot_b, fiber_b, 0);

    for (i = 0; i < 4; i++) {
        scheduler_tick();
    }

    if (counter_a != 3 || counter_b != 3) {
        printf("FAIL test1: counter_a=%d counter_b=%d (expected 3, 3)\n", counter_a, counter_b);
        return 1;
    }
    if (scheduler_current_slot() != -1) {
        printf("FAIL test1: scheduler_current_slot() should be -1 between ticks, got %d\n",
               scheduler_current_slot());
        return 1;
    }
    if (scheduler_any_active()) {
        printf("FAIL test1: scheduler_any_active() should be 0 once both fibers finished\n");
        return 1;
    }

    /* Test 2: Close-request grace timeout mechanism */
    counter_loop = 0;
    slot_loop = scheduler_reserve("LOOP");
    scheduler_activate(slot_loop, fiber_loop_forever, 0);

    /* Run for 1 tick to let fiber_loop_forever start */
    scheduler_tick();
    if (counter_loop != 1) {
        printf("FAIL test2: counter_loop should be 1 after first tick, got %d\n", counter_loop);
        return 1;
    }
    if (!scheduler_any_active()) {
        printf("FAIL test2: scheduler_any_active() should be 1 while loop fiber is running\n");
        return 1;
    }

    /* Request close and verify fiber still runs for SCHEDULER_CLOSE_GRACE_FRAMES ticks */
    scheduler_request_close(slot_loop);
    for (i = 0; i < SCHEDULER_CLOSE_GRACE_FRAMES; i++) {
        scheduler_tick();
    }
    /* After exactly 60 more ticks, fiber should still be active (counter_loop incremented) */
    if (counter_loop != 1 + SCHEDULER_CLOSE_GRACE_FRAMES) {
        printf("FAIL test2: counter_loop should be %d after 60 grace ticks, got %d\n",
               1 + SCHEDULER_CLOSE_GRACE_FRAMES, counter_loop);
        return 1;
    }
    if (!scheduler_any_active()) {
        printf("FAIL test2: scheduler_any_active() should still be 1 after %d grace ticks\n",
               SCHEDULER_CLOSE_GRACE_FRAMES);
        return 1;
    }

    /* On the 61st tick after close request, the fiber should be force-freed */
    scheduler_tick();
    if (scheduler_any_active()) {
        printf("FAIL test2: scheduler_any_active() should be 0 after force-free timeout\n");
        return 1;
    }

    /* Test 3: Stack canary corruption detection and force-free */
    counter_canary_test = 0;
    slot_canary = scheduler_reserve("CANARY");
    scheduler_activate(slot_canary, fiber_canary_test, 0);

    /* Corrupt the canary before the fiber runs */
    scheduler_test_corrupt_stack(slot_canary);

    /* Tick: canary check should fail and force-free without calling fiber_canary_test */
    scheduler_tick();

    if (counter_canary_test != 0) {
        printf("FAIL test3: fiber_canary_test should not have run (counter should be 0), got %d\n",
               counter_canary_test);
        return 1;
    }
    if (scheduler_any_active()) {
        printf("FAIL test3: scheduler_any_active() should be 0 after canary force-free\n");
        return 1;
    }

    /* Test 4: Bounds-check of scheduler_activate and scheduler_request_close */
    /* Calling with invalid slots should not crash or corrupt state */
    scheduler_activate(-1, fiber_a, 0);
    scheduler_activate(MAX_PROGRAMS, fiber_a, 0);
    scheduler_request_close(-1);
    scheduler_request_close(MAX_PROGRAMS);

    /* Verify we can still reserve and run a normal program afterward */
    counter_a = 0;
    slot_a = scheduler_reserve("BOUNDS_CHECK");
    scheduler_activate(slot_a, fiber_a, 0);
    scheduler_tick();
    scheduler_tick();
    scheduler_tick();
    scheduler_tick();

    if (counter_a != 3) {
        printf("FAIL test4: counter_a should be 3 after bounds-check test, got %d\n", counter_a);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
