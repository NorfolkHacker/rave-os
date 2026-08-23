# An 8-voice SID-like synthesizer: real oscillators, envelopes, and mixing

## Purpose

`docs/IDEAS.md`'s "Audio" item wanted an 8-channel SID-like software
synthesizer. Sub-project (A) (`docs/superpowers/specs/2026-08-22-sb16-audio-driver-design.md`)
proved this kernel can drive real hardware audio output at all -- one
hardcoded test tone, one DMA transfer, done. It deliberately built
nothing that synthesizes anything: no waveforms beyond one baked-in
buffer, no envelopes, no multiple voices. This spec is sub-project (B):
the actual synthesizer -- real oscillators, ADSR envelopes, and
8-voice mixing, inspired partly by the real MOS 6581/8580 SID chip and
partly by TempleOS's `Snd`/`Beep` sound API
(https://tinkeros.github.io/WbTempleOS/LiveHelp/Sound.html), whose
"ona" (piano-key number) note system this spec borrows directly.

Sub-project (C) -- a real control surface / note-sequencing language --
stays explicitly out of scope here, same as (A) left it. This spec
ships the engine plus just enough manual Forth words to prove it works
from the console, mirroring how `BEEP` proved (A): get one voice, one
note, working end to end first; a real sequencing language is later,
separate work once this is proven.

## Scope

**In scope:**
- A new `kernel/audio/` subsystem (`synth.c`/`.h`) implementing 8
  independent voices, each with a selectable waveform (pulse/sawtooth/
  triangle/noise) and a full ADSR envelope.
- Software mixing of all 8 voices into one continuous 8-bit unsigned
  PCM stream.
- Extending `kernel/drivers/sb16.c` from sub-project (A)'s single-cycle
  (one-shot) DMA to **auto-init (looping) DMA** with double-buffering,
  so audio actually keeps flowing continuously while notes sustain and
  envelopes evolve -- (A) explicitly deferred this ("Auto-init/
  continuous/looping playback... is a trivial follow-up once
  single-shot works"); this spec is that follow-up.
- Ona-number note addressing (1-88, piano-key numbering, TempleOS-
  style), via a precomputed note-to-frequency lookup table.
- A minimal, manual Forth control surface: select a voice, set its
  waveform/duty/note/envelope, gate it on/off. Enough to play notes
  from the console and prove the engine works -- not a music language.

**Out of scope, deliberately:**
- **The SID's resonant filter** (low-pass/band-pass/high-pass,
  configurable cutoff/resonance, applied to a chosen subset of
  voices). A real, separate DSP stage -- a follow-up spec once this
  core engine is proven, the same way (A) was proven before (B)
  started.
- **Sub-project (C):** any real note-sequencing / tracker / music
  language. The Forth words this spec adds are a manual test harness
  (turn a knob, hear a note), not a composition tool.
- **Band-limited synthesis (anti-aliasing).** Naive pulse/sawtooth
  generation is harmonically rich and will alias above the Nyquist
  frequency at this stream's sample rate (see below) -- real SID chips
  and early trackers have this same lo-fi character, and fixing it
  (BLEP/BLIT synthesis or similar) is its own DSP project, not this
  one's problem to solve.
- **Per-voice panning/stereo.** SB16 output here is mono, matching (A).
- **Runtime-computed note frequencies.** This kernel has no floating
  point anywhere (`kernel/Makefile`'s `CFLAGS` carries
  `-mgeneral-regs-only`, required by `isr.c`'s
  `__attribute__((interrupt))` handlers) -- frequencies come from a
  precomputed table, not a runtime `pow()`/`exp()`.

## Design

### Where mixing happens, and why

IRQ5 (from sub-project A) stays minimal, matching this codebase's
established IRQ convention (`irq1_keyboard()`/`irq12_mouse()` just
read a port and flag/ack -- no real work in the handler itself).
`sb16_play_buffer()`'s single-cycle DMA is extended with a new
`sb16_start_stream()` that programs the 8237 DMA controller in
**auto-init mode** over a double buffer (two halves, one contiguous
DMA region) and issues the SB16 DSP's auto-init 8-bit output command
with block size set to one half. The card then raises IRQ5 once per
half-buffer completion, forever, without software reprogramming DMA
each time.

`irq5_sb16()` (extended) does its existing `sb16_irq_ack()` call, then
sets a flag (`audio_refill_needed`) plus which half just finished
(the one *not* currently playing is the one that needs refilling) --
and returns. `kmain()`'s existing per-frame loop, right next to its
existing `scheduler_tick()` call, checks that flag every frame and
calls `synth_render_half()` to fill the free half with fresh samples
while the card keeps playing the other half uninterrupted. This keeps
all real computation (oscillator math, envelope updates, mixing) out
of interrupt context, consistent with every other IRQ handler in this
kernel.

### Stream format and buffer sizing

8-bit unsigned mono PCM at **22050 Hz** (not (A)'s 8000 Hz -- a
piano's full ona range reaches ~4186 Hz at the top key, and 8000 Hz's
4000 Hz Nyquist ceiling would alias the highest notes badly; 22050 Hz
gives real headroom while staying a standard, simple rate).

Each half-buffer is sized for a comfortable refill margin: **1024
bytes per half** (2048 bytes total) is about 46ms of audio at 22050 Hz
-- `kmain()`'s frame loop runs far more often than that in normal
operation, so a refill has many chances to happen before the card
catches up to an unfilled half. The full 2048-byte double-buffer region
is `aligned(4096)`, the same power-of-two-alignment trick sub-project
A's `beep_tone[]` used to guarantee it can never straddle a 64KB
physical-address boundary (a hard ISA-DMA requirement) -- 4096 is
comfortably larger than needed for alignment purposes and leaves the
exact half-buffer byte count free to tune during implementation if the
real refill margin turns out to need adjusting.

### Note addressing: ona numbers

Ona 1-88 maps to the standard 88-key piano range (ona 49 = A4 = 440.0
Hz, following standard MIDI/piano numbering with A4 as the tuning
reference -- implementation should double check this exact indexing
convention against a real piano-key/ona reference table before
committing the constants, since the TempleOS page itself is
inconsistent about which ona equals which reference pitch). Rather
than compute `440 * 2^((ona-49)/12)` at runtime (no floating point
available), `kernel/audio/synth.c` ships a **precomputed 88-entry
table** of phase increments (fixed-point, matching the oscillator
format below), generated once (by a short host-side script or by hand
during implementation) and committed as a C array literal -- the same
"no floating point in this kernel; a precomputed table sidesteps
needing one" precedent sub-project (A) already established for
`beep_tone[]`.

### Oscillators

Fixed-point phase accumulation (e.g. Q16.16: a 32-bit unsigned
accumulator, upper 16 bits select a position in the waveform's period).
Each voice holds `phase_accum` (running position) and `phase_increment`
(looked up from the ona table when its note is set); every output
sample, `phase_accum += phase_increment`, wrapping naturally on
overflow.

Four waveforms, matching the real SID's set:
- **Pulse/square:** output high or low depending on whether
  `phase_accum`'s top bits are below or above a duty-cycle threshold
  (itself a fraction of the full period) -- the duty cycle is
  per-voice-settable, giving the classic SID PWM sound.
- **Sawtooth:** output directly proportional to `phase_accum`'s top
  bits (a linear ramp across the period).
- **Triangle:** `phase_accum`'s top bits, folded (ramps up for the
  first half of the period, down for the second).
- **Noise:** a simple LFSR (linear-feedback shift register), clocked
  once per sample (or once per `phase_increment` wrap, closer to the
  real SID's noise-generator behavior) -- deterministic given a fixed
  seed, not read from any hardware entropy source.

### Envelopes (ADSR)

Each voice's envelope is a small state machine: `OFF -> ATTACK ->
DECAY -> SUSTAIN -> RELEASE -> OFF`. `GATE-ON` moves a voice from
`OFF`/`RELEASE` into `ATTACK`; `GATE-OFF` moves a sustaining (or still
attacking/decaying) voice into `RELEASE`. Attack/decay/release are
each a fixed-point per-sample level increment/decrement computed from
a duration (attack and release ramp toward 0/max; decay ramps from max
toward the sustain level); sustain holds its level unchanged until
gated off. The current envelope level (a fixed-point 0.0-1.0-equivalent
amplitude) multiplies that voice's waveform sample before mixing.

### Mixing

Once per output sample, each of the 8 active voices contributes a
signed value (waveform sample scaled by its current envelope level).
**These must be summed and clamped to a signed range before biasing to
unsigned 8-bit output** -- 8 simultaneously maxed-out voices sum to
roughly 4x what a single byte holds, so unclamped summation would wrap
around into loud garbage rather than just clipping. The clamped sum is
then biased by +128 (matching this stream's 8-bit-unsigned-PCM,
zero-centered-at-128 format) and written into the buffer half being
rendered.

### Voice state and API

```c
/* kernel/audio/synth.h */
enum synth_waveform { WAVE_PULSE, WAVE_SAW, WAVE_TRIANGLE, WAVE_NOISE };

void synth_init(void);
void synth_set_voice_waveform(int voice, enum synth_waveform wave);
void synth_set_duty(int voice, int duty_percent);       /* pulse only */
void synth_set_ona(int voice, int ona);                 /* 1-88 */
void synth_set_adsr(int voice, int attack_ms, int decay_ms,
                     int sustain_percent, int release_ms);
void synth_gate_on(int voice);
void synth_gate_off(int voice);

/* called from kmain()'s frame loop when sb16's refill flag is set */
void synth_render_half(unsigned char *buf, unsigned int len);
```

All setters clamp or no-op on out-of-range `voice`/parameter values
rather than indexing off the end of the 8-entry voice array or a
waveform dispatch table.

Mutating a voice's state (`GATE-ON`, changing its waveform mid-note,
etc.) while `synth_render_half()` is reading that same state from
`kmain()`'s frame loop is not a real concurrency hazard in this
kernel: single core, no preemption, cooperative scheduling only --
Forth hook calls and `synth_render_half()` both run on `kmain()`'s own
execution context, never truly simultaneously. No locking is needed,
stated explicitly here rather than left as an unexamined assumption.

### The Forth control surface

Mirrors PAINT's existing "select a context, then act on it" pattern
(`CURRENT-COLOR`-style) rather than inventing a new one: `VOICE`
selects which of the 8 voices the following words act on.

| Word | Stack effect | Effect |
|---|---|---|
| `VOICE` | `( n -- )` | Select voice `n` (0-7) as current |
| `WAVE` | `( n -- )` | Set current voice's waveform (0=pulse, 1=saw, 2=triangle, 3=noise) |
| `DUTY` | `( n -- )` | Set current voice's pulse duty cycle, percent (pulse only, ignored otherwise) |
| `ONA` | `( n -- )` | Set current voice's note, ona 1-88 |
| `ADSR` | `( a d s r -- )` | Set current voice's attack/decay/release (ms) and sustain (percent) |
| `GATE-ON` | `( -- )` | Trigger current voice's envelope |
| `GATE-OFF` | `( -- )` | Release current voice's envelope |

Each is a bare Forth word wired exactly like `BEEP`/`PAINT`: a
`forth_hook_*()` in `kernel.c`, declared in `forth_hooks.h`, a
`prim_*()` in `forth.c` added to the primitive table. The very first
call to any of these lazily calls `sb16_start_stream()` (mirroring
`BEEP`'s lazy `sb16_present`-gated pattern from sub-project A) if the
stream isn't already running; once started, the stream runs forever
(rendering silence -- all voices' envelopes at `OFF` -- when nothing is
gated) rather than starting/stopping DMA per note, avoiding
stream-restart complexity entirely. No SB16 card present means every
one of these words is a silent no-op, same convention `BEEP` already
established.

## Testing

**Host-buildable (`kernel/tests/`, no QEMU, same convention as
`test_font.c`/`test_scheduler.c`):** every piece of this that's pure
math and has no hardware dependency --
- Ona-to-phase-increment table: spot-check known reference notes (e.g.
  the ona for A4 resolves to a phase increment corresponding to 440
  Hz within rounding tolerance).
- ADSR state machine: gate-on reaches full attack then decays to the
  configured sustain level and holds; gate-off from any stage moves to
  release and reaches zero.
- Each waveform's sample shape across a full period (pulse's duty-cycle
  threshold, sawtooth's linear ramp, triangle's fold, noise's basic
  statistical spread rather than a fixed pattern).
- Mixer clamping: 8 simultaneously maxed-out voices must not wrap
  around past full-scale.

**Headless QEMU (real audio path, extending sub-project (A)'s
WAV-inspection approach):** trigger a single voice/note via the Forth
console (`sendkey`), let it play, shut down cleanly, inspect the
resulting `.wav` file for non-silence and roughly-correct sustained
duration. A stretch goal (flagged, not committed to) is a
zero-crossing-rate check on one pure sustained tone to catch a
wrong-LUT-value class of bug that a bare non-silence check would miss.

**Real-hardware pass still pending afterward**, same bar sub-project
(A) was held to. Continuous auto-init DMA's refill timing is exactly
the kind of thing QEMU is least likely to surface a real-hardware
glitch in -- a refill that lands a few frames late might be tolerated
by QEMU's emulated timing but audibly click/glitch on a real card.
