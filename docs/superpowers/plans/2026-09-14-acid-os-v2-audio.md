# acid OS v2 Audio Subsystem Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Real, audible sound on the `sim` target — apps can trigger notes on RaveOS v1's own 8-voice synth via a Ruby binding, real PCM comes out through SDL2, and a crashed app's voices don't sound forever.

**Architecture:** RaveOS v1's `kernel/audio/synth.c` is ported verbatim into `v2/core/audio/` (zero OS dependencies, confirmed). A new `v2/core/kernel/kernel_audio.c` owns the one shared, machine-wide audio-command queue and per-voice ownership tracking, with a strict single-thread-owns-the-state discipline (only the SDL audio callback's own thread ever touches `synth_voices[]`/voice ownership — everything else only enqueues commands) to avoid the exact class of cross-task data race windowing's own final review had to find and fix for the shared display object.

**Tech Stack:** C (all new kernel/audio/binding/HAL code, verbatim-ported DSP), Ruby (two new `AcidApp`-callable bindings), FreeRTOS queues (the same IPC primitive windowing already established), SDL2's audio API (`SDL_OpenAudioDevice`, a pull-model callback that maps directly onto `synth_render_half`'s own "fill this buffer" shape), SDL2's `disk` audio driver for headless verification (confirmed working on this machine).

**Spec:** `docs/superpowers/specs/2026-09-14-acid-os-v2-audio-design.md`

## Global Constraints

