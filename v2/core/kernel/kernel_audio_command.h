#ifndef ACID_KERNEL_AUDIO_COMMAND_H
#define ACID_KERNEL_AUDIO_COMMAND_H

enum kernel_audio_command_type
{
    AUDIO_CMD_NOTE_ON = 0,
    AUDIO_CMD_NOTE_OFF = 1,
    AUDIO_CMD_RELEASE_OWNER = 2
};

struct kernel_audio_command
{
    int type;
    int voice;          /* NOTE_ON / NOTE_OFF only */
    int ona;             /* NOTE_ON only */
    int volume;          /* NOTE_ON only -- 0..100, mapped to synth's own
                          * 0..SYNTH_ENV_FULL scale by kernel_audio.c */
    void * owner_task;   /* NOTE_ON (recorded as the new owner),
                          * RELEASE_OWNER (which owner to release) */
};

#endif
