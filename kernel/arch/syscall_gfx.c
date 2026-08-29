#include "syscall.h"
#include "graphics.h"

/* kernel.c-private, declared extern directly at the call site, matching
 * this codebase's existing convention (window_ring3_open()/
 * program_load_and_run()/ring3_wait_event() all do the same). Mirrors
 * every write these two syscalls make into a shadow buffer so
 * WIN_KIND_RING3's content can survive a redraw it didn't cause -- see
 * docs/superpowers/specs/2026-08-29-ring3-window-content-persistence-design.md. */
extern void ring3_shadow_put_pixel(int x, int y, unsigned int rgb);
extern void ring3_shadow_fill_rect(int x, int y, int w, int h, unsigned int rgb);

int syscall_dispatch_gfx(int num, int arg) {
    if (num == SYS_GFX_WIDTH) {
        return gfx_width();
    }
    if (num == SYS_GFX_HEIGHT) {
        return gfx_height();
    }
    if (num == SYS_GFX_CLEAR) {
        gfx_clear((unsigned int)arg);
        return 0;
    }
    if (num == SYS_GFX_PUT_PIXEL) {
        const struct sys_gfx_put_pixel_args *a = (const struct sys_gfx_put_pixel_args *)arg;
        gfx_put_pixel(a->x, a->y, a->rgb);
        ring3_shadow_put_pixel(a->x, a->y, a->rgb);
        return 0;
    }
    if (num == SYS_GFX_FILL_RECT) {
        const struct sys_gfx_fill_rect_args *a = (const struct sys_gfx_fill_rect_args *)arg;
        gfx_fill_rect(a->x, a->y, a->w, a->h, a->rgb);
        ring3_shadow_fill_rect(a->x, a->y, a->w, a->h, a->rgb);
        return 0;
    }
    if (num == SYS_GFX_PRESENT_RECT) {
        const struct sys_gfx_present_rect_args *a = (const struct sys_gfx_present_rect_args *)arg;
        gfx_present_rect(a->x, a->y, a->w, a->h);
        return 0;
    }
    return syscall_dispatch_audio(num, arg);
}
