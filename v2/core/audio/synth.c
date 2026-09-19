#include "synth.h"

struct synth_voice synth_voices[SYNTH_NUM_VOICES];

/* The one shared filter instance (matches "one shared filter", not one
 * per voice, per the SID architecture this is scaled up from). Its
 * lp/bp state persists across calls to synth_render_half() the same
 * way each voice's phase_accum/envelope_level already do -- reset only
 * by synth_init(), not per-call. */
static struct synth_filter_state synth_filter;

/* Tracks the (lp,bp) pairs visited during the current unbroken run of
 * zero-input samples fed to the shared filter -- see
 * synth_filter_process_sample()'s own doc comment for why. Reset by
 * synth_init() for the same test/re-init-isolation reason synth_filter
 * itself is. */
#define SYNTH_FILTER_SILENCE_HISTORY_LEN 512
static int synth_filter_silence_hist_lp[SYNTH_FILTER_SILENCE_HISTORY_LEN];
static int synth_filter_silence_hist_bp[SYNTH_FILTER_SILENCE_HISTORY_LEN];
static int synth_filter_silence_hist_count = 0;

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
        /* Q8 scale -- see struct synth_voice's comment in synth.h. */
        synth_voices[v].attack_rate = SYNTH_ENV_FULL << 8;
        synth_voices[v].decay_rate = SYNTH_ENV_FULL << 8;
        synth_voices[v].sustain_level = SYNTH_ENV_FULL << 8;
        synth_voices[v].release_rate = SYNTH_ENV_FULL << 8;
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

void synth_set_ring_partner(int voice, int partner) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    if (partner < 0 || partner > 7) {
        return;
    }
    synth_voices[voice].ring_partner = partner;
}

void synth_clear_ring_partner(int voice) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    synth_voices[voice].ring_partner = -1;
}

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

void synth_set_arp_rate(int voice, int ms) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    synth_voices[voice].arp_step_rate = synth_calc_arp_step_samples(ms);
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
 * period for the three deterministic waveforms. ring_active/
 * ring_partner_phase_accum only matter for WAVE_TRIANGLE -- see the
 * WAVE_TRIANGLE case below and synth.h's doc comment. */
