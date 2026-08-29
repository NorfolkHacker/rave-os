#include "syscall.h"
#include "graphics.h"

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
        return 0;
    }
    if (num == SYS_GFX_FILL_RECT) {
        const struct sys_gfx_fill_rect_args *a = (const struct sys_gfx_fill_rect_args *)arg;
        gfx_fill_rect(a->x, a->y, a->w, a->h, a->rgb);
        return 0;
    }
    if (num == SYS_GFX_PRESENT_RECT) {
        const struct sys_gfx_present_rect_args *a = (const struct sys_gfx_present_rect_args *)arg;
        gfx_present_rect(a->x, a->y, a->w, a->h);
        return 0;
    }
    return syscall_dispatch_core(num, arg);
}
