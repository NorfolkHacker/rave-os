#include <stdio.h>
#include "../audio/synth.h"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        failures++; \
    } \
} while (0)

static void test_ona_table(void) {
    int i;
    CHECK(ona_phase_increment[0] == 5356535u, "ona 1 (A0) phase increment");
    CHECK(ona_phase_increment[48] == 85704563u, "ona 49 (A4=440Hz) phase increment");
    CHECK(ona_phase_increment[87] == 815363807u, "ona 88 (C8) phase increment");
    for (i = 1; i < 88; i++) {
        CHECK(ona_phase_increment[i] > ona_phase_increment[i - 1], "ona table monotonically increasing");
    }
}

static void test_waveform_saw(void) {
    unsigned int lfsr = 1;
    CHECK(synth_osc_sample(WAVE_SAW, 0x00000000u, 128, &lfsr, 0) == -128, "saw at phase 0");
    CHECK(synth_osc_sample(WAVE_SAW, 0x80000000u, 128, &lfsr, 0) == 0, "saw at phase midpoint");
    CHECK(synth_osc_sample(WAVE_SAW, 0xFF000000u, 128, &lfsr, 0) == 127, "saw at phase near-end");
}

static void test_waveform_triangle(void) {
    unsigned int lfsr = 1;
    CHECK(synth_osc_sample(WAVE_TRIANGLE, 0x00000000u, 128, &lfsr, 0) == -128, "triangle at phase 0");
    CHECK(synth_osc_sample(WAVE_TRIANGLE, 0x40000000u, 128, &lfsr, 0) == 0, "triangle at quarter phase");
    CHECK(synth_osc_sample(WAVE_TRIANGLE, 0x80000000u, 128, &lfsr, 0) == 126, "triangle at phase midpoint (peak)");
    CHECK(synth_osc_sample(WAVE_TRIANGLE, 0xFF000000u, 128, &lfsr, 0) == -128, "triangle at phase near-end");
}

static void test_waveform_pulse(void) {
    unsigned int lfsr = 1;
    CHECK(synth_osc_sample(WAVE_PULSE, 0x00000000u, 128, &lfsr, 0) == 127, "pulse below 50% duty");
    CHECK(synth_osc_sample(WAVE_PULSE, 0x7F000000u, 128, &lfsr, 0) == 127, "pulse just below 50% duty");
    CHECK(synth_osc_sample(WAVE_PULSE, 0x80000000u, 128, &lfsr, 0) == -128, "pulse at 50% duty threshold");
    CHECK(synth_osc_sample(WAVE_PULSE, 0xFF000000u, 128, &lfsr, 0) == -128, "pulse near end");
}

static void test_waveform_noise(void) {
    unsigned int lfsr_a = 1, lfsr_b = 1;
    int i;
    int varied = 0;
    int first = synth_osc_sample(WAVE_NOISE, 0, 128, &lfsr_a, 1);
    for (i = 0; i < 20; i++) {
        int s = synth_osc_sample(WAVE_NOISE, 0, 128, &lfsr_a, 1);
        if (s != first) {
            varied = 1;
        }
    }
    CHECK(varied, "noise output varies across samples");

    lfsr_a = 12345;
    lfsr_b = 12345;
    for (i = 0; i < 10; i++) {
        int sa = synth_osc_sample(WAVE_NOISE, 0, 128, &lfsr_a, 1);
        int sb = synth_osc_sample(WAVE_NOISE, 0, 128, &lfsr_b, 1);
        CHECK(sa == sb, "noise is deterministic given the same seed");
    }

    lfsr_a = 999;
    {
        int before = synth_osc_sample(WAVE_NOISE, 0, 128, &lfsr_a, 0);
        int after = synth_osc_sample(WAVE_NOISE, 0, 128, &lfsr_a, 0);
        CHECK(before == after, "noise holds steady when phase does not wrap");
    }
}

static void test_set_ona_bounds(void) {
    synth_init();
    synth_set_ona(0, 49);
    CHECK(synth_voices[0].phase_increment == 85704563u, "synth_set_ona(0, 49) sets A4's phase increment");
    synth_set_ona(0, 0);
    CHECK(synth_voices[0].phase_increment == 85704563u, "synth_set_ona ignores out-of-range ona (too low)");
    synth_set_ona(0, 89);
    CHECK(synth_voices[0].phase_increment == 85704563u, "synth_set_ona ignores out-of-range ona (too high)");
}

