#include "boot_splash.h"

void boot_splash_init(struct boot_splash *bs) {
    bs->frames_left = BOOT_SPLASH_FRAMES;
    bs->hidden = 0;
}

int boot_splash_tick(struct boot_splash *bs) {
    if (bs->hidden) {
        return 0;
    }
    bs->frames_left--;
    if (bs->frames_left <= 0) {
        bs->hidden = 1;
        return 1;
    }
    return 0;
}
