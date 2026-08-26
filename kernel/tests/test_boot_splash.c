/* kernel/tests/test_boot_splash.c -- host-side only, never linked into
 * kernel.bin. boot_splash.c has no freestanding-only dependencies, so
 * it compiles and runs natively here exactly as it will inside
 * kernel.bin. Guards the countdown/transition logic behind kmain()'s
 * boot-banner auto-hide, same convention as test_font.c/test_scheduler.c. */
#include <stdio.h>
#include "../gui/boot_splash.h"

static int failures = 0;

static void check(int cond, const char *msg) {
    if (!cond) {
        printf("FAIL: %s\n", msg);
        failures++;
    }
}

int main(void) {
    struct boot_splash bs;
    int i;

    boot_splash_init(&bs);
    check(boot_splash_visible(&bs), "splash starts visible right after init");

    /* Every tick before the countdown reaches zero should report no
     * transition and leave the splash visible. */
    for (i = 0; i < BOOT_SPLASH_FRAMES - 1; i++) {
        int transitioned = boot_splash_tick(&bs);
        check(!transitioned, "no transition before the countdown reaches zero");
        check(boot_splash_visible(&bs), "still visible before the countdown reaches zero");
    }

    /* The Nth tick (countdown hitting zero) must report the transition
     * exactly once and flip visibility off. */
    check(boot_splash_tick(&bs) == 1, "the tick that reaches zero reports a transition");
    check(!boot_splash_visible(&bs), "hidden immediately after the transition tick");

    /* Every tick after that must report no further transition and stay
     * hidden -- ticking a hidden splash must be a stable no-op. */
    for (i = 0; i < 50; i++) {
        int transitioned = boot_splash_tick(&bs);
        check(!transitioned, "no repeat transition after already hidden");
        check(!boot_splash_visible(&bs), "stays hidden on every later tick");
    }

    if (failures == 0) {
        printf("PASS: all boot_splash tests passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
