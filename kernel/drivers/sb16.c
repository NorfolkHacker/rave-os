/* Sound Blaster 16 driver. See sb16.h. Hardcodes I/O base 0x220 (QEMU
 * -device sb16 and every real ISA SB16's default) -- no PnP/autodetection,
 * same "this kernel assumes fixed hardware" precedent as the one VBE mode
 * and PS/2-only input elsewhere in this codebase. */

#include "sb16.h"
#include "io.h"
#include "serial.h"

#define SB16_BASE 0x220

#define SB16_DSP_RESET 0x226        /* SB16_BASE + 0x6 */
#define SB16_DSP_READ 0x22A         /* SB16_BASE + 0xA */
#define SB16_DSP_WRITE 0x22C        /* SB16_BASE + 0xC -- also the write-buffer-status port when read */
#define SB16_DSP_READ_STATUS 0x22E  /* SB16_BASE + 0xE -- bit 7 = data available; reading it also acks the 8-bit IRQ */

#define SB16_TIMEOUT_ITERS 100000 /* same "fail fast, don't hang forever" convention as ata.c's ATA_TIMEOUT_ITERS */

static int sb16_present = 0;

/* Same 4-dummy-reads settle trick ata.c's ata_delay_400ns() uses, and the
 * same single outb(0x80, 0) trick pic.c's io_wait() uses (an unused port,
 * so the write has no side effect beyond costing a few microseconds on
 * real hardware) -- the DSP reset pulse needs to be held for a few
 * microseconds, not released immediately. */
static void sb16_io_wait(void) {
    outb(0x80, 0);
}

static int sb16_wait_read_ready(void) {
    int i;
    for (i = 0; i < SB16_TIMEOUT_ITERS; i++) {
        if (inb(SB16_DSP_READ_STATUS) & 0x80) {
            return 0;
        }
    }
    return -1;
}

int sb16_init(void) {
    int i;

    outb(SB16_DSP_RESET, 1);
    for (i = 0; i < 4; i++) {
        sb16_io_wait();
    }
    outb(SB16_DSP_RESET, 0);

    if (sb16_wait_read_ready() != 0) {
        serial_write_str("SB16: NOT FOUND\n");
        sb16_present = 0;
        return -1;
    }
    if (inb(SB16_DSP_READ) != 0xAA) {
        serial_write_str("SB16: NOT FOUND\n");
        sb16_present = 0;
        return -1;
    }

    serial_write_str("SB16: OK\n");
    sb16_present = 1;
    return 0;
}
