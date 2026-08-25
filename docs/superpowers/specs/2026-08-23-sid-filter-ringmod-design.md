# A resonant filter and ring modulation for the SID-like synth

## Purpose

`docs/superpowers/specs/2026-08-23-sid-synth-design.md` (the 8-voice
synth engine, shipped) deliberately deferred the real SID's resonant
filter as its own follow-up spec, once the core oscillator/envelope/
mixer engine was proven working. This spec is that follow-up, plus one
feature the original spec never covered at all: ring modulation, the
other half of what gives SID leads their characteristic metallic/bell
tones. A note-sequencing/pattern-player language (sub-project C) stays
explicitly deferred, kept separate rather than folded in here, the
same staged-scope discipline that shipped the engine itself.

## Scope

**In scope:**
- One shared, global resonant filter (matching the real SID's
  architecture: one filter instance, not one per voice), implemented
  as a fixed-point Chamberlin state-variable filter -- a well-known,
  simple digital filter topology producing simultaneous low-pass/
  band-pass/high-pass outputs from one recursive computation per
  sample.
- Per-voice filter routing: each voice independently chooses whether
  its signal passes through the filter or goes straight to the output
  unfiltered -- matching SID's own routing register, scaled from 3
  voices to 8.
- A combinable filter-mode selector (low-pass / band-pass / high-pass,
  any combination summed together), matching SID's own filter output
  select bits.