/* These tests drive envelope timing entirely through the public API
 * (synth_init/synth_set_adsr/synth_gate_on/off + synth_voices[]) rather
 * than hardcoding rate magic numbers calibrated to synth_calc_rate()'s
 * internal formula -- that way a future change to the formula (e.g. the
 * Q8 sub-unit fix) can't silently desync the tests from the real
 * implementation the way the old hardcoded 148/74/33 rates did. */

/* Counts how many synth_envelope_advance_sample() calls it takes voice 0
 * to leave `from_stage`, up to a generous sample ceiling. Returns -1 if
 * it never leaves within the ceiling. */
static int samples_until_stage_change(int voice, enum synth_env_stage from_stage, int max_samples) {
    int i;
    for (i = 0; i < max_samples; i++) {
        synth_envelope_advance_sample(&synth_voices[voice]);
        if (synth_voices[voice].envelope_stage != from_stage) {
            return i + 1;
        }
    }
    return -1;
}

static void test_envelope_attack_decay_sustain(void) {
    int attack_samples;
    int decay_samples;

    synth_init();
    synth_set_ona(0, 49);
    synth_set_adsr(0, 10, 20, 50, 40);
    synth_gate_on(0);

    attack_samples = samples_until_stage_change(0, ENV_ATTACK, 5000);
    CHECK(attack_samples > 0, "envelope reaches full scale and enters decay");
    CHECK(synth_voices[0].envelope_stage == ENV_DECAY, "envelope is in DECAY right after ATTACK completes");

    decay_samples = samples_until_stage_change(0, ENV_DECAY, 5000);
    CHECK(decay_samples > 0, "envelope decays into sustain");
    CHECK(synth_voices[0].envelope_stage == ENV_SUSTAIN, "envelope reaches SUSTAIN after DECAY completes");
    CHECK(synth_voices[0].envelope_level == synth_voices[0].sustain_level,
          "envelope holds at configured sustain level");

    {
        int i;
        for (i = 0; i < 500; i++) {
            synth_envelope_advance_sample(&synth_voices[0]);
        }
    }
    CHECK(synth_voices[0].envelope_stage == ENV_SUSTAIN, "envelope stays in sustain without gate-off");
    CHECK(synth_voices[0].envelope_level == synth_voices[0].sustain_level, "sustain level does not drift");
}

static void test_envelope_release_reaches_zero(void) {
    synth_init();
    synth_set_ona(0, 49);
    synth_set_adsr(0, 1, 1, 50, 40);
    synth_gate_on(0);
    /* Drive it through ATTACK and DECAY into SUSTAIN first. */
    samples_until_stage_change(0, ENV_ATTACK, 5000);
    samples_until_stage_change(0, ENV_DECAY, 5000);
    CHECK(synth_voices[0].envelope_stage == ENV_SUSTAIN, "voice reached sustain before release test begins");

    synth_gate_off(0);
    CHECK(samples_until_stage_change(0, ENV_RELEASE, 5000) > 0, "release eventually reaches OFF");
    CHECK(synth_voices[0].envelope_stage == ENV_OFF, "release stage transitions all the way to OFF");
    CHECK(synth_voices[0].envelope_level == 0, "released envelope level is exactly zero");
}

static void test_envelope_instant_on_zero_duration(void) {
    int level;
    synth_init();
    synth_set_ona(0, 49);
    synth_set_adsr(0, 0, 1, 0, 1);
    synth_gate_on(0);
    /* Check the function's return value (public 0..SYNTH_ENV_FULL
     * scale), not the raw envelope_level struct field -- that field is
     * Q8 sub-units internally, see synth.h's struct comment. */
    level = synth_envelope_advance_sample(&synth_voices[0]);
    CHECK(level == SYNTH_ENV_FULL, "zero-duration attack reaches full scale in one sample");
    CHECK(synth_voices[0].envelope_stage == ENV_DECAY, "zero-duration attack immediately enters decay");
}

