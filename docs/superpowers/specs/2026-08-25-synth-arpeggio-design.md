# An arpeggio effect for the SID-like synth

## Purpose

`docs/superpowers/specs/2026-08-23-sid-filter-ringmod-design.md` (the
resonant filter and ring modulation, shipped) closed out sub-project
(B)'s last gap and explicitly deferred an arpeggio effect as
possible future work, separate from sub-project (C)'s real
note-sequencing language. This spec is that follow-up. Real SID chips
only have 3 voices, so a fast, automated pitch-cycle on one voice is
how chiptune music fakes a chord; RaveOS already has 8 real voices to
spare, so this isn't filling a hardware gap -- it's the classic
chiptune arpeggio texture as a deliberate stylistic effect in its own
right, not a chord substitute.

## Scope

**In scope:**
- A per-voice arpeggio: an explicit list of 2-4 notes (absolute `ona`
  values, the same 1-88 scale `ONA` already uses) that the voice's
  pitch automatically cycles through, up-only with wraparound, while
  active.
- Sample-accurate timing, following the exact "convert milliseconds to
  a per-sample counter, no floating point" pattern the ADSR envelope's
  `synth_calc_rate()` already established.
- Stepping runs independently of envelope stage (keeps cycling through
  release, exactly like the oscillator's phase already advances
  regardless of envelope stage today) and independently of `ONA` (the
  note list is its own state, not derived from or overwriting the
  voice's plain `ona` setting).
- `GATE-ON` restarts the pattern from the first note every time.
- New Forth words: `ARP-NOTE` (per-slot), `ARP-ON`/`ARP-OFF`
  (per-current-voice), `ARP-RATE` (per-current-voice).

**Out of scope, deliberately:**
- **Any pattern order besides up-only-wrap.** No down, no ping-pong, no
  random. YAGNI -- revisit only if up-only turns out to feel limited
  once it's actually in use.
- **Interval/relative note specification, or canned chord shapes.**
  Every slot is an explicit absolute `ona` value; the caller computes
  whatever intervals they want before calling `ARP-NOTE`.
- **Syncing across voices, or a shared/global arpeggio clock.** Each
  voice's arpeggio runs on its own independently-configured rate and
  step counter, matching how ADSR is already fully per-voice (contrast
  with the filter, which is deliberately one shared instance).
- **Sub-project (C), any note-sequencing/pattern-player language.**
  Still explicitly kept separate, a future spec of its own.

## Design

### Per-voice state

Four new fields on `struct synth_voice`:
```c
int arp_notes[4];   /* absolute ona values, slots 0-3 */
int arp_count;      /* how many of arp_notes[] are in play, 2-4 */
int arp_active;      /* 0/1 -- independent of arp_count/arp_notes, so
                      * ARP-OFF then ARP-ON later doesn't require
                      * reloading notes */
int arp_step;        /* current index into arp_notes[], 0..arp_count-1 */
int arp_step_rate;    /* per-sample counter threshold, derived from
                      * ARP-RATE's ms argument -- same role
                      * attack_rate/decay_rate/release_rate already
                      * play for envelope timing */
int arp_step_counter; /* counts up each sample while arp_active;
                      * resets to 0 and advances arp_step on reaching
                      * arp_step_rate */
```
`arp_notes[]` and `arp_count` persist across `ARP-OFF`/`ARP-ON` --
`ARP-OFF` only clears `arp_active`, matching `RING-OFF`'s own
"clearing the effect, not the configuration" shape (`RING-OFF` doesn't
forget which partner was last set either, it just sets `ring_partner`
back to -1 -- the parallel here is `arp_active` playing the same role
`ring_partner`'s sign does).

`synth_init()` resets all six new fields to an idle default
(`arp_active = 0`, `arp_count = 0`, `arp_step = 0`,
`arp_step_counter = 0`, `arp_step_rate` to some safe nonzero value,
`arp_notes[]` contents don't matter while `arp_active` is 0) -- the
same per-voice clean-slate guarantee every other field already gets,
and the same test/re-init isolation reasoning the filter's own shared
state was fixed to respect.

### Timing: reusing `synth_calc_rate()`'s conversion, not its scale

`ARP-RATE`'s millisecond argument converts to `arp_step_rate` the same
"clamp duration, convert to samples, guard the zero case" shape
`synth_calc_rate()` already uses for envelope timing:
```c
static int synth_calc_arp_step_samples(int ms) {
    unsigned int samples;
    if (ms <= 0) {
        ms = 1;
    }
    if (ms > 10000) {
        ms = 10000;
    }
    samples = ((unsigned int)ms * SYNTH_SAMPLE_RATE) / 1000u;
    if (samples == 0) {
        samples = 1;
    }
    return (int)samples;
}
```
Unlike `synth_calc_rate()`, this isn't a per-sample increment added to
an accumulating level -- it's a plain sample countdown/countup to a
threshold, so no Q8 scaling is involved; the result is used directly
as `arp_step_rate`. The 10000ms upper clamp (vs. `synth_calc_rate()`'s
100000ms) is deliberately tighter: an arpeggio slower than 10
seconds/step stops sounding like an arpeggio at all, so there's no
reason to support the same extreme range ADSR's release stage does.

### Stepping in the render loop

In `synth_render_half()`'s existing per-voice per-sample loop, after
today's phase-accumulator advance, for any voice with `arp_active`:
```c
if (voice->arp_active) {
    voice->arp_step_counter++;
    if (voice->arp_step_counter >= voice->arp_step_rate) {
        voice->arp_step_counter = 0;
        voice->arp_step = (voice->arp_step + 1) % voice->arp_count;
        voice->phase_increment =
            ona_phase_increment[voice->arp_notes[voice->arp_step] - 1];
    }
}
```
This is the exact same table lookup `synth_set_ona()` already performs
-- the arpeggio is mechanically just "call the equivalent of `ONA`
automatically, on a timer." No new oscillator logic, no interaction
with `synth_osc_sample()` at all: `phase_increment` is the only thing
that changes, so ring modulation and filter routing both keep working
completely unaware an arpeggio is even happening -- they already only
ever look at `phase_accum`/the mixed sample, never at how
`phase_increment` got set.

Note this means `phase_increment` is a single shared destination for
both `ONA` and the arpeggio -- there's no separate stored "plain ona"
value to fall back to, since `synth_set_ona()` has always written
straight into `phase_increment` with nothing kept alongside it.
`ARP-OFF` therefore just stops future stepping (clears `arp_active`);
`phase_increment` stays at whatever note the arpeggio was last on, not
some remembered pre-arpeggio pitch. Getting a specific static pitch
back after `ARP-OFF` means calling `ONA` again, the same way it always
has -- this is a deliberate no-extra-state simplicity choice, not an
oversight.

### `GATE-ON` must prime the first note, not just reset the index

Today's `synth_gate_on()` silently no-ops if `phase_increment == 0`
(a voice whose `ONA` was never set) -- see its existing comment. An
arpeggiating voice may legitimately have `ona` never set (its pitch
comes entirely from `arp_notes[]`), so `synth_gate_on()` needs a new
case: when `voice->arp_active`, prime `phase_increment` from
`arp_notes[0]` (and reset `arp_step`/`arp_step_counter` to 0) *before*
the existing zero-check, e.g.:
```c
void synth_gate_on(int voice) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    if (synth_voices[voice].arp_active) {
        synth_voices[voice].arp_step = 0;
        synth_voices[voice].arp_step_counter = 0;
        synth_voices[voice].phase_increment =
            ona_phase_increment[synth_voices[voice].arp_notes[0] - 1];
    }
    if (synth_voices[voice].phase_increment == 0) {
        return;
    }
    synth_voices[voice].envelope_level = 0;
    synth_voices[voice].envelope_stage = ENV_ATTACK;
}
```
Without this, a voice arpeggiating from a never-`ONA`'d state would
silently fail to gate on at all -- the exact DC-click failure mode the
original zero-check comment describes, just reached from a new
direction.

### The Forth control surface

Per-current-voice (act on whichever voice `VOICE` last selected, same
pattern `WAVE`/`ONA`/`RING-PARTNER` already use):

| Word | Stack effect | Effect |
|---|---|---|
| `ARP-NOTE` | `( note slot -- )` | Set `arp_notes[slot]` to `note` (slot 0-3, note 1-88) |
| `ARP-ON` | `( count -- )` | Activate arpeggiation using the first `count` (2-4) loaded slots |
| `ARP-OFF` | `( -- )` | Deactivate; stepping stops immediately |
| `ARP-RATE` | `( ms -- )` | Set this voice's per-step duration in milliseconds |

Validation follows this feature's own established split (see
`kernel/forth/forth.c`'s comments on `RING-PARTNER` vs. `DUTY`):
- `ARP-NOTE`'s `slot` (0-3) and `note` (1-88) are both discrete indices
  with a meaningless out-of-range value -- hard `forth_set_error()`,
  matching `ONA`/`VOICE`/`RING-PARTNER`'s convention. `slot` in
  particular writing out of bounds would corrupt adjacent voice state,
  not just misbehave musically, so this can't be a soft clamp.
- `ARP-ON`'s `count` (2-4) is likewise hard-rejected -- letting it
  through unclamped would read `arp_notes[]` out of bounds in the
  render loop.
- `ARP-RATE`'s `ms` clamps gracefully (like `FILTER-CUTOFF`/`DUTY`), no
  hard error -- it's a continuous tuning parameter, not a discrete
  index.

Each wired exactly like the existing synth words: a `forth_hook_*()`
in `kernel.c`, declared in `forth_hooks.h`, a `prim_*()` in `forth.c`
added to the primitive table.

## Testing

**Host-buildable**, extending `kernel/tests/test_synth.c`:
- Timing: `synth_calc_arp_step_samples()` at a few millisecond values,
  confirming the clamp bounds and the zero-guard.
- Stepping order: drive `synth_render_half()` for enough samples to
  cross several step boundaries and confirm `phase_increment` visits
  `arp_notes[0..count-1]` in order and wraps back to `arp_notes[0]`,
  for both a 2-note and a 4-note list.
- `GATE-ON` restart: advance partway through a pattern, `GATE-OFF`
  then `GATE-ON` again, confirm the step index is back at 0 (first
  note), not wherever it had drifted to.
- The never-`ONA`'d-voice case: confirm `GATE-ON` on a voice that only
  ever had `ARP-NOTE`/`ARP-ON` called (never plain `ONA`) still gates
  on and plays the first arp note, not the silent no-op `ONA`-only
  voices get.
- Release behavior: confirm stepping continues (phase_increment keeps
  changing) through the release stage after `GATE-OFF`, per this
  spec's chosen behavior.
- `ARP-OFF` behavior: confirm stepping stops immediately (further
  render calls don't change `phase_increment` further) while it stays
  at whatever note the arpeggio was last on, and that a later `ARP-ON`
  (no new `ARP-NOTE` calls) resumes with the previously-loaded notes
  intact.
- Interaction sanity: confirm ring modulation and filter routing both
  still work unmodified on an arpeggiating voice (this should require
  no special-casing at all if the design's "arp only ever touches
  `phase_increment`" claim holds -- the test is there to prove that
  claim, not to add new behavior).

**Headless QEMU**, extending the established WAV-inspection pattern:
load a 3-note arpeggio at a fast rate, gate it on, and confirm the
resulting audio's zero-crossing-rate/spectral content changes in a
way consistent with rapid pitch-cycling (measurably different from
the same voice playing one static `ONA` note) -- same "prove it's
doing real work, not just non-silent" bar the filter's own QEMU pass
used.

**Real-hardware pass still pending afterward**, same bar every prior
piece of this feature has been held to.
