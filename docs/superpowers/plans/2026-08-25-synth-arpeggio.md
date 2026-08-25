# Synth Arpeggio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a per-voice arpeggio effect (2-4 explicit notes, up-only-wrap, sample-accurate millisecond timing) to the existing 8-voice synthesizer, controllable via 4 new Forth words.

**Architecture:** `kernel/audio/synth.c`/`.h` gain six new per-voice fields and four new setters. `synth_render_half()`'s existing per-voice per-sample loop gains a small stepping block, placed right after today's phase-accumulator advance, that periodically re-derives `phase_increment` from the voice's note list using the exact same `ona_phase_increment[]` table lookup `synth_set_ona()` already performs -- the arpeggio never touches the oscillator, envelope, ring-mod, or filter code at all. `synth_gate_on()` gains one new priming branch so an arpeggiating voice that never had `ONA` called still gates on correctly. Four new bare Forth words extend the existing control surface, wired exactly like `WAVE`/`DUTY`/`ONA`/`RING-PARTNER`/`RING-OFF`.

**Tech Stack:** C (i686-elf-gcc cross-compiler), fixed-point sample counting (no floating point), this kernel's existing synth engine / Forth VM infrastructure.

**Spec:** `docs/superpowers/specs/2026-08-25-synth-arpeggio-design.md`

## Global Constraints

- No floating point anywhere (`kernel/Makefile`'s `CFLAGS` carries `-mgeneral-regs-only`) -- arpeggio timing uses the same "convert milliseconds to a per-sample counter with integer division" shape `synth_calc_rate()` already established for ADSR.
- Up to 4 notes per voice (`SYNTH_ARP_NOTES`), absolute `ona` values (1-88, the same scale `ONA` already uses), cycled up-only with wraparound -- no other pattern order.
- Fully per-voice: notes, activation, and step rate are all independent per voice (contrast with the shared filter, which is deliberately one global instance).
- The note list is independent of the voice's plain `ona`: `phase_increment` is a single shared destination both write to, with no separate stored "plain ona" value to fall back to. `ARP-OFF` stops stepping; it does not restore any prior pitch.
- `GATE-ON` always restarts the pattern at note slot 0, and re-primes `phase_increment` from it even if the voice's `ona` was never set (extending the existing "phase_increment == 0 means silent no-op" guard in `synth_gate_on()`, not replacing it).
- Stepping runs independently of envelope stage -- it keeps cycling through the release stage exactly like the oscillator phase already advances regardless of envelope stage today.
- Discrete indices with a meaningless out-of-range value (`ARP-NOTE`'s slot and note, `ARP-ON`'s count) hard-reject at the Forth layer via `forth_set_error()`, matching `ONA`/`RING-PARTNER`'s convention. Continuous tuning parameters (`ARP-RATE`'s milliseconds) clamp gracefully with no error, matching `DUTY`/`FILTER-CUTOFF`'s convention. The underlying C setters always clamp/no-op silently regardless -- the hard-vs-soft distinction is a Forth-layer-only concern, same as every existing synth word.

---

## Task 1: Core arpeggio engine

**Files:**
- Modify: `kernel/audio/synth.h`
- Modify: `kernel/audio/synth.c`
- Modify: `kernel/tests/test_synth.c`

**Interfaces:**
- Produces: `SYNTH_ARP_NOTES` (4), six new `struct synth_voice` fields (`arp_notes[SYNTH_ARP_NOTES]`, `arp_count`, `arp_active`, `arp_step`, `arp_step_rate`, `arp_step_counter`), `void synth_set_arp_note(int voice, int slot, int note)`, `void synth_arp_on(int voice, int count)`, `void synth_arp_off(int voice)`, `void synth_set_arp_rate(int voice, int ms)`. **Task 2 calls all four of these directly from its Forth hooks.**

Fully host-buildable and testable in isolation, same as Tasks 1-3 of the filter+ring-mod feature.

- [ ] **Step 1: Modify `kernel/audio/synth.h` -- new struct fields**

Replace:
```c
    /* 0 = straight to output (bypasses the shared filter); 1 = routed
     * through it. Matches real SID's own per-voice filter routing bits,
     * scaled to 8 voices. */
    int filter_route;
};
```
with:
```c
    /* 0 = straight to output (bypasses the shared filter); 1 = routed
     * through it. Matches real SID's own per-voice filter routing bits,
     * scaled to 8 voices. */
    int filter_route;
    /* Arpeggio: up to SYNTH_ARP_NOTES absolute ona values (arp_notes[]),
     * cycled through up-only-with-wraparound while arp_active, using
     * only the first arp_count slots. Independent of the voice's plain
     * ona -- synth_set_ona() and the arpeggio's own stepping both write
     * directly into phase_increment, the only thing that decides pitch;
     * there is no separate stored "plain ona" value either one falls
     * back to. arp_active is a flag independent of arp_notes/arp_count
     * (mirrors ring_partner's own "activation independent of loaded
     * configuration" shape, just as an explicit flag instead of a sign
     * convention since 0 is a valid slot value here) so ARP-OFF then
     * ARP-ON later doesn't require reloading notes. arp_step_rate/
     * arp_step_counter are a plain sample countdown-to-threshold (not
     * Q8-scaled like the envelope rates above -- see
     * synth_calc_arp_step_samples()). */
    int arp_notes[SYNTH_ARP_NOTES];
    int arp_count;
    int arp_active;
    int arp_step;
    int arp_step_rate;
    int arp_step_counter;
};
```