static void test_envelope_never_stuck_at_extreme_duration(void) {
    synth_init();
    synth_set_ona(0, 49);
    /* Even a duration well past the old ~1.5s hard ceiling must still
     * make forward progress every sample and eventually complete. */
    synth_set_adsr(0, 60000, 1, 0, 1);
    synth_gate_on(0);
    CHECK(samples_until_stage_change(0, ENV_ATTACK, 22050 * 90) > 0,
          "even a very long requested attack duration eventually completes (rate never rounds to 0)");
}

/* Fix 1 regression coverage: synth_calc_rate()'s old plain (non-Q8)
 * division badly distorted requested millisecond durations for any
 * stage longer than ~200ms -- a requested 500ms attack actually took
 * ~743ms (+49%), and anything >= ~1486ms was silently clamped to
 * ~1486ms no matter what was requested (verified against the real code
 * path). The Q8 fix must make both of those cases accurate. */
static void test_envelope_500ms_attack_is_accurate(void) {
    int samples;
    double ms;
    synth_init();
    synth_set_ona(0, 49);
    synth_set_adsr(0, 500, 1, 0, 1);
    synth_gate_on(0);
    samples = samples_until_stage_change(0, ENV_ATTACK, 22050 * 5);
    CHECK(samples > 0, "500ms attack completes within a generous ceiling");
    ms = (samples * 1000.0) / SYNTH_SAMPLE_RATE;
    CHECK(ms >= 480.0 && ms <= 520.0, "500ms-requested attack completes within +-4% (was +49% / 743ms before the Q8 fix)");
}

static void test_envelope_5000ms_attack_is_not_clamped(void) {
    int samples;
    double ms;
    synth_init();
    synth_set_ona(0, 49);
    synth_set_adsr(0, 5000, 1, 0, 1);
    synth_gate_on(0);
    samples = samples_until_stage_change(0, ENV_ATTACK, 22050 * 8);
    CHECK(samples > 0, "5000ms attack completes within a generous ceiling");
    ms = (samples * 1000.0) / SYNTH_SAMPLE_RATE;
    CHECK(ms >= 4900.0 && ms <= 5100.0,
          "5000ms-requested attack is actually achievable (was clamped to ~1486ms before the Q8 fix)");
}

/* Fix 3 regression coverage: (unsigned)duration_ms * SYNTH_SAMPLE_RATE
 * overflows 32 bits above ~194783ms; just past that the product wraps
 * into a small number, inverting a very long requested duration into a
 * near-instant one. A clamp to 100000ms keeps duration_ms far below the
 * overflow threshold. */
static void test_envelope_overflow_duration_does_not_invert(void) {
    int samples;
    synth_init();
    synth_set_ona(0, 49);
    synth_set_adsr(0, 194784, 1, 0, 1);
    synth_gate_on(0);
    /* Pre-fix this completed in under 1ms (~20 samples); post-fix it
     * should take a very long time (clamped at 100000ms, i.e. hundreds
     * of thousands of samples) -- so just confirm it has NOT finished
     * within a sample budget that would only be reachable by the
     * overflow bug. */
    samples = samples_until_stage_change(0, ENV_ATTACK, 2000);
    CHECK(samples == -1, "an overflow-band requested duration does not collapse into a near-instant attack");
}

static void test_gate_on_off_transitions(void) {
    synth_init();
    synth_set_ona(0, 49);
    synth_set_adsr(0, 10, 20, 50, 40);
    CHECK(synth_voices[0].envelope_stage == ENV_OFF, "voice starts with envelope OFF");
    synth_gate_on(0);
    CHECK(synth_voices[0].envelope_stage == ENV_ATTACK, "gate-on moves OFF voice to ATTACK");
    synth_gate_off(0);
    CHECK(synth_voices[0].envelope_stage == ENV_RELEASE, "gate-off moves an active voice to RELEASE");
    synth_gate_off(0);
    CHECK(synth_voices[0].envelope_stage == ENV_RELEASE, "gate-off is a no-op from RELEASE (stays RELEASE)");
    {
        int i;
        for (i = 0; i < 2000 && synth_voices[0].envelope_stage != ENV_OFF; i++) {
            synth_envelope_advance_sample(&synth_voices[0]);
        }
    }
    CHECK(synth_voices[0].envelope_stage == ENV_OFF, "release fully completes back to OFF");
    synth_gate_off(0);
    CHECK(synth_voices[0].envelope_stage == ENV_OFF, "gate-off from OFF stays OFF (does not restart release)");
}

