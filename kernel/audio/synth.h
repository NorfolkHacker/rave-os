#ifndef RAVEOS_SYNTH_H
#define RAVEOS_SYNTH_H

#define SYNTH_SAMPLE_RATE 22050u
#define SYNTH_NUM_VOICES 8

enum synth_waveform { WAVE_PULSE = 0, WAVE_SAW = 1, WAVE_TRIANGLE = 2, WAVE_NOISE = 3 };

struct synth_voice {
    enum synth_waveform waveform;
    unsigned int phase_accum;
    unsigned int phase_increment;
    unsigned int duty_threshold;
    unsigned int noise_lfsr;
};

extern struct synth_voice synth_voices[SYNTH_NUM_VOICES];
extern const unsigned int ona_phase_increment[88];

void synth_init(void);
void synth_set_voice_waveform(int voice, enum synth_waveform wave);
void synth_set_duty(int voice, int duty_percent);
void synth_set_ona(int voice, int ona);

int synth_osc_sample(enum synth_waveform wave, unsigned int phase_accum,
                      unsigned int duty_threshold, unsigned int *noise_lfsr,
                      int phase_wrapped);

#endif