- [ ] **Step 2: Modify `kernel/audio/synth.h` -- the `SYNTH_ARP_NOTES` constant and new declarations**

Replace:
```c
#define SYNTH_SAMPLE_RATE 22050u
#define SYNTH_NUM_VOICES 8
#define SYNTH_ENV_FULL 32768
```
with:
```c
#define SYNTH_SAMPLE_RATE 22050u
#define SYNTH_NUM_VOICES 8
#define SYNTH_ENV_FULL 32768
#define SYNTH_ARP_NOTES 4
```

Replace:
```c
void synth_set_voice_filter_route(int voice, int routed);
```
with:
```c
void synth_set_voice_filter_route(int voice, int routed);
void synth_set_arp_note(int voice, int slot, int note);
void synth_arp_on(int voice, int count);
void synth_arp_off(int voice);
void synth_set_arp_rate(int voice, int ms);
```

- [ ] **Step 3: Modify `synth_init()` in `kernel/audio/synth.c`**

Replace:
```c
        synth_voices[v].ring_partner = -1;
        synth_voices[v].filter_route = 0;
    }
    synth_filter.lp = 0;
    synth_filter.bp = 0;
    synth_filter_silence_hist_count = 0;
}
```
with:
```c
        synth_voices[v].ring_partner = -1;
        synth_voices[v].filter_route = 0;
        {
            int slot;
            for (slot = 0; slot < SYNTH_ARP_NOTES; slot++) {
                /* 1 (not 0) so a stray read while arp_active == 0 (it
                 * never should be, but this stays a safe, valid ona
                 * index regardless -- see the struct field's comment
                 * on arp_active gating everything) can never index
                 * ona_phase_increment[] out of bounds. */
                synth_voices[v].arp_notes[slot] = 1;
            }
        }
        synth_voices[v].arp_count = 0;
        synth_voices[v].arp_active = 0;
        synth_voices[v].arp_step = 0;
        synth_voices[v].arp_step_rate = 1;
        synth_voices[v].arp_step_counter = 0;
    }
    synth_filter.lp = 0;
    synth_filter.bp = 0;
    synth_filter_silence_hist_count = 0;
}
```

- [ ] **Step 4: Modify `kernel/audio/synth.c` -- add the timing helper and four setters, right after `synth_set_voice_filter_route()`**

Replace:
```c
void synth_set_voice_filter_route(int voice, int routed) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    synth_voices[voice].filter_route = routed ? 1 : 0;
}
```
with:
```c
void synth_set_voice_filter_route(int voice, int routed) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    synth_voices[voice].filter_route = routed ? 1 : 0;
}

/* Same "clamp duration, convert to samples, guard the zero case" shape
 * synth_calc_rate() already uses for envelope timing -- but a plain
 * sample countdown-to-threshold, not a per-sample increment added to
 * an accumulating level, so no Q8 scaling here: the result is used
 * directly as arp_step_rate. The 10000ms upper clamp is deliberately
 * tighter than synth_calc_rate()'s 100000ms -- an arpeggio slower than
 * 10 seconds/step stops sounding like an arpeggio at all. */
static int synth_calc_arp_step_samples(int ms) {
    unsigned int samples;
    if (ms <= 0) {
        /* Fastest possible step, one sample -- the arp equivalent of
         * synth_calc_rate()'s own "duration_ms <= 0 means instant"
         * guard, just returning a minimum step count directly instead
         * of a maximum envelope rate. */
        return 1;
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

void synth_set_arp_note(int voice, int slot, int note) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    if (slot < 0 || slot >= SYNTH_ARP_NOTES) {
        return;
    }
    if (note < 1 || note > 88) {
        return;
    }
    synth_voices[voice].arp_notes[slot] = note;
}

void synth_arp_on(int voice, int count) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    if (count < 2 || count > SYNTH_ARP_NOTES) {
        return;
    }
    synth_voices[voice].arp_count = count;
    synth_voices[voice].arp_active = 1;
}

void synth_arp_off(int voice) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    synth_voices[voice].arp_active = 0;
}

void synth_set_arp_rate(int voice, int ms) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    synth_voices[voice].arp_step_rate = synth_calc_arp_step_samples(ms);
}
```

