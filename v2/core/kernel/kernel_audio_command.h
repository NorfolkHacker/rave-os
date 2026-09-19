#ifndef ACID_KERNEL_AUDIO_COMMAND_H
#define ACID_KERNEL_AUDIO_COMMAND_H

enum kernel_audio_command_type
{
    AUDIO_CMD_NOTE_ON = 0,
    AUDIO_CMD_NOTE_OFF = 1,
    AUDIO_CMD_RELEASE_OWNER = 2,
    /* Shapes a voice's envelope and filter routing -- lets a caller move
     * a voice off the harsh "instant attack/instant release, unfiltered
     * pulse" defaults synth_init() sets every voice up with. Goes through
     * this same queue rather than calling synth_set_adsr()/
     * synth_set_voice_filter_route() directly from the caller's own task,
     * because synth_voices[] may only be mutated from the audio thread
     * (see kernel_audio.c's apply(), the sole place that's allowed). */
    AUDIO_CMD_CONFIGURE_VOICE = 3,
    /* Sets the one shared filter's cutoff/resonance/mode -- there is only
     * ever one filter instance (see synth.h's own comment on
     * synth_filter_state), shared by every voice with filter_route
     * turned on, so this is a global setting, not per-voice. Same
     * audio-thread-only-mutation reasoning as CONFIGURE_VOICE above
     * applies (synth_filter_cutoff_index etc. are read every sample by
     * synth_render_half()). */
    AUDIO_CMD_CONFIGURE_FILTER = 4,
    /* Starts a voice stepping through up to 4 notes (the synth's own
     * arpeggiator -- see synth.h's struct synth_voice comment) instead of
     * holding one flat pitch. Always paired with a NOTE_ON on the same
     * voice (which sets the envelope/volume/gate) -- this only drives
     * phase_increment afterwards. Resets the step position to the start
     * of the sequence every time, so repeated triggers (e.g. one per
     * enemy hit) always begin the same way instead of picking up
     * wherever a previous run left off. NOTE_OFF turns the arp back off
     * (see apply()), so it never keeps stepping a silent, gated-off
     * voice. */
    AUDIO_CMD_TRIGGER_ARP = 5,
    /* Sets a voice's oscillator shape -- waveform (0=pulse, 1=saw,
     * 2=triangle, 3=noise, matching enum synth_waveform) and duty cycle
     * (0..100, only audible on a pulse wave). The engine has supported
     * both since the audio phase shipped (synth_set_voice_waveform/
     * synth_set_duty), but neither was ever reachable from Ruby -- every
     * voice was permanently a plain pulse wave with a fixed 50% duty.
     * Same audio-thread-only-mutation reasoning as CONFIGURE_VOICE. */
    AUDIO_CMD_CONFIGURE_OSC = 6,
    /* Pairs this voice with another for ring modulation (only audible on
     * a WAVE_TRIANGLE voice -- see synth.h's own comment on why), or
     * clears it if ring_partner < 0. Also engine-supported since the
     * audio phase but never reachable from Ruby until now. */
    AUDIO_CMD_SET_RING_PARTNER = 7
};

struct kernel_audio_command
{
    int type;
    int voice;          /* NOTE_ON / NOTE_OFF / CONFIGURE_VOICE */
    int ona;             /* NOTE_ON only */
    int volume;          /* NOTE_ON only -- 0..100, mapped to synth's own
                          * 0..SYNTH_ENV_FULL scale by kernel_audio.c */
    void * owner_task;   /* NOTE_ON (recorded as the new owner),
                          * RELEASE_OWNER (which owner to release) */
    int filter_route;    /* CONFIGURE_VOICE only -- 0 bypasses the shared
                          * filter, 1 routes through it */
    int attack_ms;       /* CONFIGURE_VOICE only */
    int decay_ms;        /* CONFIGURE_VOICE only */
    int sustain_percent; /* CONFIGURE_VOICE only -- 0..100 */
    int release_ms;      /* CONFIGURE_VOICE only */
    int cutoff;           /* CONFIGURE_FILTER only -- 0..255 */
    int resonance;         /* CONFIGURE_FILTER only -- 0..15 */
    int filter_mode;       /* CONFIGURE_FILTER only -- SYNTH_FILTER_MODE_* bits */
    int arp_notes[4];      /* TRIGGER_ARP only -- 1..88 each, only the
                            * first arp_count slots are used */
    int arp_count;          /* TRIGGER_ARP only -- 2..4 */
    int arp_rate_ms;        /* TRIGGER_ARP only -- ms per step */
    int waveform;            /* CONFIGURE_OSC only -- 0..3, enum synth_waveform */
    int duty_percent;        /* CONFIGURE_OSC only -- 0..100 */
    int ring_partner;        /* SET_RING_PARTNER only -- another voice index, or < 0 to clear */
};

#endif
