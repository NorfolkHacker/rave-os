#ifndef ACID_KERNEL_AUDIO_H
#define ACID_KERNEL_AUDIO_H

#include <stdbool.h>

/* Creates the one shared audio-command queue and resets voice ownership.
 * Call exactly once, at boot, before any app spawns -- same ordering
 * discipline gfx_init()/kernel_window_init() already established. */
void kernel_audio_init( void );

/* Enqueues a command from any app task. Non-blocking -- a dropped command
 * under extreme load is an acceptable, rare degradation for this phase
 * (unlike windowing's drag-release position, there is no single most-
 * authoritative audio command whose loss would cause a permanent,
 * uncorrectable desync). Safe to call from any task. */
void kernel_audio_enqueue_note_on( void * owner_task, int voice, int ona, int volume );
void kernel_audio_enqueue_note_off( void * owner_task, int voice );

/* Enqueues an AUDIO_CMD_RELEASE_OWNER command -- called synchronously from
 * vm_host_task's own unconditional per-app cleanup (Task 5), covering
 * every app-exit path, not just the close button. Does NOT touch
 * synth_voices[] or voice-ownership state directly (only the audio
 * callback thread does that, per this plan's Global Constraints) -- it
 * only enqueues, same as the note_on/note_off functions above. */
void kernel_audio_release_owner( void * owner_task );

/* Drains every pending command from the queue (applying each to
 * synth_voices[] and the voice-ownership table), then calls
 * synth_render_half(buf, len). MUST be called only from the audio
 * callback's own thread -- this is the sole place synth_voices[] and
 * voice ownership are ever mutated, by design (see this plan's Global
 * Constraints on avoiding windowing's own shared-LGFX-object race). */
void kernel_audio_drain_and_render( unsigned char * buf, unsigned int len );

#endif