- [ ] **Step 5: Modify `synth_gate_on()` in `kernel/audio/synth.c`**

Replace:
```c
void synth_gate_on(int voice) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    /* A voice with no ona ever set has phase_increment == 0: its phase
     * accumulator never advances or wraps, so every waveform would
     * output a constant, non-silent value for as long as the envelope
     * stays nonzero -- a full-scale DC click/thump instead of silence.
     * Silently no-op, matching this codebase's existing convention for
     * a meaningless call (e.g. sb16_play_buffer()'s len == 0 case). */
    if (synth_voices[voice].phase_increment == 0) {
        return;
    }
    synth_voices[voice].envelope_level = 0;
    synth_voices[voice].envelope_stage = ENV_ATTACK;
}
```
with:
```c
void synth_gate_on(int voice) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    /* An arpeggiating voice may legitimately have never had ONA set --
     * its pitch comes entirely from arp_notes[]. Prime phase_increment
     * from the pattern's first note (and restart the pattern at slot 0,
     * matching the spec's "GATE-ON always restarts from note 0")
     * before the phase_increment == 0 check below, or a voice that
     * only ever used ARP-NOTE/ARP-ON would incorrectly hit that check
     * and silently fail to gate on at all. */
    if (synth_voices[voice].arp_active) {
        synth_voices[voice].arp_step = 0;
        synth_voices[voice].arp_step_counter = 0;
        synth_voices[voice].phase_increment =
            ona_phase_increment[synth_voices[voice].arp_notes[0] - 1];
    }
    /* A voice with no ona ever set has phase_increment == 0: its phase
     * accumulator never advances or wraps, so every waveform would
     * output a constant, non-silent value for as long as the envelope
     * stays nonzero -- a full-scale DC click/thump instead of silence.
     * Silently no-op, matching this codebase's existing convention for
     * a meaningless call (e.g. sb16_play_buffer()'s len == 0 case). */
    if (synth_voices[voice].phase_increment == 0) {
        return;
    }
    synth_voices[voice].envelope_level = 0;
    synth_voices[voice].envelope_stage = ENV_ATTACK;
}
```

- [ ] **Step 6: Modify `synth_render_half()` in `kernel/audio/synth.c`**

Replace:
```c
            voice->phase_accum = new_accum;
        }

        filtered_out = synth_filter_process_sample(&synth_filter, filtered_sum,
```
with:
```c
            voice->phase_accum = new_accum;

            /* Mechanically just "call the equivalent of ONA
             * automatically, on a timer": phase_increment is the only
             * thing that changes, so ring modulation and filter
             * routing both keep working completely unaware an
             * arpeggio is even happening. Runs after the phase
             * accumulator's own advance above, so a step that fires
             * this sample changes pitch starting next sample, not
             * retroactively this one. arp_count is guaranteed >= 2
             * whenever arp_active is 1 (synth_arp_on()'s own
             * validation), so the modulo below can never divide by
             * zero. */
            if (voice->arp_active) {
                voice->arp_step_counter++;
                if (voice->arp_step_counter >= voice->arp_step_rate) {
                    voice->arp_step_counter = 0;
                    voice->arp_step = (voice->arp_step + 1) % voice->arp_count;
                    voice->phase_increment =
                        ona_phase_increment[voice->arp_notes[voice->arp_step] - 1];
                }
            }
        }

        filtered_out = synth_filter_process_sample(&synth_filter, filtered_sum,
```

- [ ] **Step 7: Append tests to `kernel/tests/test_synth.c`**

