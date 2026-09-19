#ifndef ACID_KERNEL_AUDIO_H
#define ACID_KERNEL_AUDIO_H

#include <stdbool.h>

/* Creates the one shared audio-command ring buffer and resets voice
 * ownership. Call exactly once, at boot, before any app spawns -- same
 * ordering discipline gfx_init()/kernel_window_init() already
 * established. */
void kernel_audio_init( void );

/* Enqueues a command from any app task. Best-effort -- a dropped note_on
 * or note_off under extreme load is an acceptable, rare degradation for
 * this phase. Safe to call from any FreeRTOS task; must never be called
 * from the audio callback thread. */
void kernel_audio_enqueue_note_on( void * owner_task, int voice, int ona, int volume );
void kernel_audio_enqueue_note_off( void * owner_task, int voice );

/* Enqueues an AUDIO_CMD_CONFIGURE_VOICE command -- shapes a voice's ADSR
 * envelope and filter routing ahead of whatever note_on calls follow.
 * Same best-effort/any-task-safe contract as note_on/note_off above. */
void kernel_audio_enqueue_configure_voice( int voice, int filter_route,
                                            int attack_ms, int decay_ms,
                                            int sustain_percent, int release_ms );

/* Enqueues an AUDIO_CMD_CONFIGURE_FILTER command -- sets the one shared
 * filter's cutoff/resonance/mode. Same best-effort/any-task-safe contract
 * as the enqueue functions above. */
void kernel_audio_enqueue_configure_filter( int cutoff, int resonance, int filter_mode );

/* Enqueues an AUDIO_CMD_TRIGGER_ARP command -- call right after
 * kernel_audio_enqueue_note_on() on the same voice (that sets the
 * envelope/volume/gate; this drives pitch-stepping on top of it). Same
 * best-effort/any-task-safe contract as the enqueue functions above. */
void kernel_audio_enqueue_trigger_arp( int voice, const int notes[4],
                                        int count, int rate_ms );

/* Enqueues an AUDIO_CMD_RELEASE_OWNER command -- called synchronously from
 * vm_host_task's own unconditional per-app cleanup (Task 5), covering
 * every app-exit path, not just the close button. Unlike note_on/note_off,
 * this command is delivery-guaranteed (bounded blocking retry): losing it
 * would permanently orphan a sounding voice with nobody able to stop it.
 * Does NOT touch synth_voices[] or voice-ownership state directly (only
 * the audio callback thread does that, per this plan's Global
 * Constraints) -- it only enqueues, same as the note_on/note_off
 * functions above. Safe to call from any FreeRTOS task; must never be
 * called from the audio callback thread. */
void kernel_audio_release_owner( void * owner_task );

/* Drains every pending command from the ring buffer (applying each to
 * synth_voices[] and the voice-ownership table), then calls
 * synth_render_half(buf, len). MUST be called only from the audio
 * callback's own thread, and that thread must never call any FreeRTOS
 * API -- on sim this thread is a raw pthread created by SDL2, not a
 * FreeRTOS task, so FreeRTOS's own synchronization primitives (queues,
 * critical sections) are not valid to use from it. This is the sole place
 * synth_voices[] and voice ownership are ever mutated, by design (see
 * this plan's Global Constraints on avoiding windowing's own shared-LGFX-
 * object race). */
void kernel_audio_drain_and_render( unsigned char * buf, unsigned int len );

/* How many of SYNTH_NUM_VOICES have a currently-sounding envelope (not
 * ENV_OFF), as of the most recent audio callback. Safe from any thread.
 * For a system-monitor app to show real synth activity -- see
 * kernel_audio.c's own comment on the mask this reads. */
int kernel_audio_active_voice_count( void );

/* Global output gain, 0..100, applied to every voice's mixed output in
 * kernel_audio_drain_and_render (post-synth_render_half, pre-buf handoff
 * to the HAL) -- deliberately NOT inside synth.c, since this is a
 * system-wide setting (a Config app's "Volume" knob), not a per-voice
 * synth parameter. Safe to call from any FreeRTOS task; backed by a
 * single _Atomic int, same single-writer-doesn't-matter-here discipline
 * as g_active_voice_mask (a caller can only ever set it to one value, so
 * there's no meaningful "last writer wins" race to worry about beyond
 * ordinary atomic tearing, which _Atomic already prevents). Clamped to
 * 0..100 inside the setter, not at every read site. */
void kernel_audio_set_master_volume( int percent );
int kernel_audio_get_master_volume( void );

#endif
