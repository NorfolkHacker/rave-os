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

int main(void) {
    test_ona_table();
    test_waveform_saw();
    test_waveform_triangle();
    test_waveform_pulse();
    test_waveform_noise();
    test_set_ona_bounds();

    if (failures == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("%d FAILURE(S)\n", failures);
    return 1;
}