Append, right before `int main(void) {`:
```c
static void test_arp_setters_validate(void) {
    synth_init();
    synth_set_arp_note(0, 0, 1);
    synth_set_arp_note(0, -1, 5);
    synth_set_arp_note(0, 4, 5);
    synth_set_arp_note(0, 1, 0);
    synth_set_arp_note(0, 1, 89);
    CHECK(synth_voices[0].arp_notes[0] == 1, "valid ARP-NOTE call sets the slot");
    CHECK(synth_voices[0].arp_notes[1] == 1, "out-of-range slot/note calls are silently ignored, slot 1 keeps its synth_init() default");

    synth_arp_on(0, 1);
    CHECK(synth_voices[0].arp_active == 0, "ARP-ON with count < 2 is rejected");
    synth_arp_on(0, 5);
    CHECK(synth_voices[0].arp_active == 0, "ARP-ON with count > 4 is rejected");
    synth_arp_on(0, 3);
    CHECK(synth_voices[0].arp_active == 1 && synth_voices[0].arp_count == 3, "ARP-ON with a valid count activates");

    synth_arp_off(0);
    CHECK(synth_voices[0].arp_active == 0, "ARP-OFF deactivates");
}

static void test_arp_rate_clamps_gracefully(void) {
    synth_init();
    synth_set_arp_rate(0, 0);
    CHECK(synth_voices[0].arp_step_rate == 1, "0ms clamps up to the 1-sample minimum, not a divide-by-zero");
    synth_set_arp_rate(0, -50);
    CHECK(synth_voices[0].arp_step_rate == 1, "negative ms also clamps to the 1-sample minimum");
    synth_set_arp_rate(0, 1);
    CHECK(synth_voices[0].arp_step_rate == 22, "1ms converts to 22 samples at SYNTH_SAMPLE_RATE=22050");
    synth_set_arp_rate(0, 1000000);
    CHECK(synth_voices[0].arp_step_rate == 220500, "extreme ms clamps to the 10-second maximum (10000ms -> 220500 samples)");
}

static void test_arp_stepping_order_two_notes(void) {
    unsigned char buf[1];
    int i;
    synth_init();
    synth_set_arp_note(0, 0, 1);
    synth_set_arp_note(0, 1, 2);
    synth_arp_on(0, 2);
    synth_set_arp_rate(0, 1);
    synth_gate_on(0);
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[0], "arp primes phase_increment from note slot 0 at gate-on");
    for (i = 0; i < 21; i++) {
        synth_render_half(buf, 1);
    }
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[0], "phase_increment unchanged before the first step boundary (21 samples in)");
    synth_render_half(buf, 1);
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[1], "phase_increment steps to note slot 1 at the 22nd sample");
    for (i = 0; i < 21; i++) {
        synth_render_half(buf, 1);
    }
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[1], "phase_increment unchanged before the second step boundary");
    synth_render_half(buf, 1);
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[0], "phase_increment wraps back to note slot 0 after 2 steps");
}

static void test_arp_stepping_order_four_notes_wraps(void) {
    unsigned char buf[1];
    int i;
    synth_init();
    synth_set_arp_note(0, 0, 1);
    synth_set_arp_note(0, 1, 2);
    synth_set_arp_note(0, 2, 3);
    synth_set_arp_note(0, 3, 4);
    synth_arp_on(0, 4);
    synth_set_arp_rate(0, 1);
    synth_gate_on(0);
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[0], "primed at note slot 0");
    for (i = 0; i < 22; i++) { synth_render_half(buf, 1); }
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[1], "steps to slot 1 after 22 samples");
    for (i = 0; i < 22; i++) { synth_render_half(buf, 1); }
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[2], "steps to slot 2 after 44 samples");
    for (i = 0; i < 22; i++) { synth_render_half(buf, 1); }
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[3], "steps to slot 3 after 66 samples");
    for (i = 0; i < 22; i++) { synth_render_half(buf, 1); }
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[0], "wraps back to slot 0 after 88 samples (4 steps)");
}

static void test_gate_on_restarts_arp_from_note_zero(void) {
    unsigned char buf[1];
    int i;
    synth_init();
    synth_set_arp_note(0, 0, 1);
    synth_set_arp_note(0, 1, 2);
    synth_arp_on(0, 2);
    synth_set_arp_rate(0, 1);
    synth_gate_on(0);
    for (i = 0; i < 22; i++) {
        synth_render_half(buf, 1);
    }
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[1], "arp has stepped to note slot 1 before the gate is retriggered");
    synth_gate_on(0);
    CHECK(synth_voices[0].arp_step == 0, "GATE-ON resets the arp step index to 0");
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[0], "GATE-ON re-primes phase_increment from note slot 0");
}

static void test_gate_on_primes_arp_voice_that_never_had_ona(void) {
    synth_init();
    synth_set_arp_note(0, 0, 5);
    synth_set_arp_note(0, 1, 9);
    synth_arp_on(0, 2);
    CHECK(synth_voices[0].phase_increment == 0, "freshly-init voice still has phase_increment == 0 before gate-on (ona never set)");
    synth_gate_on(0);
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[4], "gate-on primes phase_increment from the arp's first note even though ona was never set");
    CHECK(synth_voices[0].envelope_stage == ENV_ATTACK, "gate-on enters ATTACK for an arpeggiating voice, unlike a plain never-ona'd voice");
}

static void test_arp_continues_stepping_through_release(void) {
    unsigned char buf[1];
    int i;
    synth_init();
    synth_set_arp_note(0, 0, 1);
    synth_set_arp_note(0, 1, 2);
    synth_arp_on(0, 2);
    synth_set_arp_rate(0, 1);
    synth_gate_on(0);
    synth_gate_off(0);
    for (i = 0; i < 21; i++) {
        synth_render_half(buf, 1);
    }
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[0], "still on note slot 0 just before the step boundary, even mid-release");
    synth_render_half(buf, 1);
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[1], "arp keeps stepping to note slot 1 during release, per spec");
}

static void test_arp_off_stops_stepping(void) {
    unsigned char buf[1];
    int i;
    synth_init();
    synth_set_arp_note(0, 0, 1);
    synth_set_arp_note(0, 1, 2);
    synth_arp_on(0, 2);
    synth_set_arp_rate(0, 1);
    synth_gate_on(0);
    for (i = 0; i < 22; i++) {
        synth_render_half(buf, 1);
    }
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[1], "arp has stepped to note slot 1 before ARP-OFF");
    synth_arp_off(0);
    for (i = 0; i < 100; i++) {
        synth_render_half(buf, 1);
    }
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[1], "phase_increment stays frozen at the last arp note once ARP-OFF stops stepping");
}

static void test_arp_on_after_off_resumes_with_loaded_notes(void) {
    unsigned char buf[1];
    int i;
    synth_init();
    synth_set_arp_note(0, 0, 1);
    synth_set_arp_note(0, 1, 2);
    synth_arp_on(0, 2);
    synth_set_arp_rate(0, 1);
    synth_arp_off(0);
    synth_gate_on(0);
    CHECK(synth_voices[0].phase_increment == 0, "arp inactive at gate-on, and ona was never set, so phase_increment stays 0");
    CHECK(synth_voices[0].envelope_stage == ENV_OFF, "gate-on no-ops when arp is inactive and ona was never set (same as a plain never-ona'd voice)");
    synth_arp_on(0, 2);
    synth_gate_on(0);
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[0], "re-activating arp (no new ARP-NOTE calls) and gating on resumes with the previously-loaded notes");
    for (i = 0; i < 22; i++) {
        synth_render_half(buf, 1);
    }
    CHECK(synth_voices[0].phase_increment == ona_phase_increment[1], "stepping resumes normally after re-activation");
}

static void test_synth_init_resets_arp_state(void) {
    synth_init();
    synth_set_arp_note(0, 0, 10);
    synth_arp_on(0, 3);
    synth_set_arp_rate(0, 50);
    synth_gate_on(0);
    synth_init();
    CHECK(synth_voices[0].arp_active == 0, "synth_init() resets arp_active");
    CHECK(synth_voices[0].arp_count == 0, "synth_init() resets arp_count");
    CHECK(synth_voices[0].arp_step == 0, "synth_init() resets arp_step");
    CHECK(synth_voices[0].arp_step_counter == 0, "synth_init() resets arp_step_counter");
    CHECK(synth_voices[0].arp_step_rate == 1, "synth_init() resets arp_step_rate to a safe default");
    CHECK(synth_voices[0].arp_notes[0] == 1, "synth_init() resets arp_notes[] to the safe default");
}

static void test_arp_voice_still_respects_filter_route_and_ring_mod(void) {
    unsigned char buf[1];
    int i;
    synth_init();
    synth_set_voice_waveform(0, WAVE_TRIANGLE);
    synth_set_arp_note(0, 0, 40);
    synth_set_arp_note(0, 1, 42);
    synth_arp_on(0, 2);
    synth_set_arp_rate(0, 1);
    synth_set_voice_filter_route(0, 1);
    synth_set_ring_partner(0, 1);
    synth_set_voice_waveform(1, WAVE_PULSE);
    synth_set_ona(1, 40);
    synth_gate_on(0);
    synth_gate_on(1);
    for (i = 0; i < 50; i++) {
        synth_render_half(buf, 1);
    }
    CHECK(synth_voices[0].filter_route == 1, "arpeggiating voice keeps its filter_route setting untouched by stepping");
    CHECK(synth_voices[0].ring_partner == 1, "arpeggiating voice keeps its ring_partner setting untouched by stepping");
}
```