- Ring modulation: a per-voice, assignable "ring partner" (any of the
  8 voices, not the real chip's fixed 3-voice ring topology, consistent
  with how the engine already generalized note/waveform assignment
  past SID's fixed wiring). Affects only the triangle waveform, exactly
  where real SID's ring mod is actually wired in -- it has no effect on
  pulse/saw/noise, matching the real chip rather than generalizing the
  effect beyond what it's actually known for.
- New Forth words: `FILTER-CUTOFF`, `FILTER-RES`, `FILTER-MODE` (global
  filter settings), `FILTER-ROUTE` (per-current-voice), `RING-PARTNER`/
  `RING-OFF` (per-current-voice, mirrors `GATE-ON`/`GATE-OFF`'s shape).

**Out of scope, deliberately:**
- **A faithful reSID-grade filter emulation.** Real SID's filter has
  well-documented analog quirks and non-linearities (and the two chip
  revisions, 6581/8580, sound different) that projects like reSID spend
  enormous effort replicating, much of it leaning on floating-point
  reference math this kernel cannot use at all
  (`kernel/Makefile`'s `-mgeneral-regs-only`). This spec targets "sounds
  like a real resonant filter with SID's control surface shape," not a
  byte-exact clone.
- **Sub-project (C), any note-sequencing/pattern-player language.**
  Explicitly kept separate, a future spec once this one ships.
- **11-bit cutoff resolution.** Real SID's cutoff register is 0-2047;
  this spec uses an 8-bit range (0-255) instead, purely for a
  practically-sized precomputed coefficient table (see Design) -- 256
  steps is still fine musical control, just a smaller literal to embed
  and generate than a 2048-entry one.
- **Full filter self-oscillation as a design goal.** Extreme cutoff+
  resonance combinations get clamped for stability (see Design), not
  tuned to be a genuinely stable self-oscillating sine source the way
  some real analog resonant filters can be pushed to.

## Design

### The filter core: a fixed-point Chamberlin state-variable filter

Classic difference equations (continuous form):
```
hp = input - lp - q*bp
bp = bp + f*hp
lp = lp + f*bp
```
where `f = 2*sin(pi*fc/fs)` (a frequency coefficient derived from the
cutoff frequency) and `q = 1/Q` (a resonance feedback coefficient,
smaller `q` meaning higher resonance/sharper peak). All three outputs
(`lp`/`bp`/`hp`) come from one shared recursive computation each
sample -- this is what lets SID's filter-mode register combine them
freely.

Fixed-point (Q14, i.e. values scaled by `2^14 = 16384`, no floating
point anywhere per the parent spec's constraint): both `f` and `q` are
precomputed as Q14 integers and the multiplies use a `>>14` shift:
```c
struct synth_filter_state { int lp; int bp; };

int synth_filter_process_sample(struct synth_filter_state *st, int input,
                                 int f_coeff, int q_coeff, int mode_mask) {
    int hp = input - st->lp - ((q_coeff * st->bp) >> 14);
    int bp_new = st->bp + ((f_coeff * hp) >> 14);
    int lp_new = st->lp + ((f_coeff * bp_new) >> 14);

    /* Safety clamp on the INTERNAL recursive state, not just the final
     * output -- an aggressive cutoff+resonance combination can make
     * this state grow without bound; unclamped, that would eventually
     * overflow and corrupt the whole mixed output, not just distort. */
    if (lp_new > SYNTH_FILTER_STATE_MAX) lp_new = SYNTH_FILTER_STATE_MAX;
    if (lp_new < -SYNTH_FILTER_STATE_MAX) lp_new = -SYNTH_FILTER_STATE_MAX;
    if (bp_new > SYNTH_FILTER_STATE_MAX) bp_new = SYNTH_FILTER_STATE_MAX;
    if (bp_new < -SYNTH_FILTER_STATE_MAX) bp_new = -SYNTH_FILTER_STATE_MAX;

    st->lp = lp_new;
    st->bp = bp_new;

    {
        int out = 0;
        if (mode_mask & SYNTH_FILTER_MODE_LP) out += lp_new;
        if (mode_mask & SYNTH_FILTER_MODE_BP) out += bp_new;
        if (mode_mask & SYNTH_FILTER_MODE_HP) out += hp;
        return out;
    }
}
```
(`SYNTH_FILTER_STATE_MAX` a constant chosen generously above the
filter's normal operating amplitude but well inside `int` range --
exact value an implementation-time tuning question, verified
empirically the same way the original engine's exact register values
were: does it clamp only genuinely extreme settings, not normal ones.)

### Cutoff and resonance: precomputed coefficient tables, not runtime math

No floating point exists in this kernel, so `f = 2*sin(pi*fc/fs)`
cannot be computed at runtime. Following the exact precedent
`ona_phase_increment[]` already established: generate a table once on
the host and commit it as a literal array.

**Cutoff** (`SYNTH_FILTER_CUTOFF_STEPS` = 256 entries, `FILTER-CUTOFF`
0-255): frequency mapped exponentially (log-spaced, matching musical
pitch perception the same way the ona table's note spacing already
is) across 20Hz-3000Hz -- the upper bound deliberately conservative,
comfortably under `SYNTH_SAMPLE_RATE/6 ≈ 3675Hz`, the range where this
simple (non-oversampled) state-variable topology stays numerically
well-behaved:
```python
# generation formula, host-side, same convention as the ona table
for i in range(256):
    freq = 20.0 * (3000.0 / 20.0) ** (i / 255.0)
    f = 2.0 * math.sin(math.pi * freq / 22050.0)
    table[i] = round(f * 16384)
```

**Resonance** (`SYNTH_FILTER_RES_STEPS` = 16 entries, matching SID's
own 4-bit resonance register, `FILTER-RES` 0-15): a quality factor `Q`
sweeping roughly 0.707 (heavily damped, `FILTER-RES 0`) up to 8.0
(sharp resonant peak, `FILTER-RES 15`), linear in `Q`:
```python
for i in range(16):
    Q = 0.707 + i * (8.0 - 0.707) / 15.0
    q = 1.0 / Q
    table[i] = round(q * 16384)
```
Both curves' exact endpoints are a starting point, not a hard
requirement -- tunable during implementation by ear the same way this
project has tuned other perceptual constants (e.g. `BEEP`'s original
test-tone frequency), so long as the fixed-point scale/shift
convention (Q14, `>>14`) stays consistent throughout.

### Per-voice routing and the render loop

`synth_render_half()`'s existing per-voice loop already computes each
voice's enveloped oscillator sample. Each voice's new `filter_route`
field (0/1) decides whether that sample adds to a `filtered_sum` or a
`bypass_sum` accumulator instead of the single `sum` that exists today.
After the voice loop, once per output sample:
```c
int filtered_out = synth_filter_process_sample(&synth_filter, filtered_sum,
                                                current_f_coeff, current_q_coeff,
                                                synth_filter_mode);
int sum = filtered_out + bypass_sum;
/* existing clamp-to-[-128,127] and +128 bias, unchanged */
```
`current_f_coeff`/`current_q_coeff` are looked up from the current
global `FILTER-CUTOFF`/`FILTER-RES` settings (table lookups, not
recomputed from scratch each sample -- cheap). `synth_filter` (the one
shared `struct synth_filter_state`) is module-level state, exactly one
instance, matching "one shared filter" from Scope.

### Ring modulation: a rewrite of the triangle case, not a new subsystem

Real SID's ring mod XORs the oscillator's own accumulator MSB with a
partner voice's MSB, and that combined bit -- not the voice's own bit
alone -- drives the triangle generator's fold direction. Today's
triangle case:
```c
case WAVE_TRIANGLE: {
    unsigned int tri_pos = (pos8 < 128u) ? pos8 : (255u - pos8);
    return (int)(tri_pos * 2u) - 128;
}
```
Rewritten to isolate the top-bit fold decision explicitly (behaviorally
identical to today when ring mod is inactive -- `msb` reduces to
exactly `pos8 < 128`'s complement either way):
```c
case WAVE_TRIANGLE: {
    unsigned int msb = (pos8 >> 7) & 1u;
    unsigned int lower7 = pos8 & 0x7Fu;
    if (ring_active) {
        unsigned int partner_pos8 = (ring_partner_phase_accum >> 24) & 0xFFu;
        msb ^= (partner_pos8 >> 7) & 1u;
    }
    unsigned int tri_pos = msb ? (127u - lower7) : lower7;
    return (int)(tri_pos * 2u) - 128;
}
```
(`ring_active`/`ring_partner_phase_accum` are two new parameters
`synth_osc_sample()` gains -- exact naming/whether they're folded into
one struct argument instead is an implementation-time call. The
struct-level source of truth stays `synth_voice.ring_partner`, `-1` =
off, `0`-`7` = ring against that voice; the caller in
`synth_render_half()` derives both new parameters from it, e.g.
`ring_active = (voice->ring_partner >= 0)`.)

`synth_render_half()`'s per-voice loop already has direct access to
`synth_voices[]`, so passing the partner's `phase_accum` for this
sample costs nothing extra to look up. Whether the partner's
`phase_accum` reflects this-sample-post-update or this-sample-pre-update
(a same-loop-iteration ordering question, since all 8 voices' phases
advance within the same per-sample pass) is at most one sample's worth
of phase difference -- inaudible, not worth adding a snapshot/two-pass
structure to control precisely.

### The Forth control surface

Global filter settings (no voice-context needed):
| Word | Stack effect | Effect |
|---|---|---|
| `FILTER-CUTOFF` | `( n -- )` | Set the filter's cutoff, 0-255 |
| `FILTER-RES` | `( n -- )` | Set the filter's resonance, 0-15 |
| `FILTER-MODE` | `( n -- )` | Set which outputs sum into the result: bit0=LP, bit1=BP, bit2=HP (0-7) |

Per-current-voice (act on whichever voice `VOICE` last selected, same
pattern `WAVE`/`DUTY`/`ONA` already use):
| Word | Stack effect | Effect |
|---|---|---|
| `FILTER-ROUTE` | `( n -- )` | `0` = bypass the filter, `1` = route through it |
| `RING-PARTNER` | `( n -- )` | Ring-modulate this voice's triangle output against voice `n` (0-7) |
| `RING-OFF` | `( -- )` | Disable ring mod for this voice |

Each wired exactly like the existing synth words: a `forth_hook_*()` in
`kernel.c`, declared in `forth_hooks.h`, a `prim_*()` in `forth.c`
added to the primitive table, all no-ops (or clamped) if the audio
stream was never started / the target values are out of range,
following the established convention throughout this feature.

## Testing

**Host-buildable**, same convention as the rest of `kernel/tests/test_synth.c`:
- `synth_filter_process_sample()`: feed a step input at a few cutoff/
  resonance combinations and confirm the low-pass output settles
  toward the step's value without runaway growth; confirm higher
  resonance visibly increases peak overshoot near the step, without
  ever exceeding the internal clamp; confirm each of the 8 possible
  `FILTER-MODE` combinations (0 through `LP|BP|HP`) produces the
  expected sum shape, including the `mode_mask == 0` case (filtered
  voices audibly silent, not an error).
- Ring mod: confirm a ring-modulated triangle's output differs from
  the same voice's unmodulated output at phase values where the
  partner's MSB actually differs; confirm a voice ring-modulating
  against itself (`RING-PARTNER` targeting its own voice index) is
  harmless (XOR-with-self always clears the bit, a valid if
  uninteresting waveform, not a crash); confirm `RING-PARTNER` has
  zero effect on pulse/saw/noise waveforms (the XOR only ever gets
  read inside the triangle case).

**Headless QEMU**, extending the established WAV-inspection pattern:
trigger two voices at the same pitch, route one through a resonant
low-pass filter (`FILTER-ROUTE` on) and leave the other unfiltered,
and confirm the resulting audio measurably differs (spectral energy
shifted toward lower frequencies, not just "still non-silent" -- a
bare non-silence check wouldn't actually prove the filter did
anything).

**Real-hardware pass still pending afterward**, same bar every prior
piece of this feature has been held to.
