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

/* 8237 DMA controller #1 (channels 0-3, 8-bit). Channel 1 is what QEMU's
 * -device sb16 and every real SB16 default to for 8-bit playback. */
#define DMA1_MASK_REG 0x0A
#define DMA1_CLEAR_FF_REG 0x0C
#define DMA1_MODE_REG 0x0B
#define DMA1_CHAN1_ADDR_REG 0x02
#define DMA1_CHAN1_COUNT_REG 0x03
#define DMA1_CHAN1_PAGE_REG 0x83

#define DMA1_CHAN1_MASK_SET 0x05   /* bit2 (mask) | channel 1 */
#define DMA1_CHAN1_MASK_CLEAR 0x01 /* channel 1, mask bit clear */
/* Mode byte 0x49 = 0100_1001: bits6-7 = 01 (single-cycle mode),
 * bit4 = 0 (no auto-init), bit5 = 0 (address increment), bits2-3 = 10
 * ("read" transfer -- the DMA controller reads memory and writes the
 * device, i.e. playback), bits0-1 = 01 (channel 1). */
#define DMA1_CHAN1_MODE_SINGLE_READ 0x49
/* Same as above but bit4 = 1 (auto-init: the DMA controller reloads
 * its original base address/count and keeps going instead of stopping
 * at terminal count) -- this is what makes the double-buffer scheme
 * below loop forever without software reprogramming DMA each time. */
#define DMA1_CHAN1_MODE_AUTOINIT_READ 0x59

#define SB16_CMD_SPEAKER_ON 0xD1
#define SB16_CMD_SET_TIME_CONSTANT 0x40
#define SB16_CMD_8BIT_SINGLE_CYCLE_OUTPUT 0x14
#define SB16_CMD_SET_BLOCK_SIZE 0x48
#define SB16_CMD_8BIT_AUTOINIT_OUTPUT 0x1C

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

static int sb16_wait_write_ready(void) {
    int i;
    for (i = 0; i < SB16_TIMEOUT_ITERS; i++) {
        if (!(inb(SB16_DSP_WRITE) & 0x80)) {
            return 0;
        }
    }
    return -1;
}

/* Bounded wait, then write regardless -- same "report failure, don't hang
 * forever" discipline as ata.c, but there's no caller-visible error path
 * once sb16_init() has already succeeded, so a stuck DSP just gets a
 * best-effort write instead of freezing the kernel. */
static void sb16_dsp_write(unsigned char val) {
    sb16_wait_write_ready();
    outb(SB16_DSP_WRITE, val);
}

static void dma_program_channel1(unsigned int phys_addr, unsigned int len, unsigned char mode_byte) {
    unsigned int count = len - 1;

    outb(DMA1_MASK_REG, DMA1_CHAN1_MASK_SET);
    outb(DMA1_CLEAR_FF_REG, 0);
    outb(DMA1_MODE_REG, mode_byte);

    outb(DMA1_CHAN1_ADDR_REG, (unsigned char)(phys_addr & 0xFF));
    outb(DMA1_CHAN1_ADDR_REG, (unsigned char)((phys_addr >> 8) & 0xFF));
    outb(DMA1_CHAN1_PAGE_REG, (unsigned char)((phys_addr >> 16) & 0xFF));

    outb(DMA1_CLEAR_FF_REG, 0);
    outb(DMA1_CHAN1_COUNT_REG, (unsigned char)(count & 0xFF));
    outb(DMA1_CHAN1_COUNT_REG, (unsigned char)((count >> 8) & 0xFF));

    outb(DMA1_MASK_REG, DMA1_CHAN1_MASK_CLEAR);
}

void sb16_play_buffer(const unsigned char *buf, unsigned int len, unsigned int sample_rate) {
    unsigned int count;
    unsigned char time_constant;

    if (!sb16_present || len == 0) {
        return;
    }

    dma_program_channel1((unsigned int)(unsigned long)buf, len, DMA1_CHAN1_MODE_SINGLE_READ);

    sb16_dsp_write(SB16_CMD_SPEAKER_ON);

    /* Classic DSP 1.xx-compatible time-constant formula -- every SB16
     * still honors it. Valid for the mono 8-bit rates this driver uses. */
    time_constant = (unsigned char)(256 - (1000000 / sample_rate));
    sb16_dsp_write(SB16_CMD_SET_TIME_CONSTANT);
    sb16_dsp_write(time_constant);

    count = len - 1;
    sb16_dsp_write(SB16_CMD_8BIT_SINGLE_CYCLE_OUTPUT);
    sb16_dsp_write((unsigned char)(count & 0xFF));
    sb16_dsp_write((unsigned char)((count >> 8) & 0xFF));
}

static unsigned char *stream_buf_base = 0;
static unsigned int stream_half_len = 0;
static int stream_active = 0;
static volatile int stream_refill_flag = 0;
static volatile int stream_refill_half = 0;
static int stream_next_half_to_refill = 0;

void sb16_start_stream(unsigned char *buf, unsigned int half_len, unsigned int sample_rate) {
    unsigned char time_constant;
    unsigned int block_count;

    if (!sb16_present || half_len == 0 || stream_active) {
        return;
    }

    stream_buf_base = buf;
    stream_half_len = half_len;
    stream_refill_flag = 0;
    stream_next_half_to_refill = 0;

    dma_program_channel1((unsigned int)(unsigned long)buf, half_len * 2u, DMA1_CHAN1_MODE_AUTOINIT_READ);

    sb16_dsp_write(SB16_CMD_SPEAKER_ON);

    /* Same time-constant formula sb16_play_buffer() already uses. */
    time_constant = (unsigned char)(256 - (1000000 / sample_rate));
    sb16_dsp_write(SB16_CMD_SET_TIME_CONSTANT);
    sb16_dsp_write(time_constant);

    /* Auto-init playback needs the DSP's own block size set BEFORE the
     * auto-init command is sent -- this is what makes it raise IRQ5
     * once per half_len bytes instead of once per full loop. */
    block_count = half_len - 1;
    sb16_dsp_write(SB16_CMD_SET_BLOCK_SIZE);
    sb16_dsp_write((unsigned char)(block_count & 0xFF));
    sb16_dsp_write((unsigned char)((block_count >> 8) & 0xFF));

    sb16_dsp_write(SB16_CMD_8BIT_AUTOINIT_OUTPUT);

    stream_active = 1;
}

int sb16_stream_needs_refill(unsigned char **buf_out, unsigned int *len_out) {
    if (!stream_refill_flag) {
        return 0;
    }
    *buf_out = stream_buf_base + (stream_refill_half ? stream_half_len : 0u);
    *len_out = stream_half_len;
    return 1;
}

void sb16_stream_refill_done(void) {
    stream_refill_flag = 0;
}

void sb16_irq_ack(void) {
    inb(SB16_DSP_READ_STATUS); /* reading this port is what acks the 8-bit IRQ */
    if (stream_active) {
        /* DMA always starts by playing from the base address (half 0)
         * first, so the first IRQ corresponds to half 0 finishing --
         * this toggle stays in lockstep with that from then on. */
        stream_refill_flag = 1;
        stream_refill_half = stream_next_half_to_refill;
        stream_next_half_to_refill ^= 1;
    }
}