Then replace:
```c
    test_filter_process_sample_converges_to_true_zero_under_zero_input();
    test_render_half_returns_to_true_silence_after_filtered_note_release();

    if (failures == 0) {
```
with:
```c
    test_filter_process_sample_converges_to_true_zero_under_zero_input();
    test_render_half_returns_to_true_silence_after_filtered_note_release();
    test_arp_setters_validate();
    test_arp_rate_clamps_gracefully();
    test_arp_stepping_order_two_notes();
    test_arp_stepping_order_four_notes_wraps();
    test_gate_on_restarts_arp_from_note_zero();
    test_gate_on_primes_arp_voice_that_never_had_ona();
    test_arp_continues_stepping_through_release();
    test_arp_off_stops_stepping();
    test_arp_on_after_off_resumes_with_loaded_notes();
    test_synth_init_resets_arp_state();
    test_arp_voice_still_respects_filter_route_and_ring_mod();

    if (failures == 0) {
```

- [ ] **Step 8: Build and run the test**

```bash
gcc -m32 kernel/tests/test_synth.c kernel/audio/synth.c -o /tmp/test_synth && /tmp/test_synth
```
Expected: `PASS` (46 tests total, up from 35).

- [ ] **Step 9: Confirm freestanding compilation is still clean**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd kernel
make clean
make
```
Expected: builds clean, no new warnings (the pre-existing `kernel.elf has a LOAD segment with RWX permissions` linker warning is expected and unrelated).

- [ ] **Step 10: Commit**

```bash
git add kernel/audio/synth.h kernel/audio/synth.c kernel/tests/test_synth.c
git commit -m "audio: per-voice arpeggio engine (notes, timing, stepping)"
```

---

## Task 2: The Forth control surface

**Files:**
- Modify: `kernel/forth/forth_hooks.h`
- Modify: `kernel/kernel.c`
- Modify: `kernel/forth/forth.c`

**Interfaces:**
- Consumes: `synth_set_arp_note()`, `synth_arp_on()`, `synth_arp_off()`, `synth_set_arp_rate()` (all from Task 1), and the existing `synth_current_voice`/`audio_ensure_stream_started()` from the already-shipped engine's Forth wiring.
- Produces: four Forth words `ARP-NOTE`, `ARP-ON`, `ARP-OFF`, `ARP-RATE`.

Not host-buildable in isolation the same way Task 1 is (this wires into the real Forth VM and the already-shipped audio-stream machinery) -- verified via headless QEMU, same pattern the filter+ring-mod feature's own Forth task used.

- [ ] **Step 1: Modify `kernel/forth/forth_hooks.h`**

Replace:
```c
/* Sub-project (B): the 8-voice synthesizer's manual control surface.
 * VOICE selects which of the 8 voices WAVE/DUTY/ONA/ADSR/GATE-ON/
 * GATE-OFF act on -- the same "select a context, then act on it" shape
 * PAINT's own current_color selection already uses in this codebase,
 * just driven by a Forth word instead of a mouse click since there's
 * no picker UI for voices. See
 * docs/superpowers/specs/2026-08-23-sid-synth-design.md. */
