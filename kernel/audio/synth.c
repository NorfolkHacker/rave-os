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
        synth_voices[v].envelope_stage = ENV_OFF;
        synth_voices[v].envelope_level = 0;
        synth_voices[v].attack_rate = SYNTH_ENV_FULL;
        synth_voices[v].decay_rate = SYNTH_ENV_FULL;
        synth_voices[v].sustain_level = SYNTH_ENV_FULL;
        synth_voices[v].release_rate = SYNTH_ENV_FULL;
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

/* Rate is "how much envelope_level (0..SYNTH_ENV_FULL) changes per
 * sample" to cover the requested duration -- clamped to at least 1 so
 * a very long requested duration still makes forward progress every
 * sample (worst case ~1.5s to complete a stage at this sample rate)
 * instead of a rate that rounds down to 0 and never finishes. Decay
 * time is simplified to "time from full scale to zero, stopped early
 * at the sustain level" rather than "time from full scale to sustain
 * specifically" -- a common, deliberate simplification (exact SID
 * decay-curve emulation is a much bigger DSP topic, out of scope). */
static int synth_calc_rate(int duration_ms) {
    unsigned int samples;
    int rate;
    if (duration_ms <= 0) {
        return SYNTH_ENV_FULL;
    }
    samples = ((unsigned int)duration_ms * SYNTH_SAMPLE_RATE) / 1000u;
    if (samples == 0) {
        return SYNTH_ENV_FULL;
    }
    rate = SYNTH_ENV_FULL / (int)samples;
    if (rate < 1) {
        rate = 1;
    }
    return rate;
}

void synth_set_adsr(int voice, int attack_ms, int decay_ms, int sustain_percent, int release_ms) {
    struct synth_voice *v;
    if (clamp_voice(voice) < 0) {
        return;
    }
    if (sustain_percent < 0) {
        sustain_percent = 0;
    }
    if (sustain_percent > 100) {
        sustain_percent = 100;
    }
    v = &synth_voices[voice];
    v->attack_rate = synth_calc_rate(attack_ms);
    v->decay_rate = synth_calc_rate(decay_ms);
    v->sustain_level = (sustain_percent * SYNTH_ENV_FULL) / 100;
    v->release_rate = synth_calc_rate(release_ms);
}

void synth_gate_on(int voice) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    synth_voices[voice].envelope_level = 0;
    synth_voices[voice].envelope_stage = ENV_ATTACK;
}

void synth_gate_off(int voice) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    if (synth_voices[voice].envelope_stage != ENV_OFF) {
        synth_voices[voice].envelope_stage = ENV_RELEASE;
    }
}

int synth_envelope_advance_sample(struct synth_voice *v) {
    switch (v->envelope_stage) {
    case ENV_OFF:
        v->envelope_level = 0;
        break;
    case ENV_ATTACK:
        v->envelope_level += v->attack_rate;
        if (v->envelope_level >= SYNTH_ENV_FULL) {
            v->envelope_level = SYNTH_ENV_FULL;
            v->envelope_stage = ENV_DECAY;
        }
        break;
    case ENV_DECAY:
        if (v->envelope_level > v->sustain_level) {
            v->envelope_level -= v->decay_rate;
            if (v->envelope_level <= v->sustain_level) {
                v->envelope_level = v->sustain_level;
                v->envelope_stage = ENV_SUSTAIN;
            }
        } else {
            v->envelope_stage = ENV_SUSTAIN;
        }
        break;
    case ENV_SUSTAIN:
        v->envelope_level = v->sustain_level;
        break;
    case ENV_RELEASE:
        if (v->envelope_level > v->release_rate) {
            v->envelope_level -= v->release_rate;
        } else {
            v->envelope_level = 0;
            v->envelope_stage = ENV_OFF;
        }
        break;
    }
    return v->envelope_level;
}

void synth_render_half(unsigned char *buf, unsigned int len) {
    unsigned int i;
    int v;

    for (i = 0; i < len; i++) {
        int sum = 0;

        for (v = 0; v < SYNTH_NUM_VOICES; v++) {
            struct synth_voice *voice = &synth_voices[v];
            unsigned int old_accum = voice->phase_accum;
            unsigned int new_accum = old_accum + voice->phase_increment;
            int wrapped = (new_accum < old_accum) ? 1 : 0;
            int osc = synth_osc_sample(voice->waveform, old_accum,
                                        voice->duty_threshold,
                                        &voice->noise_lfsr, wrapped);
            int level = synth_envelope_advance_sample(voice);

            /* level is Q0.15 (0..32768): at full envelope this is an
             * exact pass-through of osc (127*32768>>15 == 127). */
            sum += (osc * level) >> 15;
            voice->phase_accum = new_accum;
        }

        /* Must clamp before biasing to unsigned 8-bit: 8 simultaneously
         * maxed-out voices sum to roughly +-1016, about 4x what a
         * signed byte holds -- an unclamped sum would wrap into loud
         * garbage instead of just clipping. */
        if (sum > 127) {
            sum = 127;
        }
        if (sum < -128) {
            sum = -128;
        }
        buf[i] = (unsigned char)(sum + 128);
    }
}
