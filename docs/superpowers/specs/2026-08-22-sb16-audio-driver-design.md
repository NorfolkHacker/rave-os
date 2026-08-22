# A minimal Sound Blaster 16 driver: proving real audio output

## Purpose

`docs/IDEAS.md`'s "Audio" item wants an 8-channel SID-like software
synthesizer eventually, but this kernel has never produced a single
sound and has never programmed DMA or enumerated a device beyond PS/2
keyboard/mouse and PIO-only ATA disk access. Real multi-voice
synthesis needs a PCM sample stream mixed in software and played
through real audio hardware -- a PC speaker (a single on/off square
wave) cannot do it at all. Decomposed with the user into independent
sub-projects: (A) a hardware audio output path, (B) a software
synthesizer, (C) a control surface (eventually a real language talking
to it, explicitly a much later project). This spec covers (A) alone:
prove this kernel can make real sound come out of real (emulated)
hardware, with no synthesis yet.

## Scope

**In scope**: a Sound Blaster 16 driver (`kernel/drivers/sb16.c`/`.h`)
that resets/detects the card, programs the 8237 DMA controller's
8-bit channel 1 for a single-cycle transfer, and plays one fixed,
hardcoded PCM buffer once via an IRQ5 completion handler. A new bare
Forth word, `BEEP`, triggers it -- mirroring the existing `PAINT` word
exactly (a hook function implemented in `kernel.c`, declared in
`forth_hooks.h`, called from a `prim_beep()` primitive in `forth.c`).

**Out of scope, deliberately**: any waveform synthesis, mixing,
multiple voices, envelopes, or filters (that's sub-project (B), a
separate spec once (A) is proven). 16-bit audio (8-bit is simpler to
program -- one DMA channel, no page-register-per-64KB-boundary
quirks -- and SID-style audio doesn't need 16-bit fidelity). Auto-init
/continuous/looping playback (single-cycle -- plays once and stops --
is the smaller, provable-first step; looping is a trivial follow-up
once single-shot works). Any control surface beyond the one `BEEP`
word (SHELL/EDITOR integration, a real note-sequencing language --
explicitly future work per the user's own stated long-term direction).
Detecting *whether* real SB16 hardware exists versus assuming it's
always present -- this kernel already assumes fixed hardware
everywhere (one VBE mode, PS/2 only), and QEMU's `sb16` device always
uses the same default I/O base/IRQ/DMA channel, so runtime
autodetection isn't this step's problem to solve.

## Design

### Hardware target and how it's verified

QEMU's `-device sb16` (added to `boot/Makefile`'s `run` target,
alongside a new `-audiodev` backend) emulates a real Sound Blaster 16
at the classic default configuration: I/O base `0x220`, IRQ 5, 8-bit
DMA channel 1. This matches every OSDev SB16 tutorial's assumed
defaults exactly, so the driver can hardcode them (same "this kernel
assumes fixed hardware" precedent as the VBE mode/PS2-only design
elsewhere) rather than reading them from a config byte.

Headless verification uses QEMU's `wav` audio backend
(`-audiodev wav,id=snd0,path=/tmp/whatever.wav`), which writes the
emulated card's actual output samples to a real `.wav` file -- this is
the audio equivalent of this session's screendump-and-sample-pixels
technique: after triggering `BEEP` in a headless QEMU run, the
resulting WAV file can be inspected (sample count, non-silent
amplitude at the expected position) to prove real audio data was
actually written and played, not just that the driver code ran without
crashing.

### `sb16.c`: reset, detect, program DMA, play, IRQ-complete

Three functions, mirroring this codebase's existing driver shape
(`ata.c`'s plain polling style, `keyboard.c`/`mouse.c`'s IRQ-push
pattern):

- `int sb16_init(void)` -- the DSP reset/detect handshake (write then
  clear a reset bit, poll a status port, read back the card's
  acknowledgement byte). Returns 0 on success, -1 if the expected
  acknowledgement byte never arrives (e.g. no SB16 present) -- same
  "-1 on failure, caller decides what that means" convention `fs_*()`
  functions already use throughout this kernel.
- `void sb16_play_buffer(const unsigned char *buf, unsigned int len,
  unsigned int sample_rate)` -- programs the 8237 DMA controller's
  channel 1 (mask, clear flip-flop, mode, address, count, unmask) for
  a single-cycle 8-bit read (memory to device) transfer of `buf`, sets
  the card's output sample rate, and sends the DSP command to begin
  playback. Exact 8237/DSP port numbers and command bytes are pulled
  from OSDev's "Sound Blaster 16" and "ISA DMA" reference pages during
  implementation and confirmed by the WAV-file test actually containing
  non-silent samples -- getting a bit pattern wrong here fails loudly
  (silence, or garbage output) rather than silently, so the empirical
  test is the real correctness gate, not a spec-level transcription of
  every register value.
- An IRQ5 handler (`arch/isr.c`, mirroring `irq1_keyboard()`/
  `irq12_mouse()`'s exact shape: `__attribute__((interrupt))`, ack via
  `pic_send_eoi_master()` since IRQ5 is on the master PIC) that
  acknowledges the DSP's own playback-complete interrupt (reading the
  card's interrupt-acknowledge port, per the SB16 spec, is required to
  let the *next* IRQ5 fire) and sets a `sb16_playback_done` flag `sb16.c`
  owns. `interrupts_init()`'s vector-registration loop gains one more
  `else if (vector == 5)` arm; `interrupts_enable()` gains
  `pic_clear_mask(5);`, following the exact pattern IRQ1/IRQ12 already
  established.

### The test tone

A small hardcoded 8-bit unsigned mono PCM buffer (`static const
unsigned char beep_tone[]` in `kernel.c`, next to other baked-in
constants like `bin_paint_default[]`) -- a short square or sine wave at
a low sample rate (8000 Hz) to keep it small (a few hundred bytes for
a fraction of a second), generated once during implementation and
committed as a literal array, not computed at runtime (no floating
point in this kernel; a precomputed table sidesteps needing one).

### The `BEEP` word

Exactly mirrors `PAINT`'s own existing shape:

```c
/* forth_hooks.h */
void forth_hook_beep(void);

/* forth.c */
static void prim_beep(struct forth_vm *vm) {
    (void)vm;
    forth_hook_beep();
}
/* added to the primitive table: {"BEEP", prim_beep}, */

/* kernel.c */
void forth_hook_beep(void) {
    sb16_play_buffer(beep_tone, sizeof(beep_tone), 8000);
}
```

`sb16_init()` runs once during `kmain()`'s own startup sequence
(alongside `gfx_init()`/`fs_bootstrap_dirs()` etc.); if it fails
(returns -1), `BEEP` is a silent no-op (`forth_hook_beep()` checks a
`sb16_present` flag first) -- same "no-error-UI, silent no-op on
failure" convention this kernel already uses throughout (SAVE with an
empty filename, LOAD of a missing file).

## Testing

Not host-buildable (real port I/O, real interrupts) -- verified via a
headless QEMU pass: boot with `-device sb16,audiodev=snd0 -audiodev
wav,id=snd0,path=<tmp>.wav`, open FORTH, type `BEEP`, wait briefly for
playback to finish, shut the VM down cleanly (so the WAV file's header
gets finalized), then inspect the resulting `.wav` file directly
(sample count matches the expected buffer duration at 8000 Hz; the
samples are non-silent, i.e. not all at the PCM zero/mid-point value)
to prove real audio data actually played, not just that `BEEP` didn't
crash the kernel.