int synth_osc_sample(enum synth_waveform wave, unsigned int phase_accum,
                      unsigned int duty_threshold, unsigned int *noise_lfsr,
                      int phase_wrapped, int ring_active,
                      unsigned int ring_partner_phase_accum) {
    unsigned int pos8 = (phase_accum >> 24) & 0xFFu;

    switch (wave) {
    case WAVE_SAW:
        return (int)pos8 - 128;
    case WAVE_TRIANGLE: {
        /* msb (pos8's own top bit) decides fold direction; lower7 is
         * the position within that half-period. Ring mod XORs a
         * partner voice's own top bit into that fold decision --
         * exactly how real SID's ring mod is wired into the triangle
         * generator. Behaviorally identical to the plain (pos8<128)
         * form when ring_active is 0: msb=0 -> tri_pos=lower7
         * (matches pos8<128's pos8==lower7); msb=1 -> tri_pos=
         * 127-lower7 (matches pos8>=128's 255-pos8 = 127-lower7). */
        unsigned int msb = (pos8 >> 7) & 1u;
        unsigned int lower7 = pos8 & 0x7Fu;
        unsigned int tri_pos;
        if (ring_active) {
            unsigned int partner_pos8 = (ring_partner_phase_accum >> 24) & 0xFFu;
            msb ^= (partner_pos8 >> 7) & 1u;
        }
        tri_pos = msb ? (127u - lower7) : lower7;
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

/* Rate is "how much envelope_level (Q8 sub-units of 0..SYNTH_ENV_FULL)
 * changes per sample" to cover the requested duration -- clamped to at
 * least 1 q8-unit/sample so a very long requested duration still makes
 * forward progress every sample (worst case ~380s to complete a stage
 * at this sample rate and Q8 resolution: (SYNTH_ENV_FULL << 8) / 22050)
 * instead of a rate that rounds down to 0 and never finishes. Working
 * in Q8 sub-units (rather than plain 0..SYNTH_ENV_FULL units) is what
 * keeps the integer division from collapsing to single digits -- and
 * therefore badly distorting requested millisecond durations -- for
 * any stage longer than roughly 200ms; see struct synth_voice's
 * comment in synth.h. Decay time is simplified to "time from full
 * scale to zero, stopped early at the sustain level" rather than
 * "time from full scale to sustain specifically" -- a common,
 * deliberate simplification (exact SID decay-curve emulation is a
 * much bigger DSP topic, out of scope).
 *
 * duration_ms is clamped to 100000 (100s) before the samples
 * calculation: `(unsigned)duration_ms * SYNTH_SAMPLE_RATE` overflows
 * 32 bits above ~194783ms, at which point the wrapped product silently
 * inverts a long requested duration into a near-instant one instead of
 * a long one. 100000ms is comfortably below that threshold and far
 * beyond any real musical use. */
static int synth_calc_rate(int duration_ms) {
    unsigned int samples;
    int rate_q8;
    if (duration_ms <= 0) {
        return SYNTH_ENV_FULL << 8;
    }
    if (duration_ms > 100000) {
        duration_ms = 100000;
    }
    samples = ((unsigned int)duration_ms * SYNTH_SAMPLE_RATE) / 1000u;
    if (samples == 0) {
        return SYNTH_ENV_FULL << 8;
    }
    rate_q8 = (SYNTH_ENV_FULL << 8) / (int)samples;
    if (rate_q8 < 1) {
        rate_q8 = 1;
    }
    return rate_q8;
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
    /* Q8 scale to match envelope_level -- see synth.h. */
    v->sustain_level = ((sustain_percent * SYNTH_ENV_FULL) / 100) << 8;
    v->release_rate = synth_calc_rate(release_ms);
}

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

void synth_gate_off(int voice) {
    if (clamp_voice(voice) < 0) {
        return;
    }
    if (synth_voices[voice].envelope_stage != ENV_OFF) {
        synth_voices[voice].envelope_stage = ENV_RELEASE;
    }
}

/* All arithmetic here operates on envelope_level/attack_rate/decay_rate/
 * sustain_level/release_rate in Q8 sub-units (see synth.h's struct
 * comment) -- the caller-visible 0..SYNTH_ENV_FULL scale is produced
 * only at the very end, by the final `>> 8` on the return value.
 * synth_render_half() and everything else outside this file is
 * unaffected: it still sees the same 0..SYNTH_ENV_FULL result it
 * always has. */
int synth_envelope_advance_sample(struct synth_voice *v) {
    switch (v->envelope_stage) {
    case ENV_OFF:
        v->envelope_level = 0;
        break;
    case ENV_ATTACK:
        v->envelope_level += v->attack_rate;
        if (v->envelope_level >= (SYNTH_ENV_FULL << 8)) {
            v->envelope_level = SYNTH_ENV_FULL << 8;
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
    return v->envelope_level >> 8;
}

void synth_render_half(unsigned char *buf, unsigned int len) {
    unsigned int i;
    int v;

    for (i = 0; i < len; i++) {
        int filtered_sum = 0;
        int bypass_sum = 0;
        int sum;
        int filtered_out;

        for (v = 0; v < SYNTH_NUM_VOICES; v++) {
            struct synth_voice *voice = &synth_voices[v];
            unsigned int old_accum = voice->phase_accum;
            unsigned int new_accum = old_accum + voice->phase_increment;
            int wrapped = (new_accum < old_accum) ? 1 : 0;
            int ring_active = (voice->ring_partner >= 0) ? 1 : 0;
            unsigned int ring_partner_accum = ring_active
                ? synth_voices[voice->ring_partner].phase_accum : 0u;
            int osc = synth_osc_sample(voice->waveform, old_accum,
                                        voice->duty_threshold,
                                        &voice->noise_lfsr, wrapped,
                                        ring_active, ring_partner_accum);
            int level = synth_envelope_advance_sample(voice);
            int contribution;

            /* level is Q0.15 (0..32768): at full envelope this is an
             * exact pass-through of osc (127*32768>>15 == 127). */
            contribution = (osc * level) >> 15;
            if (voice->filter_route) {
                filtered_sum += contribution;
            } else {
                bypass_sum += contribution;
            }
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
                                                     synth_filter_f_coeff[synth_filter_cutoff_index],
                                                     synth_filter_q_coeff[synth_filter_res_index],
                                                     synth_filter_mode_mask);
        sum = filtered_out + bypass_sum;

        /* Must clamp before biasing to unsigned 8-bit: unfiltered voices
         * alone can already sum to roughly +-1016 (8 maxed-out voices),
         * about 4x what a signed byte holds, and a resonant filter can
         * amplify filtered_sum well past that near its cutoff on top --
         * an unclamped sum would wrap into loud garbage instead of just
         * clipping. */
        if (sum > 127) {
            sum = 127;
        }
        if (sum < -128) {
            sum = -128;
        }
        buf[i] = (unsigned char)(sum + 128);
    }
}

/* Cutoff-to-frequency-coefficient table: Q14 fixed-point f = 2*sin(pi*fc/fs)
 * for fc log-spaced 20Hz-3000Hz (index 0-255), fs = SYNTH_SAMPLE_RATE.
 * The 3000Hz upper bound is deliberately conservative -- comfortably
 * under fs/6 (~3675Hz), the range where this simple (non-oversampled)
 * state-variable topology stays numerically well-behaved. No floating
 * point exists in this kernel, so this table (like ona_phase_increment[])
 * is generated once on the host and committed as a literal -- exact
 * Python: `round(2.0 * math.sin(math.pi * (20.0 * (3000.0/20.0) **
 * (i/255.0)) / 22050.0) * 16384)` for i in 0..255. */
const int synth_filter_f_coeff[256] = {
    93, 95, 97, 99, 101, 103, 105, 107,
    109, 111, 114, 116, 118, 121, 123, 125,
    128, 130, 133, 136, 138, 141, 144, 147,
    150, 153, 156, 159, 162, 165, 168, 172,
    175, 179, 182, 186, 189, 193, 197, 201,
    205, 209, 213, 217, 222, 226, 231, 235,
    240, 245, 249, 254, 259, 265, 270, 275,
    281, 286, 292, 298, 304, 310, 316, 322,
    328, 335, 342, 348, 355, 362, 369, 377,
    384, 392, 400, 408, 416, 424, 432, 441,
    450, 459, 468, 477, 486, 496, 506, 516,
    526, 537, 547, 558, 569, 581, 592, 604,
    616, 628, 640, 653, 666, 679, 693, 707,
    721, 735, 749, 764, 780, 795, 811, 827,
    843, 860, 877, 894, 912, 930, 949, 968,
    987, 1006, 1026, 1047, 1067, 1089, 1110, 1132,
    1155, 1178, 1201, 1225, 1249, 1274, 1299, 1325,
    1351, 1378, 1405, 1433, 1461, 1490, 1520, 1550,
    1581, 1612, 1644, 1677, 1710, 1744, 1779, 1814,
    1850, 1886, 1924, 1962, 2001, 2040, 2081, 2122,
    2164, 2207, 2251, 2295, 2341, 2387, 2435, 2483,
    2532, 2582, 2633, 2685, 2738, 2793, 2848, 2904,
    2962, 3020, 3080, 3141, 3203, 3267, 3331, 3397,
    3464, 3533, 3603, 3674, 3746, 3820, 3896, 3973,
    4051, 4131, 4213, 4296, 4380, 4467, 4555, 4645,
    4736, 4830, 4925, 5022, 5120, 5221, 5324, 5429,
    5535, 5644, 5755, 5868, 5983, 6100, 6220, 6342,
    6466, 6593, 6722, 6853, 6987, 7123, 7262, 7404,
    7548, 7695, 7845, 7998, 8153, 8311, 8472, 8637,
    8804, 8974, 9147, 9324, 9504, 9687, 9873, 10062,
    10255, 10452, 10652, 10855, 11062, 11272, 11486, 11704,
    11926, 12151, 12380, 12613, 12850, 13090, 13335, 13583,
};

/* Resonance-to-Q14-feedback-coefficient table, index 0-15 matching
 * SID's own 4-bit resonance register. Q sweeps 0.707 (heavily damped,
 * index 0) to 8.0 (sharp resonant peak, index 15), q = 1/Q. Exact
 * Python: `round((1.0 / (0.707 + i * (8.0 - 0.707) / 15.0)) * 16384)`
 * for i in 0..15. */
const int synth_filter_q_coeff[16] = {
    23174, 13731, 9756, 7566, 6178, 5221, 4521, 3986, 3564, 3223, 2942, 2706, 2505, 2331, 2181, 2048,
};

int synth_filter_cutoff_index = 128;
int synth_filter_res_index = 0;
int synth_filter_mode_mask = SYNTH_FILTER_MODE_LP;

void synth_set_filter_cutoff(int cutoff) {
    if (cutoff < 0) {
        cutoff = 0;
    }
    if (cutoff > 255) {
        cutoff = 255;
    }
    synth_filter_cutoff_index = cutoff;
}

void synth_set_filter_resonance(int resonance) {
    if (resonance < 0) {
        resonance = 0;
    }
    if (resonance > 15) {
        resonance = 15;
    }
    synth_filter_res_index = resonance;
}

void synth_set_filter_mode(int mode_mask) {
    if (mode_mask < 0) {
        mode_mask = 0;
    }
    if (mode_mask > 7) {
        mode_mask = 7;
    }
    synth_filter_mode_mask = mode_mask;
}

/* FIXPOINT_LIMIT_CYCLE_FIX: a truncating (floor) fixed-point IIR
 * recursion has spurious nonzero equilibria and small limit cycles the
 * real analog SVF doesn't -- with zero input, the only physically
 * correct resting state is (0,0), but this recursion's rounding error
 * can park it on an exact repeating orbit instead of ever reaching
 * true zero. Detected by remembering the (lp,bp) pairs visited during
 * the current unbroken run of zero-input samples (see the
 * synth_filter_silence_hist_* declarations near synth_filter's own,
 * above): if the state repeats one already seen, it has (by definition)
 * stopped decaying and is cycling forever, so force it the rest of the
 * way to true silence. Any nonzero input resets the history, so this
 * can never fire mid-note -- only once a release's whole filtered input
 * has gone fully silent. */
int synth_filter_process_sample(struct synth_filter_state *st, int input,
                                 int f_coeff, int q_coeff, int mode_mask) {
    int hp = input - st->lp - (int)(((long long)q_coeff * st->bp) >> 14);
    int bp_new = st->bp + (int)(((long long)f_coeff * hp) >> 14);
    int lp_new = st->lp + (int)(((long long)f_coeff * bp_new) >> 14);

    if (input != 0) {
        synth_filter_silence_hist_count = 0;
    } else {
        int k;
        int seen = synth_filter_silence_hist_count < SYNTH_FILTER_SILENCE_HISTORY_LEN
                       ? synth_filter_silence_hist_count
                       : SYNTH_FILTER_SILENCE_HISTORY_LEN;
        int matched = 0;
        for (k = 0; k < seen; k++) {
            if (synth_filter_silence_hist_lp[k] == lp_new &&
                synth_filter_silence_hist_bp[k] == bp_new) {
                matched = 1;
                break;
            }
        }
        if (matched) {
            lp_new = 0;
            bp_new = 0;
            synth_filter_silence_hist_count = 0;
        } else {
            int slot = synth_filter_silence_hist_count % SYNTH_FILTER_SILENCE_HISTORY_LEN;
            synth_filter_silence_hist_lp[slot] = lp_new;
            synth_filter_silence_hist_bp[slot] = bp_new;
            synth_filter_silence_hist_count++;
        }
    }

    if (lp_new > SYNTH_FILTER_STATE_MAX) {
        lp_new = SYNTH_FILTER_STATE_MAX;
    }
    if (lp_new < -SYNTH_FILTER_STATE_MAX) {
        lp_new = -SYNTH_FILTER_STATE_MAX;
    }
    if (bp_new > SYNTH_FILTER_STATE_MAX) {
        bp_new = SYNTH_FILTER_STATE_MAX;
    }
    if (bp_new < -SYNTH_FILTER_STATE_MAX) {
        bp_new = -SYNTH_FILTER_STATE_MAX;
    }

    st->lp = lp_new;
    st->bp = bp_new;

    {
        int out = 0;
        if (mode_mask & SYNTH_FILTER_MODE_LP) {
            out += lp_new;
        }
        if (mode_mask & SYNTH_FILTER_MODE_BP) {
            out += bp_new;
        }
        if (mode_mask & SYNTH_FILTER_MODE_HP) {
            out += hp;
        }
        return out;
    }
}