void forth_hook_synth_voice(int voice);
void forth_hook_synth_wave(int wave);
void forth_hook_synth_duty(int duty_percent);
void forth_hook_synth_ona(int ona);
void forth_hook_synth_adsr(int attack_ms, int decay_ms, int sustain_percent, int release_ms);
void forth_hook_synth_gate_on(void);
void forth_hook_synth_gate_off(void);
void forth_hook_synth_filter_cutoff(int cutoff);
void forth_hook_synth_filter_res(int resonance);
void forth_hook_synth_filter_mode(int mode_mask);
void forth_hook_synth_filter_route(int routed);
void forth_hook_synth_ring_partner(int partner);
void forth_hook_synth_ring_off(void);

#endif
```
with:
```c
/* Sub-project (B): the 8-voice synthesizer's manual control surface.
 * VOICE selects which of the 8 voices WAVE/DUTY/ONA/ADSR/GATE-ON/
 * GATE-OFF/FILTER-ROUTE/RING-PARTNER/RING-OFF/ARP-NOTE/ARP-ON/ARP-OFF/
 * ARP-RATE act on -- the same "select a context, then act on it" shape
 * PAINT's own current_color selection already uses in this codebase,
 * just driven by a Forth word instead of a mouse click since there's
 * no picker UI for voices. FILTER-CUTOFF/FILTER-RES/FILTER-MODE are
 * the exception -- they set the one shared filter's global settings,
 * not anything per-voice. See
 * docs/superpowers/specs/2026-08-23-sid-synth-design.md,
 * docs/superpowers/specs/2026-08-23-sid-filter-ringmod-design.md, and
 * docs/superpowers/specs/2026-08-25-synth-arpeggio-design.md. */
void forth_hook_synth_voice(int voice);
void forth_hook_synth_wave(int wave);
void forth_hook_synth_duty(int duty_percent);
void forth_hook_synth_ona(int ona);
void forth_hook_synth_adsr(int attack_ms, int decay_ms, int sustain_percent, int release_ms);
void forth_hook_synth_gate_on(void);
void forth_hook_synth_gate_off(void);
void forth_hook_synth_filter_cutoff(int cutoff);
void forth_hook_synth_filter_res(int resonance);
void forth_hook_synth_filter_mode(int mode_mask);
void forth_hook_synth_filter_route(int routed);
void forth_hook_synth_ring_partner(int partner);
void forth_hook_synth_ring_off(void);
void forth_hook_synth_arp_note(int note, int slot);
void forth_hook_synth_arp_on(int count);
void forth_hook_synth_arp_off(void);
void forth_hook_synth_arp_rate(int ms);