static void test_mixer_clamps_max_voices(void) {
    unsigned char buf[4];
    int v;

    synth_init();
    for (v = 0; v < SYNTH_NUM_VOICES; v++) {
        synth_set_voice_waveform(v, WAVE_PULSE);
        synth_set_duty(v, 50);
        synth_voices[v].phase_increment = 0;
        synth_voices[v].envelope_stage = ENV_SUSTAIN;
        /* envelope_level/sustain_level are Q8 sub-units internally --
         * see synth.h's struct comment -- so "full scale" here is
         * SYNTH_ENV_FULL << 8, not SYNTH_ENV_FULL. */
        synth_voices[v].envelope_level = SYNTH_ENV_FULL << 8;
        synth_voices[v].sustain_level = SYNTH_ENV_FULL << 8;
    }
    synth_render_half(buf, 4);
    CHECK(buf[0] == 255, "8 max-positive voices clamp to full-scale, no wraparound");

    for (v = 0; v < SYNTH_NUM_VOICES; v++) {
        synth_voices[v].phase_increment = 0;
        synth_voices[v].duty_threshold = 0;
    }
    synth_render_half(buf, 4);
    CHECK(buf[0] == 0, "8 max-negative voices clamp to zero, no wraparound");
}

static void test_mixer_silence_when_no_voices_gated(void) {
    unsigned char buf[8];
    unsigned int i;
    synth_init();
    for (i = 0; i < 8; i++) {
        buf[i] = 0xFF;
    }
    synth_render_half(buf, 8);
    for (i = 0; i < 8; i++) {
        CHECK(buf[i] == 128, "silent (never-gated) voices render as mid-point 128");
    }
}

static void test_mixer_single_voice_full_envelope_matches_oscillator(void) {
    unsigned char buf[1];
    synth_init();
    synth_set_voice_waveform(0, WAVE_SAW);
    synth_voices[0].phase_accum = 0;
    synth_voices[0].phase_increment = 0;
    synth_voices[0].envelope_stage = ENV_SUSTAIN;
    /* Q8 sub-units internally -- see synth.h's struct comment. */
    synth_voices[0].envelope_level = SYNTH_ENV_FULL << 8;
    synth_voices[0].sustain_level = SYNTH_ENV_FULL << 8;
    synth_render_half(buf, 1);
    CHECK(buf[0] == 0, "single full-envelope voice passes its oscillator sample through unscaled");
}

/* Fix 2 regression coverage: synth_init() leaves phase_increment at 0
 * for every voice, so a voice that's never had its ona set (i.e.
 * `0 VOICE GATE-ON` typed at the console before any `ONA` call) must
 * NOT be moved into ATTACK -- the phase accumulator would never
 * advance or wrap, so every waveform would output a constant,
 * non-silent value for as long as the envelope stayed nonzero: a
 * full-scale DC click/thump instead of silence. */
static void test_gate_on_no_ona_is_silent_no_op(void) {
    synth_init();
    synth_set_adsr(0, 10, 20, 50, 40);
    CHECK(synth_voices[0].phase_increment == 0, "freshly-init voice has no ona set (phase_increment == 0)");
    CHECK(synth_voices[0].envelope_stage == ENV_OFF, "voice starts with envelope OFF");
    synth_gate_on(0);
    CHECK(synth_voices[0].envelope_stage == ENV_OFF, "gate-on with no ona set stays OFF instead of entering ATTACK");
}

int main(void) {
    test_ona_table();
    test_waveform_saw();
    test_waveform_triangle();
    test_waveform_pulse();
    test_waveform_noise();
    test_set_ona_bounds();
    test_envelope_attack_decay_sustain();
    test_envelope_release_reaches_zero();
    test_envelope_instant_on_zero_duration();
    test_envelope_never_stuck_at_extreme_duration();
    test_envelope_500ms_attack_is_accurate();
    test_envelope_5000ms_attack_is_not_clamped();
    test_envelope_overflow_duration_does_not_invert();
    test_gate_on_off_transitions();
    test_gate_on_no_ona_is_silent_no_op();
    test_mixer_clamps_max_voices();
    test_mixer_silence_when_no_voices_gated();
    test_mixer_single_voice_full_envelope_matches_oscillator();

    if (failures == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("%d FAILURE(S)\n", failures);
    return 1;
}
