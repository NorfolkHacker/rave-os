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

int synth_osc_sample(enum synth_waveform wave, unsigned int phase_accum,
                      unsigned int duty_threshold, unsigned int *noise_lfsr,
                      int phase_wrapped);
int synth_envelope_advance_sample(struct synth_voice *v);

/* Renders len bytes of 8-bit unsigned PCM (128 = silence) into buf,
 * advancing every voice's oscillator phase and envelope by one sample
 * per byte and mixing all SYNTH_NUM_VOICES together. Called from
 * kmain()'s frame loop whenever sb16_stream_needs_refill() says a
 * buffer half needs new data. */
void synth_render_half(unsigned char *buf, unsigned int len);

#endif