#endif
```

(This also fixes a known, previously-deferred documentation gap: the comment above was already stale before this task, missing `FILTER-ROUTE`/`RING-PARTNER`/`RING-OFF` from the prior feature -- fixed here since this task has to touch the same lines anyway.)

- [ ] **Step 2: Modify `kernel/kernel.c` -- the four new hooks**

Replace:
```c
void forth_hook_synth_ring_off(void) {
    synth_clear_ring_partner(synth_current_voice);
}
```
with:
```c
void forth_hook_synth_ring_off(void) {
    synth_clear_ring_partner(synth_current_voice);
}

void forth_hook_synth_arp_note(int note, int slot) {
    audio_ensure_stream_started();
    synth_set_arp_note(synth_current_voice, slot, note);
}

void forth_hook_synth_arp_on(int count) {
    audio_ensure_stream_started();
    synth_arp_on(synth_current_voice, count);
}

void forth_hook_synth_arp_off(void) {
    synth_arp_off(synth_current_voice);
}

void forth_hook_synth_arp_rate(int ms) {
    audio_ensure_stream_started();
    synth_set_arp_rate(synth_current_voice, ms);
}
```

- [ ] **Step 3: Modify `kernel/forth/forth.c` -- the four new primitives**

Replace:
```c
static void prim_synth_ring_off(struct forth_vm *vm) {
    (void)vm;
    forth_hook_synth_ring_off();
}
```
with:
```c
static void prim_synth_ring_off(struct forth_vm *vm) {
    (void)vm;
    forth_hook_synth_ring_off();
}

/* ARP-NOTE: both slot (0-3) and note (1-88) are discrete indices with
 * a meaningless out-of-range value -- hard-error, matching ONA/
 * RING-PARTNER's convention. slot in particular writing out of bounds
 * would corrupt adjacent voice state, not just misbehave musically. */
static void prim_synth_arp_note(struct forth_vm *vm) {
    int32_t note, slot;
    if (!forth_pop(vm, &slot) || !forth_pop(vm, &note)) {
        return;
    }
    if (slot < 0 || slot > 3) {
        forth_set_error(vm, "BAD ARP SLOT");
        return;
    }
    if (note < 1 || note > 88) {
        forth_set_error(vm, "BAD ARP NOTE");
        return;
    }
    forth_hook_synth_arp_note((int)note, (int)slot);
}

/* ARP-ON's count (2-4) is hard-rejected too -- letting an out-of-range
 * count through would read arp_notes[] out of bounds in the render
 * loop, not just misbehave musically. */
static void prim_synth_arp_on(struct forth_vm *vm) {
    int32_t n;
    if (!forth_pop(vm, &n)) {
        return;
    }
    if (n < 2 || n > 4) {
        forth_set_error(vm, "BAD ARP COUNT");
        return;
    }
    forth_hook_synth_arp_on((int)n);
}

static void prim_synth_arp_off(struct forth_vm *vm) {
    (void)vm;
    forth_hook_synth_arp_off();
}

/* ARP-RATE: like DUTY/FILTER-CUTOFF, synth_set_arp_rate() already
 * clamps gracefully -- no hard Forth-level error, matching DUTY's own
 * convention rather than VOICE/WAVE/ONA/ARP-NOTE/ARP-ON's. */
static void prim_synth_arp_rate(struct forth_vm *vm) {
    int32_t n;
    if (!forth_pop(vm, &n)) {
        return;
    }
    forth_hook_synth_arp_rate((int)n);
}
```

- [ ] **Step 4: Modify `kernel/forth/forth.c` -- the primitive table**

Replace:
```c
    {"RING-PARTNER", prim_synth_ring_partner}, {"RING-OFF", prim_synth_ring_off},
```
with:
```c
    {"RING-PARTNER", prim_synth_ring_partner}, {"RING-OFF", prim_synth_ring_off},
    {"ARP-NOTE", prim_synth_arp_note}, {"ARP-ON", prim_synth_arp_on},
    {"ARP-OFF", prim_synth_arp_off}, {"ARP-RATE", prim_synth_arp_rate},
```

- [ ] **Step 5: Full clean rebuild**

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
cd kernel
make clean
make
```
Expected: builds clean, no new warnings (the pre-existing `kernel.elf has a LOAD segment with RWX permissions` linker warning is expected and unrelated).

```bash
cd ../boot
make
```
Expected: builds clean.

- [ ] **Step 6: Re-run the full host test suite (regression check)**

```bash
cd ../kernel
gcc -m32 tests/test_synth.c audio/synth.c -o /tmp/test_synth && /tmp/test_synth
nasm -f elf32 sched/context_switch.asm -o /tmp/cs_host.o
gcc -m32 tests/test_context_switch.c /tmp/cs_host.o -o /tmp/tcs && /tmp/tcs
gcc -m32 tests/test_font.c gfx/font.c -o /tmp/tf && /tmp/tf
gcc -m32 tests/test_scheduler.c sched/scheduler.c /tmp/cs_host.o -o /tmp/tsc && /tmp/tsc
gcc -m32 -Igfx -Igui tests/test_editor.c gui/editor.c -o /tmp/ted && /tmp/ted
rm -f /tmp/cs_host.o
```
Expected: `PASS` from all five.

