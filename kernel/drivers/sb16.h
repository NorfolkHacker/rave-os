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

#endif
