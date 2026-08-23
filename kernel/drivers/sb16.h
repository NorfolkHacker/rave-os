#ifndef RAVEOS_SB16_H
#define RAVEOS_SB16_H

/* Sound Blaster 16 driver: DSP reset/detect, and (Task 2) one-shot 8-bit
 * PCM playback via the 8237 DMA controller's channel 1. Hardcodes the
 * classic SB16 defaults (I/O base 0x220, IRQ 5, 8-bit DMA channel 1) that
 * QEMU's -device sb16 and every real ISA SB16 assume -- see
 * docs/superpowers/specs/2026-08-22-sb16-audio-driver-design.md. */

/* Resets and detects the card (DSP reset handshake, expects the card to
 * hand back 0xAA). Returns 0 if a card responded, -1 otherwise. Call once,
 * from kmain(), after interrupts_init()/interrupts_enable() -- mirrors
 * ata_selftest()'s own placement. Nothing else in this driver does
 * anything useful until this has been called and returned 0. */
int sb16_init(void);

/* Plays buf (len bytes of 8-bit unsigned PCM, 128 = silence) once via a
 * single-cycle DMA transfer at the given sample_rate. buf must not cross
 * a 64KB physical address boundary -- give it `__attribute__((aligned(N)))`
 * for some power-of-two N >= len (an N-aligned address plus a run of at
 * most N bytes can never straddle a 64KB line, since 65536 is itself a
 * multiple of every power-of-two N used here). No-op if sb16_init() never
 * found a card, or if len is 0. Fire-and-forget: returns immediately:
 * playback continues in the background and the card raises IRQ5 on
 * completion (arch/isr.c's handler calls sb16_irq_ack()). */
void sb16_play_buffer(const unsigned char *buf, unsigned int len, unsigned int sample_rate);

/* Called by arch/isr.c's IRQ5 handler once playback finishes -- reads the
 * DSP's own interrupt-acknowledge port so the card can raise IRQ5 again on
 * the next sb16_play_buffer() call. Nothing else needs calling this. */
void sb16_irq_ack(void);

/* Programs the 8237 DMA controller and SB16 DSP for continuous
 * auto-init (looping) 8-bit playback over a double buffer: buf must be
 * 2*half_len bytes, and (same 64KB-boundary rule as sb16_play_buffer())
 * must never cross a 64KB physical boundary -- give it
 * __attribute__((aligned(N))) for a power-of-two N >= 2*half_len.
 * No-op if sb16_init() never found a card, half_len is 0, or a stream
 * is already running (only one stream at a time -- this driver has no
 * way to stop one once started). Once running, IRQ5 fires once per
 * half-buffer completion -- see sb16_stream_needs_refill(). */
void sb16_start_stream(unsigned char *buf, unsigned int half_len, unsigned int sample_rate);

/* Polls whether a buffer half needs new samples written into it before
 * the card catches up and plays stale data again. Returns 1 and fills
 * *buf_out and *len_out (pointing at exactly the half that just finished
 * playing) if so, 0 otherwise. Call once per kmain() frame; if it
 * returns 1, render fresh samples into *buf_out and then call
 * sb16_stream_refill_done(). */
int sb16_stream_needs_refill(unsigned char **buf_out, unsigned int *len_out);

/* Clears the flag sb16_stream_needs_refill() set. Must be called after
 * every refill, or the same half keeps being reported as needing
 * refill (harmless -- it just re-renders redundantly -- but wasteful). */
void sb16_stream_refill_done(void);

#endif
