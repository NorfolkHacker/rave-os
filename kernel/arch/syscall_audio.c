#include "syscall.h"
#include "synth.h"

int syscall_dispatch_audio(int num, int arg) {
    if (num == SYS_SYNTH_SET_WAVEFORM) {
        const struct sys_synth_set_waveform_args *a = (const struct sys_synth_set_waveform_args *)arg;
        synth_set_voice_waveform(a->voice, (enum synth_waveform)a->waveform);
        return 0;
    }
    if (num == SYS_SYNTH_SET_ONA) {
        const struct sys_synth_set_ona_args *a = (const struct sys_synth_set_ona_args *)arg;
        synth_set_ona(a->voice, a->ona);
        return 0;
    }
    if (num == SYS_SYNTH_SET_ADSR) {
        const struct sys_synth_set_adsr_args *a = (const struct sys_synth_set_adsr_args *)arg;
        synth_set_adsr(a->voice, a->attack_ms, a->decay_ms, a->sustain_percent, a->release_ms);
        return 0;
    }
    if (num == SYS_SYNTH_GATE_ON) {
        synth_gate_on(arg);
        return 0;
    }
    if (num == SYS_SYNTH_GATE_OFF) {
        synth_gate_off(arg);
        return 0;
    }
    return syscall_dispatch_core(num, arg);
}
