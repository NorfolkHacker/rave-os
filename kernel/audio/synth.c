#include "synth.h"

struct synth_voice synth_voices[SYNTH_NUM_VOICES];

/* Standard 88-key piano numbering, ona 49 = A4 = 440.0Hz
 * (freq(n) = 440 * 2^((n-49)/12)). Each entry is a Q-format DDS phase
 * increment for a 32-bit phase accumulator at SYNTH_SAMPLE_RATE:
 * increment = round(freq * 2^32 / SYNTH_SAMPLE_RATE). No floating
 * point exists in this kernel, so this table is generated once on the
 * host (the exact Python one-liner: `round(440.0 * 2**((n-49)/12) *
 * 2**32 / 22050)` for n in 1..88) and committed as a literal, the same
 * "precomputed table sidesteps needing runtime float" precedent
 * sub-project (A)'s beep_tone[] already established. */
const unsigned int ona_phase_increment[88] = {
    5356535u, 5675051u, 6012507u, 6370030u, 6748811u, 7150117u, 7575285u, 8025735u,
    8502970u, 9008582u, 9544261u, 10111792u, 10713070u, 11350103u, 12025015u, 12740059u,
    13497623u, 14300233u, 15150569u, 16051469u, 17005939u, 18017165u, 19088521u, 20223584u,
    21426141u, 22700205u, 24050030u, 25480119u, 26995246u, 28600467u, 30301139u, 32102938u,
    34011878u, 36034330u, 38177043u, 40447168u, 42852281u, 45400411u, 48100060u, 50960238u,
    53990491u, 57200933u, 60602278u, 64205876u, 68023757u, 72068660u, 76354085u, 80894335u,
    85704563u, 90800821u, 96200119u, 101920476u, 107980983u, 114401866u, 121204555u, 128411753u,
    136047513u, 144137319u, 152708170u, 161788671u, 171409126u, 181601643u, 192400238u, 203840952u,
    215961966u, 228803732u, 242409110u, 256823506u, 272095026u, 288274639u, 305416341u, 323577341u,
    342818251u, 363203285u, 384800477u, 407681904u, 431923931u, 457607465u, 484818220u, 513647012u,
    544190053u, 576549277u, 610832681u, 647154683u, 685636503u, 726406571u, 769600953u, 815363807u,
};

void synth_init(void) {
    int v;
    for (v = 0; v < SYNTH_NUM_VOICES; v++) {
        synth_voices[v].waveform = WAVE_PULSE;
        synth_voices[v].phase_accum = 0;
        synth_voices[v].phase_increment = 0;
        synth_voices[v].duty_threshold = 128;
        /* LFSR seed must never be 0 (a zero state never changes) --
         * a distinct fixed seed per voice also keeps multiple noise
         * voices from sounding perfectly correlated. */
        synth_voices[v].noise_lfsr = 0xACE1u + (unsigned int)v;
    }
}

static int clamp_voice(int voice) {
    if (voice < 0 || voice >= SYNTH_NUM_VOICES) {
        return -1;
    }
    return voice;
}

void synth_set_voice_waveform(int voice, enum synth_waveform wave) {
    if (clamp_voice(voice) < 0 || wave < WAVE_PULSE || wave > WAVE_NOISE) {
        return;
    }
    synth_voices[voice].waveform = wave;
}

void synth_set_duty(int voice, int duty_percent) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    if (duty_percent < 1) {
        duty_percent = 1;
    }
    if (duty_percent > 99) {
        duty_percent = 99;
    }
    synth_voices[voice].duty_threshold = ((unsigned int)duty_percent * 256u) / 100u;
}

void synth_set_ona(int voice, int ona) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    if (ona < 1 || ona > 88) {
        return;
    }
    synth_voices[voice].phase_increment = ona_phase_increment[ona - 1];
}

/* 16-bit Galois LFSR, maximal-length tap mask 0xB400. Ties the noise
 * generator's pitch to the voice's own ona (like the real SID chip):
 * it only advances when the caller says this sample's phase_accum
 * wrapped, not every sample. */
static void noise_advance(unsigned int *lfsr) {
    unsigned int v = *lfsr & 0xFFFFu;
    unsigned int lsb = v & 1u;
    v >>= 1;
    if (lsb) {
        v ^= 0xB400u;
    }
    *lfsr = v;
}

/* Returns a signed sample in roughly [-128, 127] for one oscillator.
 * duty_threshold (0-255) only matters for WAVE_PULSE. noise_lfsr is
 * only read/advanced for WAVE_NOISE. phase_accum's top 8 bits
 * (pos8, 0-255) give this sample's position within the current
 * period for the three deterministic waveforms. */
int synth_osc_sample(enum synth_waveform wave, unsigned int phase_accum,
                      unsigned int duty_threshold, unsigned int *noise_lfsr,
                      int phase_wrapped) {
    unsigned int pos8 = (phase_accum >> 24) & 0xFFu;

    switch (wave) {
    case WAVE_SAW:
        return (int)pos8 - 128;
    case WAVE_TRIANGLE: {
        unsigned int tri_pos = (pos8 < 128u) ? pos8 : (255u - pos8);
        return (int)(tri_pos * 2u) - 128;
    }
    case WAVE_PULSE:
        return (pos8 < duty_threshold) ? 127 : -128;
    case WAVE_NOISE:
        if (phase_wrapped) {
            noise_advance(noise_lfsr);
        }
        return (int)(*noise_lfsr & 0xFFu) - 128;
    default:
        return 0;
    }
}
