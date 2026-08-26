#ifndef RAVEOS_BOOT_SPLASH_H
#define RAVEOS_BOOT_SPLASH_H

/* How long the "RAVE-OS" boot banner stays on screen before hiding
 * itself. This kernel has no PIT/timer driver (see docs/IDEAS.md's
 * 2026-08-26 floating-point/userspace entries), so boot_splash_tick()
 * counts kmain()'s own main-loop iterations as a rough proxy for
 * elapsed time rather than real wall-clock seconds -- and the main
 * loop deliberately skips its usual idle `hlt` for as long as the
 * splash is visible (see kmain()), so this counts CPU-bound spins, not
 * real frames.
 *
 * There is no way to pin this to real wall-clock time without a PIT
 * driver, and headless QEMU measurement on 2026-08-26 hardware (under
 * `-accel kvm`, what `boot/Makefile`'s `run` target actually uses)
 * showed the achievable iteration rate swinging by an order of
 * magnitude run to run -- host CPU frequency scaling under sustained
 * 100%-core spin, not measurement noise (`scaling_cur_freq` sampled as
 * low as 800MHz mid-run on an otherwise-idle host). This value is
 * picked to land in the right ballpark (low single-digit seconds) across
 * that swing, not tuned to an exact duration. */
#define BOOT_SPLASH_FRAMES 100000000

struct boot_splash {
    int frames_left;
    int hidden;
};

void boot_splash_init(struct boot_splash *bs);

/* Call once per kmain() main-loop iteration. Returns 1 exactly on the
 * one call where the countdown reaches zero and the splash transitions
 * from visible to hidden -- the caller needs that signal to force a
 * one-time redraw over the banner's old screen rect. Returns 0 on every
 * other call, including every call after it's already hidden. */
int boot_splash_tick(struct boot_splash *bs);

static inline int boot_splash_visible(const struct boot_splash *bs) {
    return !bs->hidden;
}

#endif
