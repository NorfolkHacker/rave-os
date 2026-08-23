#ifndef RAVEOS_SYNTH_H
#define RAVEOS_SYNTH_H

#define SYNTH_SAMPLE_RATE 22050u
#define SYNTH_NUM_VOICES 8
#define SYNTH_ENV_FULL 32768

enum synth_waveform { WAVE_PULSE = 0, WAVE_SAW = 1, WAVE_TRIANGLE = 2, WAVE_NOISE = 3 };
enum synth_env_stage { ENV_OFF = 0, ENV_ATTACK = 1, ENV_DECAY = 2, ENV_SUSTAIN = 3, ENV_RELEASE = 4 };

struct synth_voice {
    enum synth_waveform waveform;
    unsigned int phase_accum;
    unsigned int phase_increment;
    unsigned int duty_threshold;
    unsigned int noise_lfsr;
    enum synth_env_stage envelope_stage;
    /* envelope_level, attack_rate, decay_rate, sustain_level, and
     * release_rate are all in Q8 sub-units internally (i.e. scaled up
     * by 256 from the public 0..SYNTH_ENV_FULL envelope scale) so that
     * synth_calc_rate()'s integer division truncates on a much
     * finer-grained quantity -- a plain 0..32768 rate collapses to
     * single digits for any envelope stage longer than ~200ms, badly
     * distorting requested millisecond durations. Only
     * synth_envelope_advance_sample()'s return value (and anything
     * outside synth.c) sees the un-scaled 0..SYNTH_ENV_FULL value;
     * these struct fields never leave Q8 scale. */
    int envelope_level;
    int attack_rate;
    int decay_rate;
    int sustain_level;
    int release_rate;
    /* -1 = ring mod off; 0-7 = ring-modulate this voice's triangle
     * output against that voice's own phase. Meaningless for any
     * other waveform -- matches real SID, whose ring mod is wired
     * directly into the triangle generator's fold-direction bit,
     * not a generic effect. */
    int ring_partner;
    /* 0 = straight to output (bypasses the shared filter); 1 = routed
     * through it. Matches real SID's own per-voice filter routing bits,
     * scaled to 8 voices. */
    int filter_route;
};

extern struct synth_voice synth_voices[SYNTH_NUM_VOICES];
extern const unsigned int ona_phase_increment[88];

void synth_init(void);
void synth_set_voice_waveform(int voice, enum synth_waveform wave);
void synth_set_duty(int voice, int duty_percent);
void synth_set_ona(int voice, int ona);
void synth_set_adsr(int voice, int attack_ms, int decay_ms, int sustain_percent, int release_ms);
void synth_gate_on(int voice);
void synth_gate_off(int voice);
void synth_set_ring_partner(int voice, int partner);
void synth_clear_ring_partner(int voice);
void synth_set_voice_filter_route(int voice, int routed);

/* ring_active/ring_partner_phase_accum only affect WAVE_TRIANGLE's fold
 * direction (real SID's ring mod is wired into the triangle generator
 * specifically) -- pass ring_active=0 for every other waveform, or
 * whenever the calling voice's ring_partner is -1. */
int synth_osc_sample(enum synth_waveform wave, unsigned int phase_accum,
                      unsigned int duty_threshold, unsigned int *noise_lfsr,
                      int phase_wrapped, int ring_active,
                      unsigned int ring_partner_phase_accum);
int synth_envelope_advance_sample(struct synth_voice *v);

#define SYNTH_FILTER_MODE_LP 1
#define SYNTH_FILTER_MODE_BP 2
#define SYNTH_FILTER_MODE_HP 4
#define SYNTH_FILTER_STATE_MAX 65536

struct synth_filter_state {
    int lp;
    int bp;
};

extern const int synth_filter_f_coeff[256];
extern const int synth_filter_q_coeff[16];

/* Current global filter settings -- set by synth_set_filter_cutoff()/
 * _resonance()/_mode() below, read by synth_render_half() (via the
 * f_coeff/q_coeff table lookups) once the filter is wired into the
 * mixer. Exposed here (not static in synth.c) the same way
 * synth_voices[] already is, so host tests can verify each setter's
 * clamping directly. */
extern int synth_filter_cutoff_index;
extern int synth_filter_res_index;
extern int synth_filter_mode_mask;

void synth_set_filter_cutoff(int cutoff);
void synth_set_filter_resonance(int resonance);
void synth_set_filter_mode(int mode_mask);

/* One Chamberlin state-variable filter step: updates *st in place and
 * returns the sum of whichever of low/band/high-pass outputs mode_mask
 * selects (bits: SYNTH_FILTER_MODE_LP/BP/HP, any combination). f_coeff/
 * q_coeff are Q14 fixed-point (looked up from synth_filter_f_coeff[]/
 * synth_filter_q_coeff[] by the caller). Clamps its own internal state
 * to +-SYNTH_FILTER_STATE_MAX -- an aggressive cutoff+resonance
 * combination could otherwise grow this recursive state without bound
 * and eventually corrupt the whole mixed output, not just distort. */
int synth_filter_process_sample(struct synth_filter_state *st, int input,
                                 int f_coeff, int q_coeff, int mode_mask);

/* Renders len bytes of 8-bit unsigned PCM (128 = silence) into buf,
 * advancing every voice's oscillator phase and envelope by one sample
 * per byte and mixing all SYNTH_NUM_VOICES together. Called from
 * kmain()'s frame loop whenever sb16_stream_needs_refill() says a
 * buffer half needs new data. */
void synth_render_half(unsigned char *buf, unsigned int len);

#endif
