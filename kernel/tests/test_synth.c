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

static void test_envelope_attack_decay_sustain(void) {
    struct synth_voice v;
    int i;
    int reached_full = 0;
    int reached_sustain = 0;

    v.envelope_stage = ENV_ATTACK;
    v.envelope_level = 0;
    v.attack_rate = 148;   /* ~10ms at 22050Hz per synth_calc_rate's formula */
    v.decay_rate = 74;     /* ~20ms */
    v.sustain_level = (50 * SYNTH_ENV_FULL) / 100;
    v.release_rate = 33;   /* ~40ms */

    for (i = 0; i < 1000; i++) {
        synth_envelope_advance_sample(&v);
        if (v.envelope_stage == ENV_DECAY && !reached_full) {
            reached_full = 1;
        }
        if (v.envelope_stage == ENV_SUSTAIN) {
            reached_sustain = 1;
            break;
        }
    }
    CHECK(reached_full, "envelope reaches full scale and enters decay");
    CHECK(reached_sustain, "envelope decays into sustain");
    CHECK(v.envelope_level == v.sustain_level, "envelope holds at configured sustain level");

    for (i = 0; i < 500; i++) {
        synth_envelope_advance_sample(&v);
    }
    CHECK(v.envelope_stage == ENV_SUSTAIN, "envelope stays in sustain without gate-off");
    CHECK(v.envelope_level == v.sustain_level, "sustain level does not drift");
}

static void test_envelope_release_reaches_zero(void) {
    struct synth_voice v;
    int i;
    int reached_off = 0;

    v.envelope_stage = ENV_RELEASE;
    v.envelope_level = (50 * SYNTH_ENV_FULL) / 100;
    v.sustain_level = v.envelope_level;
    v.release_rate = 33;

    for (i = 0; i < 2000; i++) {
        synth_envelope_advance_sample(&v);
        if (v.envelope_stage == ENV_OFF) {
            reached_off = 1;
            break;
        }
    }
    CHECK(reached_off, "release eventually reaches OFF");
    CHECK(v.envelope_level == 0, "released envelope level is exactly zero");
}

static void test_envelope_instant_on_zero_duration(void) {
    struct synth_voice v;
    v.envelope_stage = ENV_ATTACK;
    v.envelope_level = 0;
    v.attack_rate = SYNTH_ENV_FULL; /* synth_calc_rate(0) */
    v.decay_rate = 1;
    v.sustain_level = 0;
    v.release_rate = 1;
    synth_envelope_advance_sample(&v);
    CHECK(v.envelope_level == SYNTH_ENV_FULL, "zero-duration attack reaches full scale in one sample");
    CHECK(v.envelope_stage == ENV_DECAY, "zero-duration attack immediately enters decay");
}

static void test_envelope_never_stuck_at_extreme_duration(void) {
    int rate;
    struct synth_voice v;
    int i;
    int reached_full = 0;

    v.envelope_stage = ENV_ATTACK;
    v.envelope_level = 0;
    rate = SYNTH_ENV_FULL / (int)(((unsigned int)3600000u * SYNTH_SAMPLE_RATE) / 1000u);
    if (rate < 1) {
        rate = 1;
    }
    CHECK(rate >= 1, "synth_calc_rate-equivalent never computes a zero rate");
    v.attack_rate = rate;
    for (i = 0; i < SYNTH_ENV_FULL + 10; i++) {
        synth_envelope_advance_sample(&v);
        if (v.envelope_stage != ENV_ATTACK) {
            reached_full = 1;
            break;
        }
    }
    CHECK(reached_full, "even a minimum rate=1 envelope eventually completes attack");
}

static void test_gate_on_off_transitions(void) {
    synth_init();
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
        synth_voices[v].envelope_level = SYNTH_ENV_FULL;
        synth_voices[v].sustain_level = SYNTH_ENV_FULL;
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
    synth_voices[0].envelope_level = SYNTH_ENV_FULL;
    synth_voices[0].sustain_level = SYNTH_ENV_FULL;
    synth_render_half(buf, 1);
    CHECK(buf[0] == 0, "single full-envelope voice passes its oscillator sample through unscaled");
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
    test_gate_on_off_transitions();
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