- RaveOS v1's existing paths (`kernel/`, `boot/`, `programs/`, `demos/`) must not be touched by any task in this plan — `kernel/audio/synth.c`/`.h` are *read* (copied from), never modified in place.
- Everything new lives under `v2/`.
- Same engine as v1 — `synth.c`/`.h` are ported verbatim, no modifications beyond the copy itself.
- Ruby API for this phase is exactly `acid_play_note(voice, ona, volume)` / `acid_stop_note(voice)` — no waveform/ADSR/filter/arpeggio control exposed to Ruby yet (deferred to phase 4, once a real app needs it).
- One shared, machine-wide synth — not per-app, not per-window. Voice stealing ("last write wins") is accepted, matching family-mruby-os's own explicit design for the same class of shared resource.
- `synth_voices[]` and per-voice ownership tracking are touched **only** by the code draining the audio command queue (the SDL callback's own thread) — every other caller (any app task) only ever enqueues a command, never touches synth state directly.
- `hw` target gets a stub (`hal_audio_hw.c`, logging only, no real I2S) — matching `hal_display_hw.c`'s existing precedent. Real I2S is roadmap phase 5 (real hardware bring-up), not this phase.
- This phase targets `sim` only for verification (via SDL2's `disk` audio driver, inspecting real generated PCM bytes — the audio-domain equivalent of bring-up's screenshots and windowing's pixel sampling). `hw` gets the same new source files added to its build so it isn't left behind; `idf.py build` itself remains unrunnable on this machine (no ESP-IDF installed) — the same pre-existing, documented gap carried forward from bring-up and windowing.
- Every new SDL2/FreeRTOS API call in this plan was verified against the real headers on this machine (`/usr/include/SDL2/SDL_audio.h`, `/usr/include/SDL2/SDL_stdinc.h`, `v2/components/freertos-kernel/include/task.h`) before being written into a step here.
- Kernel/GUI/audio logic is plain C; SDL2's audio API is a standard C API requiring no C++ (unlike LovyanGFX's templated drawing API) — `hal_audio_sim.c` is plain C, not a new `.cpp` file.

---

## Task 1: Port `synth.c`/`synth.h`, standalone test

**Files:**
- Create: `v2/core/audio/synth.h`
- Create: `v2/core/audio/synth.c`
- Create: `v2/core/audio/test_synth.c`

**Interfaces:**
- Produces: the full `synth.h` API (`synth_init`, `synth_set_voice_waveform`, `synth_set_ona`, `synth_set_adsr`, `synth_gate_on`, `synth_gate_off`, `synth_render_half`, etc.) — Task 3's HAL implementation calls `synth_render_half` directly; Task 2's `kernel_audio.c` calls `synth_set_ona`/`synth_gate_on`/`synth_gate_off` to apply queued commands.

This is a pure copy with no dependencies (confirmed: `kernel/audio/synth.c`'s only `#include` is `"synth.h"`, no floating point, no kernel calls) — standalone-testable the same way Task 1 of the windowing plan tested `kernel_window.c`.

- [ ] **Step 1: Copy the two files verbatim**

```bash
cd /home/norfolkh/os
cp kernel/audio/synth.h v2/core/audio/synth.h
cp kernel/audio/synth.c v2/core/audio/synth.c
```

Do not edit either file's contents — this is a verbatim port.

- [ ] **Step 2: Write the failing test — `v2/core/audio/test_synth.c`**

Verifies the actual vertical slice this task exists to prove: initializing a voice and gating it on produces real, non-silence PCM data when rendered, and gating it off eventually lets it decay back toward silence.

```c
#include <assert.h>
#include <stdio.h>

#include "synth.h"

int
main( void )
{
    synth_init();

    /* Voice 0, ona 49 = A4 = 440 Hz (per synth.h's own documented
     * numbering: "Standard 88-key piano numbering, ona 49 = A4 = 440.0Hz").
     * synth_init() leaves attack_rate at its maximum (SYNTH_ENV_FULL << 8),
     * so gating on should bring the envelope to full volume within a
     * handful of samples, not a slow fade. */
    synth_set_voice_waveform( 0, WAVE_PULSE );
    synth_set_ona( 0, 49 );
    synth_gate_on( 0 );

    unsigned char buf[ 512 ];
    synth_render_half( buf, sizeof( buf ) );

    /* Real audio, not silence: at least some bytes must differ from 128
     * (the documented silence value) once the voice has been gated on
     * and rendered for this many samples. */
    int i;
    int saw_non_silence = 0;
    for( i = 0; i < 512; i++ )
    {
        if( buf[ i ] != 128 )
        {
            saw_non_silence = 1;
            break;
        }
    }
    assert( saw_non_silence );

    /* Gate off, then render enough samples for the release stage (default
     * release_rate is also SYNTH_ENV_FULL << 8, i.e. fast) to bring the
     * envelope back down. A voice with envelope_stage == ENV_OFF (or
     * ENV_RELEASE settled at envelope_level == 0) renders pure silence. */
    synth_gate_off( 0 );
    unsigned char buf2[ 4096 ];
    synth_render_half( buf2, sizeof( buf2 ) );

    int all_silent_after_release = 1;
    for( i = 0; i < 4096; i++ )
    {
        if( buf2[ i ] != 128 )
        {
            all_silent_after_release = 0;
            break;
        }
    }
    assert( all_silent_after_release );

    printf( "test_synth: all assertions passed\n" );
    return 0;
}
```

- [ ] **Step 3: Compile and run — confirm it passes**

```bash
gcc -std=c99 -Wall -Wextra -o /tmp/test_synth \
    /home/norfolkh/os/v2/core/audio/synth.c \
    /home/norfolkh/os/v2/core/audio/test_synth.c \
    -I /home/norfolkh/os/v2/core/audio
/tmp/test_synth
echo "exit=$?"
```

Expected: no compiler warnings, prints `test_synth: all assertions passed`, `exit=0`. If the "all silent after release" assertion fails, it likely means 4096 samples (~186 ms at 22050 Hz) isn't long enough for the default release rate to fully settle — read `synth_envelope_advance_sample`'s actual behavior in `synth.c` and adjust the buffer size in the test (not the synth code) until the assertion reflects real, correct behavior rather than being loosened to hide a misunderstanding.

- [ ] **Step 4: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/audio/synth.h v2/core/audio/synth.c v2/core/audio/test_synth.c
git commit -m "v2: port RaveOS v1's synth.c verbatim, standalone test"
```

---

## Task 2: `kernel_audio` — the shared command queue and voice ownership

**Files:**
- Create: `v2/core/kernel/kernel_audio_command.h`
- Create: `v2/core/kernel/kernel_audio.h`
- Create: `v2/core/kernel/kernel_audio.c`

**Interfaces:**
- Consumes: `synth_set_voice_waveform`/`synth_set_ona`/`synth_gate_on`/`synth_gate_off`/`synth_render_half` (Task 1).
- Produces: `kernel_audio_init(void)` (Task 5's `sim_main.c` calls this once at boot), `kernel_audio_enqueue_note_on`/`kernel_audio_enqueue_note_off` (Task 4's mruby binding calls these), `kernel_audio_release_owner` (Task 5's `vm_host.c` cleanup calls this), `kernel_audio_drain_and_render` (Task 3's SDL callback calls this — the one function that's safe to call only from the audio callback's own thread).

- [ ] **Step 1: Write `v2/core/kernel/kernel_audio_command.h`**

```c
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
```

- [ ] **Step 2: Write `v2/core/kernel/kernel_audio.h`**

```c
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
```

- [ ] **Step 3: Write `v2/core/kernel/kernel_audio.c`**

`xQueueCreate`/`xQueueSendToBack`/`xQueueReceive` are the exact same, already-verified FreeRTOS queue API windowing's own `kernel_router.c`/`kernel_spawn.c` already use.

```c
#include "FreeRTOS.h"
#include "queue.h"

#include "kernel_audio.h"
#include "kernel_audio_command.h"
#include "../audio/synth.h"

#define KERNEL_AUDIO_QUEUE_LEN 16

static QueueHandle_t g_audio_queue;
static void * g_voice_owner[ SYNTH_NUM_VOICES ];

void
kernel_audio_init( void )
{
    synth_init();
    g_audio_queue = xQueueCreate( KERNEL_AUDIO_QUEUE_LEN, sizeof( struct kernel_audio_command ) );

    int i;
    for( i = 0; i < SYNTH_NUM_VOICES; i++ )
    {
        g_voice_owner[ i ] = NULL;
    }
}

static void
enqueue( struct kernel_audio_command * cmd )
{
    xQueueSendToBack( g_audio_queue, cmd, 0 );
}

void
kernel_audio_enqueue_note_on( void * owner_task, int voice, int ona, int volume )
{
    struct kernel_audio_command cmd;
    cmd.type = AUDIO_CMD_NOTE_ON;
    cmd.voice = voice;
    cmd.ona = ona;
    cmd.volume = volume;
    cmd.owner_task = owner_task;
    enqueue( &cmd );
}

void
kernel_audio_enqueue_note_off( void * owner_task, int voice )
{
    struct kernel_audio_command cmd;
    cmd.type = AUDIO_CMD_NOTE_OFF;
    cmd.voice = voice;
    cmd.ona = 0;
    cmd.volume = 0;
    cmd.owner_task = owner_task;
    enqueue( &cmd );
}

void
kernel_audio_release_owner( void * owner_task )
{
    struct kernel_audio_command cmd;
    cmd.type = AUDIO_CMD_RELEASE_OWNER;
    cmd.voice = 0;
    cmd.ona = 0;
    cmd.volume = 0;
    cmd.owner_task = owner_task;
    enqueue( &cmd );
}

/* Only ever called from the audio callback's own thread -- see this
 * file's header comment on kernel_audio_drain_and_render. Every mutation
 * of synth_voices[]/g_voice_owner[] happens here and nowhere else. */
static void
apply( const struct kernel_audio_command * cmd )
{
    if( cmd->type == AUDIO_CMD_NOTE_ON )
    {
        if( cmd->voice < 0 || cmd->voice >= SYNTH_NUM_VOICES )
        {
            return;
        }
        synth_set_ona( cmd->voice, cmd->ona );
        /* volume is 0..100 from the Ruby-facing API; synth's own envelope
         * scale is 0..SYNTH_ENV_FULL (32768) -- sustain_level is what
         * actually caps a gated-on voice's held volume. */
        synth_voices[ cmd->voice ].sustain_level =
            ( ( cmd->volume < 0 ? 0 : ( cmd->volume > 100 ? 100 : cmd->volume ) )
              * SYNTH_ENV_FULL / 100 ) << 8; /* Q8 scale, per synth.h's own struct comment */
        synth_gate_on( cmd->voice );
        g_voice_owner[ cmd->voice ] = cmd->owner_task;
    }
    else if( cmd->type == AUDIO_CMD_NOTE_OFF )
    {
        if( cmd->voice < 0 || cmd->voice >= SYNTH_NUM_VOICES )
        {
            return;
        }
        /* Unconditional, per this plan's spec: whichever voice is
         * currently there gets gated off, regardless of who is asking --
         * see the spec's "Ambiguity closed explicitly" note on voice
         * stealing. */
        synth_gate_off( cmd->voice );
        g_voice_owner[ cmd->voice ] = NULL;
    }
    else if( cmd->type == AUDIO_CMD_RELEASE_OWNER )
    {
        int i;
        for( i = 0; i < SYNTH_NUM_VOICES; i++ )
        {
            if( g_voice_owner[ i ] == cmd->owner_task )
            {
                synth_gate_off( i );
                g_voice_owner[ i ] = NULL;
            }
        }
    }
}

void
kernel_audio_drain_and_render( unsigned char * buf, unsigned int len )
{
    struct kernel_audio_command cmd;
    while( xQueueReceive( g_audio_queue, &cmd, 0 ) == pdTRUE )
    {
        apply( &cmd );
    }
    synth_render_half( buf, len );
}
```

- [ ] **Step 4: Compile-check (no build target owns this yet — a plain syntax/type check against the real headers)**

```bash
gcc -std=c99 -Wall -Wextra -fsyntax-only \
    -I /home/norfolkh/os/v2/core/audio \
    -I /home/norfolkh/os/v2/core/kernel \
    -I /home/norfolkh/os/v2/components/freertos-kernel/include \
    -I /home/norfolkh/os/v2/sim \
    /home/norfolkh/os/v2/core/kernel/kernel_audio.c
```

Expected: no errors. (This machine's `v2/sim/FreeRTOSConfig.h` is picked up via the `-I .../v2/sim` path, matching how the real CMake build already locates it for every other `core/kernel/*.c` file — if this specific invocation reports it can't find `FreeRTOS.h`'s own config, double-check against how `v2/sim/CMakeLists.txt` already builds `kernel_router.c`/`kernel_spawn.c` for the exact include path shape and adjust the command, not the source.) Task 5 is where this file is actually added to the real build and linked.

- [ ] **Step 5: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/kernel/kernel_audio_command.h v2/core/kernel/kernel_audio.h v2/core/kernel/kernel_audio.c
git commit -m "v2: kernel_audio -- shared command queue and voice ownership, single-thread-owns-state"
```

---

## Task 3: HAL — SDL2 audio callback (sim), stub (hw)

**Files:**
- Create: `v2/core/hal/hal_audio.h`
- Create: `v2/sim/hal_audio_sim.c`
- Create: `v2/hw/main/hal_audio_hw.c`

**Interfaces:**
- Consumes: `kernel_audio_drain_and_render` (Task 2).
- Produces: `hal_audio_init(void)` — Task 5's `sim_main.c`/`app_main.c` call this once at boot, right after `kernel_audio_init()`.

- [ ] **Step 1: Write `v2/core/hal/hal_audio.h`**

```c
#ifndef ACID_HAL_AUDIO_H
#define ACID_HAL_AUDIO_H

void hal_audio_init( void );

#endif
```

- [ ] **Step 2: Write `v2/sim/hal_audio_sim.c`**

Verified real API, `/usr/include/SDL2/SDL_audio.h` on this machine: `SDL_AudioCallback` is `void (SDLCALL *)(void *userdata, Uint8 *stream, int len)` — matches `kernel_audio_drain_and_render`'s own `(unsigned char *, unsigned int)` shape exactly (a cast, not a signature mismatch). `SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes)` and `SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on)` are both real, confirmed against the same header. `SDL_InitSubSystem(SDL_INIT_AUDIO)` is necessary and not already done elsewhere: bring-up's `Panel_sdl::main()` only calls `SDL_Init(SDL_INIT_VIDEO)` (verified directly against `v2/components/lovyangfx/src/lgfx/v1/platforms/sdl/Panel_sdl.cpp:266`).

```c
#include <SDL2/SDL.h>

#include "hal_audio.h"
#include "../core/kernel/kernel_audio.h"
#include "../core/audio/synth.h"

static void
sdl_audio_callback( void * userdata, Uint8 * stream, int len )
{
    ( void ) userdata;
    kernel_audio_drain_and_render( ( unsigned char * ) stream, ( unsigned int ) len );
}

void
hal_audio_init( void )
{
    SDL_InitSubSystem( SDL_INIT_AUDIO );

    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    SDL_zero( desired );
    desired.freq = SYNTH_SAMPLE_RATE;
    desired.format = AUDIO_U8;
    desired.channels = 1;
    desired.samples = 1024;
    desired.callback = sdl_audio_callback;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice( NULL, 0, &desired, &obtained, 0 );
    SDL_PauseAudioDevice( dev, 0 );
}
```

- [ ] **Step 3: Write `v2/hw/main/hal_audio_hw.c`**

Matches `v2/hw/main/hal_display_hw.c`'s existing stub pattern exactly.

```c
#include "esp_log.h"
#include "../../core/hal/hal_audio.h"

static const char * TAG = "hal_audio_hw";

void
hal_audio_init( void )
{
    ESP_LOGI( TAG, "hal_audio_init: stub, no I2S output wired up yet" );
}
```

- [ ] **Step 4: Build `sim` with this new file added, ad hoc (Task 5 wires it into the real CMakeLists.txt permanently — this step is just to catch a compile error early, at the smallest possible scope)**

```bash
gcc -std=c99 -Wall -Wextra -fsyntax-only \
    $(pkg-config --cflags sdl2) \
    -I /home/norfolkh/os/v2/core/audio \
    -I /home/norfolkh/os/v2/core/kernel \
    -I /home/norfolkh/os/v2/core/hal \
    -I /home/norfolkh/os/v2/components/freertos-kernel/include \
    -I /home/norfolkh/os/v2/sim \
    /home/norfolkh/os/v2/sim/hal_audio_sim.c
```

Expected: no errors.

- [ ] **Step 5: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/hal/hal_audio.h v2/sim/hal_audio_sim.c v2/hw/main/hal_audio_hw.c
git commit -m "v2: hal_audio -- SDL2 callback wiring (sim), stub (hw)"
```

---

## Task 4: mruby bindings — `acid_play_note`/`acid_stop_note`

**Files:**
- Create: `v2/core/bindings/audio_binding.h`
- Create: `v2/core/bindings/audio_binding.c`

**Interfaces:**
- Consumes: `kernel_audio_enqueue_note_on`/`kernel_audio_enqueue_note_off` (Task 2).
- Produces: `acid_audio_bindings_register(mrb_state *)` — Task 5's `vm_host.c` calls this alongside the existing `acid_bindings_register`/`acid_event_bindings_register`/`acid_chrome_bindings_register` calls.

- [ ] **Step 1: Write `v2/core/bindings/audio_binding.h`**

```c
#ifndef ACID_AUDIO_BINDING_H
#define ACID_AUDIO_BINDING_H

#include "mruby.h"

void acid_audio_bindings_register( mrb_state * mrb );

#endif
```

- [ ] **Step 2: Write `v2/core/bindings/audio_binding.c`**

`xTaskGetCurrentTaskHandle()` is real, verified API (`v2/components/freertos-kernel/include/task.h:3712`, `TaskHandle_t xTaskGetCurrentTaskHandle(void)`) — already used the same way in `v2/core/vm_host/vm_host.c`'s own cleanup path. `mrb_get_args`/`MRB_ARGS_REQ` are the same already-verified mruby API every other binding in this project uses.

```c
#include "audio_binding.h"

#include "FreeRTOS.h"
#include "task.h"

#include "../kernel/kernel_audio.h"

static mrb_value
acid_play_note( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int voice, ona, volume;
    mrb_get_args( mrb, "iii", &voice, &ona, &volume );
    kernel_audio_enqueue_note_on( ( void * ) xTaskGetCurrentTaskHandle(),
                                   ( int ) voice, ( int ) ona, ( int ) volume );
    return mrb_nil_value();
}

static mrb_value
acid_stop_note( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int voice;
    mrb_get_args( mrb, "i", &voice );
    kernel_audio_enqueue_note_off( ( void * ) xTaskGetCurrentTaskHandle(), ( int ) voice );
    return mrb_nil_value();
}

void
acid_audio_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_play_note",
                                 acid_play_note, MRB_ARGS_REQ( 3 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_stop_note",
                                 acid_stop_note, MRB_ARGS_REQ( 1 ) );
}
```

- [ ] **Step 3: Compile-check**

```bash
gcc -std=c99 -Wall -Wextra -fsyntax-only \
    -I /home/norfolkh/os/v2/core/kernel \
    -I /home/norfolkh/os/v2/core/audio \
    -I /home/norfolkh/os/v2/components/freertos-kernel/include \
    -I /home/norfolkh/os/v2/sim \
    -I /home/norfolkh/os/v2/components/mruby/include \
    /home/norfolkh/os/v2/core/bindings/audio_binding.c
```

Expected: no errors.

- [ ] **Step 4: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/bindings/audio_binding.h v2/core/bindings/audio_binding.c
git commit -m "v2: acid_play_note/acid_stop_note mruby bindings"
```

---

## Task 5: Wire it all together on `sim` — boot, cleanup, demo app, real verification

**Files:**
- Modify: `v2/core/vm_host/vm_host.c`
- Modify: `v2/sim/sim_main.c`
- Modify: `v2/sim/CMakeLists.txt`
- Modify: `v2/apps/demo_touch.rb`

**Interfaces:**
- Consumes: everything from Tasks 1-4.
- Produces: nothing further builds on this within `sim` — this is where the phase's own definition of done gets proven end to end.

- [ ] **Step 1: Register the audio bindings and add cleanup in `v2/core/vm_host/vm_host.c`**

Add the include, alongside the existing binding includes:

```c
#include "../bindings/audio_binding.h"
```

and

```c
#include "../kernel/kernel_audio.h"
```

In `vm_host_task`, add the registration call right after the existing `acid_chrome_bindings_register( mrb );` line:

```c
    acid_audio_bindings_register( mrb );
```

And add the cleanup call in the existing unconditional-cleanup block, right after `kernel_window_unregister( ( void * ) xTaskGetCurrentTaskHandle() );` and before `vQueueDelete( params->queue );`:

```c
    kernel_audio_release_owner( ( void * ) xTaskGetCurrentTaskHandle() );
```

(This is a non-blocking enqueue, same as every other `kernel_audio_*` call from an app task — it does not touch `synth_voices[]` directly, per this plan's Global Constraints, so it's safe to call here unconditionally, the same reasoning that already justifies the adjacent `kernel_window_unregister` call.)

- [ ] **Step 2: Wire `kernel_audio_init()`/`hal_audio_init()` into `v2/sim/sim_main.c`'s boot sequence**

Add the includes:

```c
#include "../core/kernel/kernel_audio.h"
#include "../core/hal/hal_audio.h"
```

In `sim_freertos_main`, add both calls right after the existing `gfx_init();` line, before any `kernel_spawn_app` call:

```c
    kernel_audio_init();
    hal_audio_init();
```

(`kernel_audio_init()` before `hal_audio_init()`: the queue and voice-ownership table must exist before SDL's audio callback — which could theoretically fire the moment `SDL_PauseAudioDevice` unpauses the device — ever has a chance to call `kernel_audio_drain_and_render`.)

- [ ] **Step 3: Update `v2/sim/CMakeLists.txt`'s source list**

```cmake
add_executable(acidos_sim
    sim_main.c
    hal_display_sim.cpp
    hal_audio_sim.c
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/vm_host/vm_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/gfx_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/event_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/chrome_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/audio_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/gfx/gfx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/audio/synth.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_window.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_spawn.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_router.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_audio.c
    ${LGFX_SOURCES}
)
```

(This replaces the existing 10-entry list — `hal_audio_sim.c`, `audio_binding.c`, `synth.c`, and `kernel_audio.c` are the four new entries; every other line is unchanged from the file's current content.)

- [ ] **Step 4: Extend `v2/apps/demo_touch.rb` to play a note on touch, stop it on release**

Maps the touch's `x` position (0..140, this window's own width) to an audible pitch range, so different touch positions produce different notes — proving both `acid_play_note` and `acid_stop_note` through the same natural press/release gesture windowing's own touch model already delivers.

```ruby
class DemoTouchApp < AcidApp
  def on_touch(x, y, pressed)
    if pressed
      acid_fill_rect(x - 5, y - 5, 10, 10, 0x00FF66)
      ona = 40 + (x / 3)
      acid_play_note(0, ona, 80)
    else
      acid_stop_note(0)
    end
  end
end

DemoTouchApp.new.start
```

- [ ] **Step 5: Build**

```bash
cd /home/norfolkh/os
rm -rf v2/sim/build
cmake -S v2/sim -B v2/sim/build
cmake --build v2/sim/build
```

Expected: builds cleanly.

- [ ] **Step 6: Run and verify real audio output via SDL2's `disk` audio driver**

```bash
cd /home/norfolkh/os
rm -f /tmp/acid_audio_test.raw
SDL_AUDIODRIVER=disk SDL2_DISKAUDIOFILE=/tmp/acid_audio_test.raw \
    xvfb-run -a ./v2/sim/build/acidos_sim &
SIM_PID=$!
sleep 1
```

Find the real window and synthesize a real press-and-hold-then-release on `demo_touch`'s body (spawned at `(10, 30, 140, 100)`; touch somewhere well inside it, e.g. window-relative `(60, 60)`, avoiding the top 16px title-bar strip) using `xdotool` — `mousedown`/`sleep`/`mouseup`, not an atomic click, per this project's own established practice (an atomic click can race past the router's poll cycle):

```bash
WIN_ID=$(xdotool search --name "LGFX" | head -1)
eval $(xwininfo -id "$WIN_ID" | awk '/Absolute upper-left X/{print "WX="$4} /Absolute upper-left Y/{print "WY="$4}')
xdotool mousemove $((WX + 60)) $((WY + 60))
xdotool mousedown 1
sleep 0.5
xdotool mouseup 1
sleep 0.5
kill $SIM_PID 2>/dev/null
wait $SIM_PID 2>/dev/null
```

- [ ] **Step 7: Inspect the captured audio for real, non-silence PCM data**

```bash
python3 -c "
with open('/tmp/acid_audio_test.raw', 'rb') as f:
    data = f.read()
print('total bytes:', len(data))
non_silent = sum(1 for b in data if b != 128)
print('non-silent bytes:', non_silent)
assert len(data) > 0, 'no audio data captured at all'
assert non_silent > 1000, 'expected substantial non-silence during the held touch'
print('PASS: real audio data captured')
"
```

Expected: `PASS: real audio data captured`, with a non-silent byte count in the thousands (roughly matching the ~0.5s hold at 22050 Hz mono = ~11025 bytes for that portion of the capture, though total file length also includes the surrounding silence before/after the touch).

- [ ] **Step 8: Verify crash/close cleanup — a closed app's note doesn't sound forever**

```bash
cd /home/norfolkh/os
rm -f /tmp/acid_audio_test2.raw
SDL_AUDIODRIVER=disk SDL2_DISKAUDIOFILE=/tmp/acid_audio_test2.raw \
    xvfb-run -a ./v2/sim/build/acidos_sim &
SIM_PID=$!
sleep 1
WIN_ID=$(xdotool search --name "LGFX" | head -1)
eval $(xwininfo -id "$WIN_ID" | awk '/Absolute upper-left X/{print "WX="$4} /Absolute upper-left Y/{print "WY="$4}')
# Press and HOLD (do not release) inside demo_touch's body, then close the
# window via its close button while still held -- this exercises the
# cleanup path (kernel_audio_release_owner), not a normal note_off.
xdotool mousemove $((WX + 60)) $((WY + 60))
xdotool mousedown 1
sleep 0.3
# demo_touch's close button: window (10,30,140,100), close center at
# window-relative (140-8, 8) = (132, 8), absolute (WX+10+132, WY+30+8).
xdotool mousemove $((WX + 10 + 132)) $((WY + 30 + 8))
xdotool mousedown 1
sleep 0.2
xdotool mouseup 1
xdotool mouseup 1
sleep 1
kill $SIM_PID 2>/dev/null
wait $SIM_PID 2>/dev/null
python3 -c "
with open('/tmp/acid_audio_test2.raw', 'rb') as f:
    data = f.read()
# Check the LAST second of captured audio (22050 bytes) -- well after the
# close should have propagated -- is silent, not still sounding the note
# the closed app never released.
tail = data[-22050:] if len(data) >= 22050 else data
non_silent_tail = sum(1 for b in tail if b != 128)
print('non-silent bytes in final second:', non_silent_tail)
assert non_silent_tail < 100, 'note kept sounding after the app that started it was closed'
print('PASS: orphaned note was silenced on app close')
"
```

Expected: `PASS: orphaned note was silenced on app close`. If this fails, do not weaken the assertion — investigate whether `kernel_audio_release_owner`'s enqueue is actually being called from `vm_host.c`'s cleanup path, and whether `AUDIO_CMD_RELEASE_OWNER` is being correctly applied by `kernel_audio.c`'s `apply()` function.

- [ ] **Step 9: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/vm_host/vm_host.c v2/sim/sim_main.c v2/sim/CMakeLists.txt v2/apps/demo_touch.rb
git commit -m "v2: wire audio into boot, cleanup, and demo_touch; verified real PCM output"
```

---

## Task 6: `hw` scaffold — add the new source files (static verification only)

**Files:**
- Modify: `v2/hw/main/CMakeLists.txt`
- Modify: `v2/hw/main/app_main.c`

**Interfaces:**
- Consumes: everything from Tasks 1-5 (the same `core/` files `sim` already builds against, unchanged).
- Produces: nothing further builds on this — this is the plan's last task.

- [ ] **Step 1: Update `v2/hw/main/CMakeLists.txt`**

```cmake
idf_component_register(
    SRCS
        app_main.c
        hal_display_hw.c
        hal_input_hw.c
        hal_audio_hw.c
        ../../core/vm_host/vm_host.c
        ../../core/bindings/gfx_binding.c
        ../../core/bindings/event_binding.c
        ../../core/bindings/chrome_binding.c
        ../../core/bindings/audio_binding.c
        ../../core/gfx/gfx.c
        ../../core/audio/synth.c
        ../../core/kernel/kernel_window.c
        ../../core/kernel/kernel_spawn.c
        ../../core/kernel/kernel_router.c
        ../../core/kernel/kernel_audio.c
    INCLUDE_DIRS
        ../../core/vm_host
        ../../core/hal
        ../../core/gfx
        ../../core/audio
        ../../core/bindings
        ../../core/kernel
        freertos_compat
    REQUIRES mruby_component
)
```

(Adds `hal_audio_hw.c`, `../../core/bindings/audio_binding.c`, `../../core/audio/synth.c`, `../../core/kernel/kernel_audio.c` to `SRCS`, and `../../core/audio` to `INCLUDE_DIRS` — every other line is unchanged from the file's current content.)

- [ ] **Step 2: Update `v2/hw/main/app_main.c`**

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../../core/kernel/kernel_window.h"
#include "../../core/kernel/kernel_spawn.h"
#include "../../core/kernel/kernel_router.h"
#include "../../core/kernel/kernel_audio.h"
#include "../../core/gfx/gfx.h"
#include "../../core/hal/hal_audio.h"

void
app_main( void )
{
    kernel_window_init();
    gfx_init();
    kernel_audio_init();
    hal_audio_init();

    void * desktop_task = kernel_spawn_app( "v2/apps/desktop.rb", 0, 0, 320, 20, 0 );
    kernel_router_set_desktop_task( desktop_task );

    kernel_spawn_app( "v2/apps/demo_touch.rb", 10, 30, 140, 100, 1 );
    kernel_spawn_app( "v2/apps/demo_swatch.rb", 160, 70, 140, 100, 1 );

    xTaskCreate( kernel_router_task, "router", 4096, NULL, 5, NULL );
}
```

- [ ] **Step 3: Static verification (no ESP-IDF on this machine, same gap bring-up and windowing both carried forward)**

Confirm every path in the updated `CMakeLists.txt` resolves to a real file:

```bash
cd /home/norfolkh/os
for f in v2/hw/main/app_main.c v2/hw/main/hal_display_hw.c v2/hw/main/hal_input_hw.c v2/hw/main/hal_audio_hw.c \
         v2/hw/main/../../core/vm_host/vm_host.c \
         v2/hw/main/../../core/bindings/gfx_binding.c v2/hw/main/../../core/bindings/event_binding.c \
         v2/hw/main/../../core/bindings/chrome_binding.c v2/hw/main/../../core/bindings/audio_binding.c \
         v2/hw/main/../../core/gfx/gfx.c v2/hw/main/../../core/audio/synth.c \
         v2/hw/main/../../core/kernel/kernel_window.c v2/hw/main/../../core/kernel/kernel_spawn.c \
         v2/hw/main/../../core/kernel/kernel_router.c v2/hw/main/../../core/kernel/kernel_audio.c; do
    test -f "$f" && echo "OK: $f" || echo "MISSING: $f"
done
```

Expected: every line says `OK:` — if any says `MISSING:`, fix the path in `CMakeLists.txt` before proceeding, don't just note it.

Confirm `hal_audio_hw.c`'s `hal_audio_init` signature matches `core/hal/hal_audio.h`'s declaration exactly (both take no arguments, return void) — read both files directly and compare, don't assume.

If ESP-IDF happens to be available when this task is executed, run the real build and report the actual result instead of only doing static checks:

```bash
cd /home/norfolkh/os/v2/hw
idf.py set-target esp32p4
idf.py build
```

- [ ] **Step 4: Commit**

```bash
cd /home/norfolkh/os
git add v2/hw/main/CMakeLists.txt v2/hw/main/app_main.c
git commit -m "v2/hw: add audio subsystem source files to the build, wire boot sequence"
```

---

## Definition of done (per spec)

- `sim`: `acid_play_note`/`acid_stop_note` are callable from `demo_touch.rb`; touching and holding produces real, non-silence PCM data captured via SDL2's `disk` audio driver; closing an app that left a note playing (via the close button, exercising the same cleanup path a crash would) silences that voice rather than leaving it sounding forever.
- `hw`: all of this phase's new source files are added to `v2/hw/main/CMakeLists.txt`, and `app_main.c` boots the same way `sim_main.c` does (audio init included). `idf.py build` succeeding remains unverified on this machine (no ESP-IDF installed) — the same carried-forward, plan-anticipated gap from bring-up and windowing, not a new one this phase introduces.