- [ ] **Step 7: Headless QEMU verification**

Boot with `-device sb16,audiodev=snd0 -audiodev wav,id=snd0,path=<tmp>.wav`, same pattern as the filter+ring-mod feature's own verification. Drive the FORTH console via the monitor socket. First, a static single-note baseline:
```
0 VOICE 1 WAVE 1 ONA 10 50 80 500 ADSR GATE-ON
```
(voice 0, sawtooth, ona 1 -- a very low, distinctive pitch -- gated on, no arpeggio yet). Wait at least 1 second of sustain, capture that as WAV region A. Then load and activate a wide-spanning 3-note arpeggio at a fast rate:
```
1 0 ARP-NOTE 40 1 ARP-NOTE 80 2 ARP-NOTE 3 ARP-ON 2 ARP-RATE
```
(slot 0 = ona 1, slot 1 = ona 40, slot 2 = ona 80 -- deliberately wide-spaced so the pitch cycling is unmistakable in a zero-crossing-rate measurement, not just a subtle wobble; 2ms/step, fast enough to read as a chiptune-style "sizzling" arpeggio rather than a slow sweep). Wait at least 1 second of sustain, capture that as WAV region B. Then:
```
GATE-OFF
```
Wait for release, `quit` over the monitor for a clean exit.

Inspect the resulting WAV file: confirm region A (static ona 1) has a low, roughly constant zero-crossing rate consistent with one steady low-pitched sawtooth. Confirm region B (arpeggio active) has a measurably different zero-crossing rate, consistent with rapid, repeated jumps across three widely-spaced pitches -- not just non-silence, real evidence the pitch is actually cycling (a bug that made `ARP-ON` a no-op would still pass a bare non-silence check).

Report the exact frame count, sample rate, and both regions' zero-crossing-rate measurements in the task report, following this project's established WAV-inspection convention.

- [ ] **Step 8: Commit**

```bash
git add kernel/forth/forth_hooks.h kernel/kernel.c kernel/forth/forth.c
git commit -m "audio: arpeggio Forth control surface (ARP-NOTE/ON/OFF/RATE)"
```

---

## Task 3: Documentation

**Files:**
- Modify: `docs/IDEAS.md`
- Modify: `docs/BUILD_LOG.md`

**Interfaces:** None (docs only).

- [ ] **Step 1: Update `docs/IDEAS.md`**

Find the Audio entry (already struck through, with sub-projects (A) and (B) -- including the filter and ring modulation -- noted as done, and (C) flagged as remaining future work; this is the same entry the filter+ring-mod feature's own paragraph was appended to). Append a new paragraph to that same entry (do not create a new bullet) recording that a per-voice arpeggio effect has now shipped too: an explicit 2-4 note list per voice, up-only-wrap, sample-accurate millisecond timing, fully independent of the shared filter/ring-mod machinery. State explicitly this is a separate, self-contained addition, not part of (and does not replace the need for) sub-project (C)'s still-unbuilt real note-sequencing language. Reference `docs/superpowers/specs/2026-08-25-synth-arpeggio-design.md` and this entry's `docs/BUILD_LOG.md` counterpart.

- [ ] **Step 2: Add a `docs/BUILD_LOG.md` entry**

New entry at the end of the file, following this file's established house style (compare the most recent few entries, especially the filter+ring-mod entry, for format: bold sub-section leads, dense technical prose, backtick-quoted symbols/files, a trailing `Files:` line). Must include, using the **actual** results from Tasks 1-2's reports (not invented numbers):

- What was built: the core per-voice arpeggio engine -- data model, timing conversion, render-loop stepping, the `GATE-ON` priming fix (Task 1) -- and the Forth control surface (Task 2), one paragraph each is reasonable, matching prior entries' own per-task structure.
- How it was verified: the exact host-test pass/fail results from Task 1 (note `kernel/tests/test_synth.c` grew further, still part of the same five-suite host regression run), and Task 2's actual headless-QEMU zero-crossing-rate comparison between the static baseline and the active arpeggio -- copied from that task's real report, not the illustrative numbers in this plan.
- **A real-hardware verification pass is still pending**, same bar every prior piece of this feature has been held to.
- Any deferred findings from Task 2's own review (if the review process surfaces any) recorded as known gaps, following this project's established "known minor gaps, deferred, not fixed here" convention.
- A `Files:` line listing every file touched across both tasks.

- [ ] **Step 3: Commit**

```bash
git add docs/IDEAS.md docs/BUILD_LOG.md
git commit -m "docs: close out the synth arpeggio follow-up"
```
