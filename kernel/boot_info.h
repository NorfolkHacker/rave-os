#ifndef RAVEOS_BOOT_INFO_H
#define RAVEOS_BOOT_INFO_H

#include <stdint.h>

/* Must match BOOT_INFO_ADDR in ../boot/stage2.asm -- that's where stage2
 * (still in real mode, with BIOS VBE calls available) writes the video
 * mode details it queried, since the kernel has no other way to find out
 * where the linear framebuffer actually lives. Paging is off and
 * segmentation is flat, so this is just read as a plain physical address. */
#define BOOT_INFO_ADDR 0x9500

struct boot_info {
    uint32_t framebuffer_addr;
    uint16_t pitch;   /* bytes per scanline */
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
} __attribute__((packed));

#define boot_info ((const struct boot_info *)BOOT_INFO_ADDR)

#endif
