# acid OS v2 Windowing/GUI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Real windowing on the `sim` target — the kernel spawns multiple concurrent mruby app tasks, each owning a titled, closable, draggable window in acid OS v2's own (RaveOS-v1-derived) visual style, with touch input correctly routed by window geometry and z-order.

**Architecture:** A new `core/kernel/` module (window list, input router, app spawner) sits alongside bring-up's `core/vm_host/gfx/bindings/hal`, all still target-agnostic C. Each app is its own FreeRTOS task running its own mruby VM, communicating with the kernel only through a per-app FreeRTOS queue (no shared state). Apps subclass a new Ruby `AcidApp` base class whose chrome-drawing and event-loop methods call thin C bindings — apps never touch raw window geometry or hand-paint their own title bar.

**Tech Stack:** C (all new kernel/binding code), Ruby (mruby, the `AcidApp` framework and app scripts), FreeRTOS queues (native `xQueueCreate`/`xQueueSendToBack`/`xQueueReceive` — no custom transport), LovyanGFX's `getTouch()` (single-pointer touch, sim-backed by SDL mouse events with zero extra glue).

**Spec:** `docs/superpowers/specs/2026-09-13-acid-os-v2-windowing-design.md`

## Global Constraints

- RaveOS v1's existing paths (`kernel/`, `boot/`, `programs/`, `demos/`) must not be touched by any task in this plan.
- Everything new lives under `v2/`.
- No resize, no keyboard input, no fullscreen, no taskbar/launcher, no audio — all explicitly deferred per the spec's "Explicitly deferred" section. Do not build any of it.
- Visual palette is RaveOS v1's exact documented colors (`docs/BUILD_LOG.md`): `--bg #050607`, `--hard #00ff66`, `--panel 0x0B1712`, `--text 0xD4E6DB`, `--muted 0x9DAAA3`. Do not invent new colors.
- One touch point only (press/move/release) — no mouse wheel, no multi-touch.
- This phase targets `sim` only for verification (build+run+click, same as bring-up's Xvfb-based method). `hw`'s scaffold gets the same new source files added to its `CMakeLists.txt` so it isn't left behind, but `idf.py build` itself remains unrunnable on this machine (the same pre-existing, documented environment gap from bring-up) — do not attempt to install ESP-IDF or otherwise work around this.
- Every new mruby C API call must be verified against the vendored `v2/components/mruby/include/` headers before being written into a step here — this plan already did that verification; if an implementer finds a mismatch against the real vendored source, stop and report rather than guessing.
- Kernel/GUI logic is plain C; the only sanctioned C++ remains the existing LovyanGFX/SDL glue (`v2/sim/hal_display_sim.cpp`, `v2/sim/main.cpp`) — this plan's new sim-side touch code extends that same file, it does not add new C++ files.
- Run every sim build/run verification in this plan the same way bring-up's later tasks did: `xvfb-run -a <command>` when no display is attached, from the repo root (`/home/norfolkh/os`) so relative script paths resolve.

---

## Task 1: Window list (`core/kernel/kernel_window.{c,h}`)

**Files:**
- Create: `v2/core/kernel/kernel_window.h`
- Create: `v2/core/kernel/kernel_window.c`
- Create: `v2/core/kernel/test_kernel_window.c`

**Interfaces:**
- Produces: `struct kernel_window` and `kernel_window_init/register/unregister/find_at/by_task/bring_to_front/count/at_index` — Tasks 4 and 5 build the router and spawner directly on top of this, unchanged.

This module is deliberately dependency-free (no FreeRTOS, no mruby) — `task`/`queue` are stored as opaque `void *` handles, cast to their real types only by callers in later tasks that know what they are. This makes it compilable and testable as a plain standalone C program, no CMake/FreeRTOS toolchain needed.

- [ ] **Step 1: Write `v2/core/kernel/kernel_window.h`**

```c
#ifndef ACID_KERNEL_WINDOW_H
#define ACID_KERNEL_WINDOW_H

#define KERNEL_WINDOW_MAX 8

struct kernel_window
{
    void * task;          /* opaque TaskHandle_t */
    void * queue;          /* opaque QueueHandle_t */
    const char * app_name;
    int x, y, w, h;
    int z_order;
    int closable;
    int in_use;
};

void kernel_window_init( void );
int kernel_window_register( void * task, void * queue, const char * app_name,
                             int x, int y, int w, int h, int closable );
void kernel_window_unregister( void * task );
struct kernel_window * kernel_window_find_at( int x, int y );
struct kernel_window * kernel_window_by_task( void * task );
void kernel_window_bring_to_front( void * task );
int kernel_window_count( void );
struct kernel_window * kernel_window_at_index( int index );

#endif
```

- [ ] **Step 2: Write `v2/core/kernel/kernel_window.c`**

```c
#include <string.h>

#include "kernel_window.h"

static struct kernel_window g_windows[ KERNEL_WINDOW_MAX ];
static int g_next_z;

void
kernel_window_init( void )
{
    memset( g_windows, 0, sizeof( g_windows ) );
    g_next_z = 1;
}

int
kernel_window_register( void * task, void * queue, const char * app_name,
                         int x, int y, int w, int h, int closable )
{
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( !g_windows[ i ].in_use )
        {
            g_windows[ i ].task = task;
            g_windows[ i ].queue = queue;
            g_windows[ i ].app_name = app_name;
            g_windows[ i ].x = x;
            g_windows[ i ].y = y;
            g_windows[ i ].w = w;
            g_windows[ i ].h = h;
            g_windows[ i ].z_order = g_next_z++;
            g_windows[ i ].closable = closable;
            g_windows[ i ].in_use = 1;
            return 1;
        }
    }
    return 0;
}

void
kernel_window_unregister( void * task )
{
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( g_windows[ i ].in_use && g_windows[ i ].task == task )
        {
            g_windows[ i ].in_use = 0;
            return;
        }
    }
}

struct kernel_window *
kernel_window_find_at( int x, int y )
{
    struct kernel_window * best = NULL;
    int best_z = -1;
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( !g_windows[ i ].in_use )
        {
            continue;
        }
        if( x >= g_windows[ i ].x && x < g_windows[ i ].x + g_windows[ i ].w &&
            y >= g_windows[ i ].y && y < g_windows[ i ].y + g_windows[ i ].h )
        {
            if( g_windows[ i ].z_order > best_z )
            {
                best_z = g_windows[ i ].z_order;
                best = &g_windows[ i ];
            }
        }
    }
    return best;
}

struct kernel_window *
kernel_window_by_task( void * task )
{
    int i;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( g_windows[ i ].in_use && g_windows[ i ].task == task )
        {
            return &g_windows[ i ];
        }
    }
    return NULL;
}

void
kernel_window_bring_to_front( void * task )
{
    struct kernel_window * win = kernel_window_by_task( task );
    if( win != NULL )
    {
        win->z_order = g_next_z++;
    }
}

int
kernel_window_count( void )
{
    int i, count = 0;
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        if( g_windows[ i ].in_use )
        {
            count++;
        }
    }
    return count;
}

struct kernel_window *
kernel_window_at_index( int index )
{
    if( index < 0 || index >= KERNEL_WINDOW_MAX )
    {
        return NULL;
    }
    return &g_windows[ index ];
}
```

- [ ] **Step 3: Write the failing test — `v2/core/kernel/test_kernel_window.c`**

```c
#include <assert.h>
#include <stdio.h>

#include "kernel_window.h"

int
main( void )
{
    kernel_window_init();

    int dummy_a, dummy_b, dummy_c;
    void * task_a = &dummy_a;
    void * task_b = &dummy_b;
    void * task_c = &dummy_c;

    assert( kernel_window_count() == 0 );
    assert( kernel_window_register( task_a, NULL, "a", 0, 0, 100, 100, 1 ) == 1 );
    assert( kernel_window_register( task_b, NULL, "b", 50, 50, 100, 100, 1 ) == 1 );
    assert( kernel_window_count() == 2 );

    /* (60,60) is inside both; b was registered later so it has the higher
     * z-order and should win the hit-test. */
    struct kernel_window * hit = kernel_window_find_at( 60, 60 );
    assert( hit != NULL && hit->task == task_b );

    hit = kernel_window_find_at( 10, 10 );
    assert( hit != NULL && hit->task == task_a );

    hit = kernel_window_find_at( 500, 500 );
    assert( hit == NULL );

    /* Bring a to front; the overlap region should now hit a instead. */
    kernel_window_bring_to_front( task_a );
    hit = kernel_window_find_at( 60, 60 );
    assert( hit != NULL && hit->task == task_a );

    kernel_window_unregister( task_a );
    assert( kernel_window_count() == 1 );
    hit = kernel_window_find_at( 10, 10 );
    assert( hit == NULL );

    /* Fill the remaining slots and confirm the cap is enforced. */
    int i;
    int registered = 1; /* b is still registered */
    for( i = 0; i < KERNEL_WINDOW_MAX; i++ )
    {
        void * extra_task = ( void * ) ( long ) ( 1000 + i );
        if( kernel_window_register( extra_task, NULL, "extra", 0, 0, 1, 1, 1 ) )
        {
            registered++;
        }
    }
    assert( registered == KERNEL_WINDOW_MAX );
    assert( kernel_window_register( task_c, NULL, "c", 0, 0, 1, 1, 1 ) == 0 );

    printf( "kernel_window: all assertions passed\n" );
    return 0;
}
```

- [ ] **Step 4: Compile and run — confirm it passes**

```bash
gcc -std=c99 -Wall -Wextra -o /tmp/test_kernel_window \
    /home/norfolkh/os/v2/core/kernel/kernel_window.c \
    /home/norfolkh/os/v2/core/kernel/test_kernel_window.c \
    -I /home/norfolkh/os/v2/core/kernel
/tmp/test_kernel_window
echo "exit=$?"
```

Expected: no compiler warnings, prints `kernel_window: all assertions passed`, `exit=0`.

- [ ] **Step 5: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/kernel/kernel_window.h v2/core/kernel/kernel_window.c v2/core/kernel/test_kernel_window.c
git commit -m "v2: window list (geometry, z-order, hit-testing) with a standalone test"
```

---

## Task 2: HAL touch input

**Files:**
- Modify: `v2/core/hal/hal_input.h`
- Modify: `v2/sim/hal_display_sim.cpp`
- Modify: `v2/hw/main/hal_input_hw.c`
- Modify: `v2/core/vm_host/vm_host.c` (temporary diagnostic, superseded by Task 3)

**Interfaces:**
- Produces: `hal_input_poll_touch(int *x, int *y, bool *pressed)` (declared in `core/hal/hal_input.h`) — Task 4's `kernel_router` becomes the real, permanent consumer; this task's own use of it in `vm_host.c` is temporary verification, replaced by Task 3.

- [ ] **Step 1: Add `hal_input_poll_touch` to `v2/core/hal/hal_input.h`**

Replace the file's full content with:

```c
#ifndef ACID_HAL_INPUT_H
#define ACID_HAL_INPUT_H

#include <stdbool.h>

/* Returns nonzero once the platform wants the app to exit (e.g. the sim
 * window was closed). Bring-up doesn't do real keyboard/mouse/touch
 * events yet -- that's the windowing/GUI phase (roadmap item 2). */
int hal_input_should_quit( void );

/* Polls the platform's single touch point. *pressed is set true/false;
 * *x/*y are only meaningful when *pressed is true. One touch point only --
 * this project's whole input vocabulary for the windowing phase (see the
 * windowing design spec's "input model" decision). */
void hal_input_poll_touch( int * x, int * y, bool * pressed );

#endif
```

- [ ] **Step 2: Implement it in `v2/sim/hal_display_sim.cpp`**

Add this function to the end of the file (the existing `#include`s and `static LGFX lcd(320, 240);` stay unchanged — this reads the same `lcd` object `hal_display_fill_rect` already uses):

```cpp
extern "C" void hal_input_poll_touch( int * x, int * y, bool * pressed )
{
    lgfx::v1::touch_point_t tp;
    uint_fast8_t count = lcd.getTouch( &tp, 1 );
    *pressed = ( count > 0 );
    if( count > 0 )
    {
        *x = tp.x;
        *y = tp.y;
    }
}
```

(Verified real API: `LGFX_Device::getTouch(touch_point_t*, uint8_t)` at `v2/components/lovyangfx/src/lgfx/v1/LGFXBase.hpp:1465`; on `sim` it is backed by SDL mouse events via `Panel_sdl::getTouchRaw()` at `v2/components/lovyangfx/src/lgfx/v1/platforms/sdl/Panel_sdl.cpp:460` — no extra glue needed. `touch_point_t` is defined at `v2/components/lovyangfx/src/lgfx/v1/Touch.hpp:29` with `int16_t x, y` fields.)

- [ ] **Step 3: Stub it in `v2/hw/main/hal_input_hw.c`**

Replace the file's full content with:

```c
#include "../../core/hal/hal_input.h"

int
hal_input_should_quit( void )
{
    return 0;
}

void
hal_input_poll_touch( int * x, int * y, bool * pressed )
{
    ( void ) x;
    ( void ) y;
    *pressed = false;
}
```

(Same honest-stub pattern bring-up already established for `hal_display_hw.c` — no real Tab5 touch driver exists yet; that's roadmap phase 5.)

- [ ] **Step 4: Temporarily wire it into `vm_host.c`'s existing parking loop to prove it works**

In `v2/core/vm_host/vm_host.c`, add `#include <stdbool.h>` near the top (alongside the existing includes), and replace the parking loop at the end of `vm_host_task` from:

```c
    for( ;; )
    {
        if( hal_input_should_quit() )
        {
            exit( 0 );
        }
        vTaskDelay( pdMS_TO_TICKS( 100 ) );
    }
```

to:

```c
    for( ;; )
    {
        if( hal_input_should_quit() )
        {
            exit( 0 );
        }
        int tx, ty;
        bool tpressed;
        hal_input_poll_touch( &tx, &ty, &tpressed );
        if( tpressed )
        {
            printf( "acid OS v2: touch at (%d, %d)\n", tx, ty );
            fflush( stdout );
        }
        vTaskDelay( pdMS_TO_TICKS( 100 ) );
    }
```

This is deliberately temporary — Task 3 replaces `vm_host_task` (and this whole parking-loop shape) with the real generalized per-app host loop. Its only job here is to prove `hal_input_poll_touch` reports real coordinates before anything depends on it.

- [ ] **Step 5: Build**

```bash
cd /home/norfolkh/os
rm -rf v2/sim/build
cmake -S v2/sim -B v2/sim/build
cmake --build v2/sim/build
```

Expected: builds cleanly.

- [ ] **Step 6: Run and verify real touch coordinates are reported**

```bash
cd /home/norfolkh/os
xvfb-run -a ./v2/sim/build/acidos_sim &
SIM_PID=$!
sleep 1
# Click at a few points inside the 320x240 window using xdotool if available,
# otherwise skip the automated click and just confirm the process is alive
# and the build/run succeeded; report which verification path was used.
sleep 2
kill $SIM_PID 2>/dev/null
wait $SIM_PID 2>/dev/null
```

Expected: if `xdotool` (or an equivalent way to synthesize an X11 click under the Xvfb display) is available on this machine, clicking inside the window prints `acid OS v2: touch at (X, Y)` lines with coordinates matching where you clicked. If no way to synthesize a click exists in this environment, report that explicitly rather than claiming a click was verified — confirming the build runs without crashing is still real partial evidence, but say plainly that coordinate accuracy wasn't visually confirmed this task.

- [ ] **Step 7: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/hal/hal_input.h v2/sim/hal_display_sim.cpp v2/hw/main/hal_input_hw.c v2/core/vm_host/vm_host.c
git commit -m "v2: add hal_input_poll_touch (single-pointer touch), verified via a temporary debug print"
```

---

## Task 3: Generalized per-app host loop, event-poll binding, and the `AcidApp` framework

**Files:**
- Create: `v2/core/kernel/kernel_app_context.h`
- Create: `v2/core/kernel/kernel_event.h`
- Create: `v2/core/bindings/event_binding.h`
- Create: `v2/core/bindings/event_binding.c`
- Modify: `v2/core/vm_host/vm_host.h`
- Modify: `v2/core/vm_host/vm_host.c`
- Modify: `v2/core/bindings/gfx_binding.c` (window-relative-to-absolute coordinate translation)
- Modify: `v2/sim/sim_main.c`
- Modify: `v2/sim/CMakeLists.txt`
- Create: `v2/apps/lib/acid_app.rb`
- Create: `v2/apps/demo_touch.rb`

**Interfaces:**
- Consumes: `hal_input_poll_touch` (Task 2).
- Produces: `struct vm_host_params` / the new `vm_host_task` signature (Task 4's `kernel_spawn_app` calls this directly), `struct kernel_app_context` (read via `mrb->ud` by every binding from here on — Task 6's chrome bindings depend on its `window_w`/`window_h` fields), `Kernel.acid_poll_event` (Ruby-callable, used by `AcidApp`), `AcidApp#start`/`#on_touch`/`#on_create`/`#on_destroy` (every future app script subclasses this unchanged).

**A real design gap this task closes, not covered explicitly by the spec:** bring-up's `acid_fill_rect` draws to *absolute* screen coordinates (there is one shared `lcd` object, no per-app offscreen canvas — that's exactly the "hardware-composited canvas" complexity the windowing spec explicitly deferred). Once more than one window exists, an app's own drawing calls must be *window-relative* (an app always draws as if its own top-left were `(0,0)`) or every app would draw at the screen's absolute top-left regardless of where its window actually sits. This task makes that translation happen in exactly one place — `gfx_binding.c`'s `acid_fill_rect`, the single mruby-facing entry point every draw call already goes through — by adding the app's own `window_x`/`window_y` (from `kernel_app_context`, i.e. `mrb->ud`) to whatever coordinates the script passes in. `core/gfx/gfx.c`'s own functions stay absolute-coordinate and unchanged; only the binding layer knows about windows at all.

- [ ] **Step 1: Write `v2/core/kernel/kernel_app_context.h`**

```c
#ifndef ACID_KERNEL_APP_CONTEXT_H
#define ACID_KERNEL_APP_CONTEXT_H

#include "queue.h"

/* Stored in mrb->ud (mrb_state's own auxiliary-data field, confirmed present
 * at v2/components/mruby/include/mruby.h:480) for the lifetime of one app's
 * VM, so any mruby binding can reach its owning app's queue and window
 * geometry without a separate lookup mechanism. */
struct kernel_app_context
{
    QueueHandle_t queue;
    int window_x;
    int window_y;
    int window_w;
    int window_h;
};

#endif
```

- [ ] **Step 2: Write `v2/core/kernel/kernel_event.h`**

```c
#ifndef ACID_KERNEL_EVENT_H
#define ACID_KERNEL_EVENT_H

enum kernel_event_type
{
    KERNEL_EVENT_TOUCH = 0,
    KERNEL_EVENT_CLOSE = 1
};

struct kernel_event
{
    int type;
    int x;
    int y;
    int pressed;
};

#endif
```

- [ ] **Step 3: Write `v2/core/bindings/event_binding.h`**

```c
#ifndef ACID_EVENT_BINDING_H
#define ACID_EVENT_BINDING_H

#include "mruby.h"

void acid_event_bindings_register( mrb_state * mrb );

#endif
```

- [ ] **Step 4: Write `v2/core/bindings/event_binding.c`**

mruby API used here — all verified directly against the vendored `v2/components/mruby/include/mruby.h` and `mruby/value.h`: `mrb_get_args` (already used by `gfx_binding.c`), `mrb_fixnum_value`/`mrb_bool_value` (value.h:425/477 area), `mrb_symbol_value`/`mrb_intern_cstr` (for the `:close` symbol), `mrb_ary_new_from_values` (mruby/array.h:139), `MRB_ARGS_REQ` (mruby.h:1075).

(The spec's Architecture section mentions a binding exposing an app's own window geometry to Ruby, for computing chrome placement. Task 6 makes that unnecessary: chrome drawing there is done entirely by a C binding using `kernel_app_context`'s `window_w`/`window_h` directly, so Ruby itself never needs its own window size. Not adding an unused `acid_window_size` binding here — see Task 6 for where that geometry is actually consumed.)

```c
#include <stdlib.h>

#include "event_binding.h"
#include "../kernel/kernel_app_context.h"
#include "../kernel/kernel_event.h"
#include "../hal/hal_input.h"

#include "queue.h"

static mrb_value
acid_poll_event( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int timeout_ms;
    mrb_get_args( mrb, "i", &timeout_ms );

    if( hal_input_should_quit() )
    {
        exit( 0 );
    }

    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    struct kernel_event ev;
    if( xQueueReceive( ctx->queue, &ev, pdMS_TO_TICKS( timeout_ms ) ) != pdTRUE )
    {
        return mrb_nil_value();
    }

    if( ev.type == KERNEL_EVENT_CLOSE )
    {
        return mrb_symbol_value( mrb_intern_cstr( mrb, "close" ) );
    }

    mrb_value values[ 3 ];
    values[ 0 ] = mrb_fixnum_value( ev.x );
    values[ 1 ] = mrb_fixnum_value( ev.y );
    values[ 2 ] = mrb_bool_value( ev.pressed != 0 );
    return mrb_ary_new_from_values( mrb, 3, values );
}

void
acid_event_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_poll_event",
                                 acid_poll_event, MRB_ARGS_REQ( 1 ) );
}
```

- [ ] **Step 5: Translate window-relative coordinates in `v2/core/bindings/gfx_binding.c`**

Replace the file's full content with:

```c
#include "gfx_binding.h"
#include "../gfx/gfx.h"
#include "../kernel/kernel_app_context.h"

static mrb_value
acid_fill_rect( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int x, y, w, h, color;
    mrb_get_args( mrb, "iiiii", &x, &y, &w, &h, &color );
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    gfx_fill_rect( ctx->window_x + ( int ) x, ctx->window_y + ( int ) y,
                   ( int ) w, ( int ) h, ( unsigned int ) color );
    return mrb_nil_value();
}

void
acid_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_fill_rect",
                                 acid_fill_rect, MRB_ARGS_REQ( 5 ) );
}
```

- [ ] **Step 6: Rewrite `v2/core/vm_host/vm_host.h`**

```c
#ifndef ACID_VM_HOST_H
#define ACID_VM_HOST_H

#include "queue.h"

struct vm_host_params
{
    const char * script_path;
    QueueHandle_t queue;
    int window_x;
    int window_y;
    int window_w;
    int window_h;
};

/* pvParameters must point to a heap-allocated struct vm_host_params -- the
 * task frees it itself once the app's script has finished running. */
void vm_host_task( void * pvParameters );

#endif
```

- [ ] **Step 7: Rewrite `v2/core/vm_host/vm_host.c`**

```c
#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/error.h"

#include "vm_host.h"
#include "../bindings/gfx_binding.h"
#include "../bindings/event_binding.h"
#include "../gfx/gfx.h"
#include "../kernel/kernel_app_context.h"

/* Loaded into every app's VM before its own script, so AcidApp is always
 * defined -- the vendored mruby's default gembox (mrbgems/default.gembox)
 * has no require/require_relative gem, so the host loads framework code and
 * app code as two separate sequential mrb_load_detect_file_cxt calls into
 * the same VM instance instead. */
#define ACID_APP_LIB_PATH "v2/apps/lib/acid_app.rb"

static void
load_file_into_vm( mrb_state * mrb, mrb_ccontext * cxt, const char * path )
{
    FILE * fp = fopen( path, "r" );
    if( fp == NULL )
    {
        fprintf( stderr, "acid OS v2: could not open %s\n", path );
        return;
    }
    mrb_load_detect_file_cxt( mrb, fp, cxt );
    if( mrb->exc )
    {
        mrb_print_error( mrb );
        mrb->exc = NULL;
    }
    fclose( fp );
}

void
vm_host_task( void * pvParameters )
{
    struct vm_host_params * params = ( struct vm_host_params * ) pvParameters;

    struct kernel_app_context ctx;
    ctx.queue = params->queue;
    ctx.window_x = params->window_x;
    ctx.window_y = params->window_y;
    ctx.window_w = params->window_w;
    ctx.window_h = params->window_h;

    gfx_init();

    mrb_state * mrb = mrb_open();
    mrb->ud = &ctx;
    acid_bindings_register( mrb );
    acid_event_bindings_register( mrb );

    mrb_ccontext * cxt = mrb_ccontext_new( mrb );
    load_file_into_vm( mrb, cxt, ACID_APP_LIB_PATH );
    load_file_into_vm( mrb, cxt, params->script_path );
    mrb_ccontext_free( mrb, cxt );
    mrb_close( mrb );

    /* Bring-up's vm_host_task parked forever here (instead of returning)
     * purely to keep something alive checking hal_input_should_quit(),
     * since it was the only task in the system and vTaskStartScheduler()
     * never returns on its own. That's no longer this task's job: from
     * Task 4 onward, kernel_router_task is the one permanent task doing
     * that (and acid_poll_event's own hal_input_should_quit() check
     * already covers it before then, per Task 3's own verification step).
     * A script ending -- whether by closing normally or by faulting, with
     * mrb->exc already handled by load_file_into_vm above -- now just
     * cleanly frees this one app's resources instead of leaking a
     * forever-parked task; other apps and the router are unaffected
     * either way, which is the spec's actual fault-containment
     * requirement, not the parking loop itself. */
    free( params );
    vTaskDelete( NULL );
}
```

Note `gfx_init()` moved here from being called once globally in bring-up to being called once per spawned app task — harmless for now (it just calls `hal_display_init()`, which re-inits the same shared `lcd` object; Task 4 spawns multiple apps in quick succession at boot, so this runs a few times before the first frame is ever seen). Revisit only if a future phase's HAL implementation makes repeated init unsafe.

- [ ] **Step 8: Write `v2/apps/lib/acid_app.rb`**

```ruby
class AcidApp
  def on_create
  end

  def on_touch(x, y, pressed)
  end

  def on_destroy
  end

  def start
    on_create
    running = true
    while running
      ev = acid_poll_event(200)
      if ev == :close
        running = false
      elsif ev
        on_touch(ev[0], ev[1], ev[2])
      end
    end
    on_destroy
  end
end
```

- [ ] **Step 9: Write `v2/apps/demo_touch.rb`**

```ruby
class DemoTouchApp < AcidApp
  def on_create
    acid_fill_rect(0, 0, 320, 240, 0x000000)
  end

  def on_touch(x, y, pressed)
    acid_fill_rect(x - 5, y - 5, 10, 10, 0x00FF66) if pressed
  end
end

DemoTouchApp.new.start
```

- [ ] **Step 10: Replace `v2/sim/sim_main.c`'s single-app wiring with a manual, full-screen instance of the new host-loop shape**

This task does not yet have a real kernel window list, spawner, or router — Task 4 replaces everything in this step wholesale. For now, `sim_main.c` creates one queue and feeds it directly from a polling task, purely to prove the new `AcidApp`/event-binding/host-loop plumbing end to end before generalizing it to multiple windows.

Replace the file's full content with:

```c
#include <stdbool.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "../core/vm_host/vm_host.h"
#include "../core/kernel/kernel_event.h"
#include "../core/hal/hal_input.h"

void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
    fprintf( stderr, "acid OS v2 sim: assert failed at %s:%lu\n", pcFile, ulLine );
    for( ;; ) {}
}

static QueueHandle_t g_demo_queue;

static void
input_feed_task( void * pvParameters )
{
    ( void ) pvParameters;
    for( ;; )
    {
        int x, y;
        bool pressed;
        hal_input_poll_touch( &x, &y, &pressed );
        if( pressed )
        {
            struct kernel_event ev;
            ev.type = KERNEL_EVENT_TOUCH;
            ev.x = x;
            ev.y = y;
            ev.pressed = 1;
            xQueueSendToBack( g_demo_queue, &ev, 0 );
        }
        vTaskDelay( pdMS_TO_TICKS( 16 ) );
    }
}

void
sim_freertos_main( void )
{
    g_demo_queue = xQueueCreate( 8, sizeof( struct kernel_event ) );

    struct vm_host_params * params =
        ( struct vm_host_params * ) pvPortMalloc( sizeof( struct vm_host_params ) );
    params->script_path = "v2/apps/demo_touch.rb";
    params->queue = g_demo_queue;
    params->window_x = 0;
    params->window_y = 0;
    params->window_w = 320;
    params->window_h = 240;

    xTaskCreate( vm_host_task, "demo", 8192, params, tskIDLE_PRIORITY + 1, NULL );
    xTaskCreate( input_feed_task, "input_feed", 4096, NULL, tskIDLE_PRIORITY + 1, NULL );
    vTaskStartScheduler();
    for( ;; ) {}
}
```

- [ ] **Step 11: Update `v2/sim/CMakeLists.txt`'s source list**

In the `add_executable(acidos_sim ...)` block, add the two new source files (`kernel_window.c` is not needed by the build yet — Task 4 adds it to the executable when something actually calls it):

```cmake
add_executable(acidos_sim
    sim_main.c
    hal_display_sim.cpp
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/vm_host/vm_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/gfx_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/event_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/gfx/gfx.c
    ${LGFX_SOURCES}
)
```

(This replaces the existing three-entry `core/...` list with four entries — only `event_binding.c` is new here.)

- [ ] **Step 12: Build**

```bash
cd /home/norfolkh/os
rm -rf v2/sim/build
cmake -S v2/sim -B v2/sim/build
cmake --build v2/sim/build
```

Expected: builds cleanly.

- [ ] **Step 13: Run and verify touch reaches Ruby**

```bash
cd /home/norfolkh/os
xvfb-run -a ./v2/sim/build/acidos_sim &
SIM_PID=$!
sleep 1
# Synthesize a couple of clicks inside the window if xdotool (or an
# equivalent) is available; otherwise note that only build+run-without-crash
# was confirmed, not the visual result.
sleep 2
kill $SIM_PID 2>/dev/null
wait $SIM_PID 2>/dev/null
```

Expected: a black window; wherever you click, a small green (`0x00FF66`) square appears at that exact position, proving touch → kernel event → `xQueueReceive` → `Kernel.acid_poll_event` → `AcidApp#on_touch` → `acid_fill_rect` → screen all actually works. Closing the window exits the process cleanly (via `acid_poll_event`'s own `hal_input_should_quit()` check — confirm this by observing the process actually terminates within a couple of seconds of the window closing, not requiring a manual kill).

- [ ] **Step 14: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/kernel/kernel_app_context.h v2/core/kernel/kernel_event.h \
        v2/core/bindings/event_binding.h v2/core/bindings/event_binding.c \
        v2/core/bindings/gfx_binding.c v2/core/vm_host/vm_host.h v2/core/vm_host/vm_host.c \
        v2/apps/lib/acid_app.rb v2/apps/demo_touch.rb v2/sim/sim_main.c v2/sim/CMakeLists.txt
git commit -m "v2: generalized per-app host loop, blocking event-poll binding, AcidApp framework"
```

---

## Task 4: Multi-app spawning and real input routing (`core/kernel/kernel_spawn`, `core/kernel/kernel_router`)

**Files:**
- Create: `v2/core/kernel/kernel_layout.h`
- Create: `v2/core/kernel/kernel_spawn.h`
- Create: `v2/core/kernel/kernel_spawn.c`
- Create: `v2/core/kernel/kernel_router.h`
- Create: `v2/core/kernel/kernel_router.c`
- Modify: `v2/sim/sim_main.c` (replace Task 3's manual one-app wiring entirely)
- Modify: `v2/sim/CMakeLists.txt`
- Create: `v2/apps/demo_swatch.rb` (second demo app, so multi-window is real, not just structurally possible)

**Interfaces:**
- Consumes: `kernel_window_*` (Task 1), `vm_host_task`/`struct vm_host_params` (Task 3), `hal_input_poll_touch` (Task 2).
- Produces: `kernel_spawn_app(const char *script_path, int x, int y, int w, int h, int closable)` (Task 5 and Task 7 call this directly, unchanged), `kernel_router_task` (Task 7's boot sequence starts this once, unchanged), `KERNEL_TITLE_BAR_H`/`KERNEL_CLOSE_BTN_R`/`KERNEL_CLOSE_BTN_MARGIN` in `kernel_layout.h` (Task 5's close-button hit test and Task 6's chrome-drawing binding both read these — single source of truth so hit-testing and drawing can never drift apart).

- [ ] **Step 1: Write `v2/core/kernel/kernel_layout.h`**

```c
#ifndef ACID_KERNEL_LAYOUT_H
#define ACID_KERNEL_LAYOUT_H

/* Per-window chrome geometry, shared between kernel_router's hit-testing
 * (this task, and Task 5) and the chrome-drawing binding (Task 6) -- one
 * source of truth so a click always lands exactly where the chrome is
 * actually drawn. */
#define KERNEL_TITLE_BAR_H 16
#define KERNEL_CLOSE_BTN_R 5
#define KERNEL_CLOSE_BTN_MARGIN 8

/* The desktop's own top strip is a separate, larger concept (Task 7) --
 * defined here now so Task 5's close-button geometry and this one never
 * get confused with each other. */
#define KERNEL_DESKTOP_STRIP_H 20

#endif
```

- [ ] **Step 2: Write `v2/core/kernel/kernel_spawn.h`**

```c
#ifndef ACID_KERNEL_SPAWN_H
#define ACID_KERNEL_SPAWN_H

/* Spawns one app: a fresh FreeRTOS task running its own mruby VM against
 * script_path, with its own input queue, registered in the kernel window
 * list at (x, y, w, h). Returns the new task handle (cast to void*) on
 * success, or NULL if the queue or task couldn't be created. */
void * kernel_spawn_app( const char * script_path, int x, int y, int w, int h, int closable );

#endif
```

- [ ] **Step 3: Write `v2/core/kernel/kernel_spawn.c`**

```c
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "kernel_spawn.h"
#include "kernel_window.h"
#include "kernel_event.h"
#include "../vm_host/vm_host.h"

void *
kernel_spawn_app( const char * script_path, int x, int y, int w, int h, int closable )
{
    QueueHandle_t queue = xQueueCreate( 8, sizeof( struct kernel_event ) );
    if( queue == NULL )
    {
        return NULL;
    }

    struct vm_host_params * params =
        ( struct vm_host_params * ) pvPortMalloc( sizeof( struct vm_host_params ) );
    params->script_path = script_path;
    params->queue = queue;
    params->window_x = x;
    params->window_y = y;
    params->window_w = w;
    params->window_h = h;

    TaskHandle_t task = NULL;
    BaseType_t ok = xTaskCreate( vm_host_task, script_path, 8192, params,
                                  tskIDLE_PRIORITY + 1, &task );
    if( ok != pdPASS )
    {
        vQueueDelete( queue );
        vPortFree( params );
        return NULL;
    }

    kernel_window_register( ( void * ) task, ( void * ) queue, script_path,
                             x, y, w, h, closable );
    return ( void * ) task;
}
```

- [ ] **Step 4: Write `v2/core/kernel/kernel_router.h`**

```c
#ifndef ACID_KERNEL_ROUTER_H
#define ACID_KERNEL_ROUTER_H

void kernel_router_task( void * pvParameters );

#endif
```

- [ ] **Step 5: Write `v2/core/kernel/kernel_router.c`**

This is Task 4's version — it does not yet know about the close-button corner (Task 5) or the desktop strip (Task 7); it hit-tests, drags, and forwards ordinary touches only.

```c
#include <stdbool.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "kernel_router.h"
#include "kernel_window.h"
#include "kernel_event.h"
#include "kernel_layout.h"
#include "../hal/hal_input.h"

enum drag_mode
{
    DRAG_NONE = 0,
    DRAG_MOVE = 1
};

static enum drag_mode g_drag_mode = DRAG_NONE;
static void * g_drag_task = NULL;
static int g_drag_offset_x = 0;
static int g_drag_offset_y = 0;
static bool g_was_pressed = false;

static void
send_event( struct kernel_window * win, int type, int x, int y, int pressed )
{
    if( win == NULL || win->queue == NULL )
    {
        return;
    }
    struct kernel_event ev;
    ev.type = type;
    ev.x = x;
    ev.y = y;
    ev.pressed = pressed;
    xQueueSendToBack( ( QueueHandle_t ) win->queue, &ev, 0 );
}

static void
kernel_router_poll( void )
{
    int x, y;
    bool pressed;
    hal_input_poll_touch( &x, &y, &pressed );

    bool fresh_press = pressed && !g_was_pressed;
    bool fresh_release = !pressed && g_was_pressed;
    g_was_pressed = pressed;

    if( g_drag_mode == DRAG_MOVE )
    {
        struct kernel_window * win = kernel_window_by_task( g_drag_task );
        if( win == NULL || !pressed )
        {
            g_drag_mode = DRAG_NONE;
            g_drag_task = NULL;
        }
        else
        {
            win->x = x - g_drag_offset_x;
            win->y = y - g_drag_offset_y;
        }
        return;
    }

    if( fresh_press )
    {
        struct kernel_window * win = kernel_window_find_at( x, y );
        if( win == NULL )
        {
            return;
        }

        kernel_window_bring_to_front( win->task );

        int rel_x = x - win->x;
        int rel_y = y - win->y;

        if( rel_y < KERNEL_TITLE_BAR_H )
        {
            g_drag_mode = DRAG_MOVE;
            g_drag_task = win->task;
            g_drag_offset_x = rel_x;
            g_drag_offset_y = rel_y;
            return;
        }

        send_event( win, KERNEL_EVENT_TOUCH, rel_x, rel_y, 1 );
        return;
    }

    if( pressed )
    {
        struct kernel_window * win = kernel_window_find_at( x, y );
        if( win != NULL )
        {
            send_event( win, KERNEL_EVENT_TOUCH, x - win->x, y - win->y, 1 );
        }
        return;
    }

    if( fresh_release )
    {
        struct kernel_window * win = kernel_window_find_at( x, y );
        if( win != NULL )
        {
            send_event( win, KERNEL_EVENT_TOUCH, x - win->x, y - win->y, 0 );
        }
    }
}

void
kernel_router_task( void * pvParameters )
{
    ( void ) pvParameters;
    for( ;; )
    {
        if( hal_input_should_quit() )
        {
            exit( 0 );
        }
        kernel_router_poll();
        vTaskDelay( pdMS_TO_TICKS( 16 ) );
    }
}
```

- [ ] **Step 6: Write `v2/apps/demo_swatch.rb` (second demo app)**

```ruby
class DemoSwatchApp < AcidApp
  SWATCHES = [0x00FF66, 0xFF0066, 0x0066FF, 0xFFFFFF].freeze

  def on_create
    @index = 0
    acid_fill_rect(0, 0, 140, 100, 0x050607)
  end

  def on_touch(x, y, pressed)
    return unless pressed
    @index = (@index + 1) % SWATCHES.size
    acid_fill_rect(0, 0, 140, 100, SWATCHES[@index])
  end
end

DemoSwatchApp.new.start
```

(140x100 matches the window size this app is spawned at below — an `AcidApp` script draws in window-relative coordinates and, per this phase's YAGNI cut on resize, is always spawned at a fixed size it can simply know.)

- [ ] **Step 7: Replace `v2/sim/sim_main.c`'s manual wiring with real multi-app spawning**

Replace the file's full content with:

```c
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "../core/kernel/kernel_window.h"
#include "../core/kernel/kernel_spawn.h"
#include "../core/kernel/kernel_router.h"

void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
    fprintf( stderr, "acid OS v2 sim: assert failed at %s:%lu\n", pcFile, ulLine );
    for( ;; ) {}
}

void
sim_freertos_main( void )
{
    kernel_window_init();

    kernel_spawn_app( "v2/apps/demo_touch.rb", 10, 30, 140, 100, 1 );
    kernel_spawn_app( "v2/apps/demo_swatch.rb", 160, 70, 140, 100, 1 );

    xTaskCreate( kernel_router_task, "router", 4096, NULL, tskIDLE_PRIORITY + 2, NULL );
    vTaskStartScheduler();
    for( ;; ) {}
}
```

(Note `demo_touch.rb` is spawned at 140x100 now, not full-screen 320x240 as in Task 3 — its own `on_create`/`on_touch` calls in `v2/apps/demo_touch.rb` don't hardcode a size dependency, so no change to that file is needed; it will simply draw within its new, smaller window.)

- [ ] **Step 8: Update `v2/sim/CMakeLists.txt`'s source list**

```cmake
add_executable(acidos_sim
    sim_main.c
    hal_display_sim.cpp
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/vm_host/vm_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/gfx_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/event_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/gfx/gfx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_window.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_spawn.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_router.c
    ${LGFX_SOURCES}
)
```

- [ ] **Step 9: Build**

```bash
cd /home/norfolkh/os
rm -rf v2/sim/build
cmake -S v2/sim -B v2/sim/build
cmake --build v2/sim/build
```

Expected: builds cleanly.

- [ ] **Step 10: Run and verify real multi-window routing**

```bash
cd /home/norfolkh/os
xvfb-run -a ./v2/sim/build/acidos_sim &
SIM_PID=$!
sleep 2
kill $SIM_PID 2>/dev/null
wait $SIM_PID 2>/dev/null
```

Expected, confirmed either by synthesized clicks (if `xdotool` or equivalent is available) or by manual interactive observation if this is run somewhere with real display access: both windows are visible at their distinct positions from boot. Touching inside the left window (10,30)-(150,130) draws a small green square there and does not affect the right window; touching inside the right window (160,70)-(300,170) cycles its own fill color and does not affect the left window. Touching near the top of either window (its own top `KERNEL_TITLE_BAR_H` = 16 pixels) and dragging moves only that window — the other stays put and keeps responding to its own touches correctly afterward, including after the dragged window is moved to overlap it (the front-most one, most recently touched, should draw on top — verify by dragging one window to overlap the other and confirming which one's content is visible in the overlapping region).

- [ ] **Step 11: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/kernel/kernel_layout.h v2/core/kernel/kernel_spawn.h v2/core/kernel/kernel_spawn.c \
        v2/core/kernel/kernel_router.h v2/core/kernel/kernel_router.c \
        v2/apps/demo_swatch.rb v2/sim/sim_main.c v2/sim/CMakeLists.txt
git commit -m "v2: real multi-app spawning and hit-tested input routing with drag-to-move"
```

---

## Task 5: Close-button handling

**Files:**
- Modify: `v2/core/kernel/kernel_router.c`

**Interfaces:**
- Consumes: `KERNEL_TITLE_BAR_H`/`KERNEL_CLOSE_BTN_R`/`KERNEL_CLOSE_BTN_MARGIN` (Task 4's `kernel_layout.h`), `KERNEL_EVENT_CLOSE` (Task 3's `kernel_event.h`).
- Produces: nothing new for later tasks — `AcidApp#start` (Task 3) already handles a `:close` event correctly; this task is purely about the kernel-side trigger.

No changes to `vm_host.c`, `event_binding.c`, or `acid_app.rb` are needed here — the whole point of Task 3's design was that `:close` handling already exists end-to-end in the app's own event loop and host task teardown. This task only adds the *trigger*.

- [ ] **Step 1: Add close-button hit-testing to `kernel_router.c`'s `fresh_press` branch**

In `v2/core/kernel/kernel_router.c`, replace this block inside `kernel_router_poll`:

```c
        if( rel_y < KERNEL_TITLE_BAR_H )
        {
            g_drag_mode = DRAG_MOVE;
            g_drag_task = win->task;
            g_drag_offset_x = rel_x;
            g_drag_offset_y = rel_y;
            return;
        }

        send_event( win, KERNEL_EVENT_TOUCH, rel_x, rel_y, 1 );
        return;
```

with:

```c
        if( rel_y < KERNEL_TITLE_BAR_H )
        {
            if( win->closable )
            {
                int cx = win->w - KERNEL_CLOSE_BTN_MARGIN;
                int cy = KERNEL_TITLE_BAR_H / 2;
                int dx = rel_x - cx;
                int dy = rel_y - cy;
                int hit_r = KERNEL_CLOSE_BTN_R + 3; /* a little forgiveness for touch */
                if( ( dx * dx + dy * dy ) <= ( hit_r * hit_r ) )
                {
                    send_event( win, KERNEL_EVENT_CLOSE, 0, 0, 0 );
                    kernel_window_unregister( win->task );
                    return;
                }
            }

            g_drag_mode = DRAG_MOVE;
            g_drag_task = win->task;
            g_drag_offset_x = rel_x;
            g_drag_offset_y = rel_y;
            return;
        }

        send_event( win, KERNEL_EVENT_TOUCH, rel_x, rel_y, 1 );
        return;
```

(Unregistering the window from `kernel_window` *before* the app's own task actually finishes tearing down is deliberate, per the spec's Data flow step 5: it stops the closed window from receiving further input or blocking hit-testing immediately, while the app's own `mrb_close()`/`vTaskDelete(NULL)` happens on its own time inside its own task, not the router's.)

- [ ] **Step 2: Build**

```bash
cd /home/norfolkh/os
cmake --build v2/sim/build
```

Expected: builds cleanly (no new files, no CMakeLists.txt change needed).

- [ ] **Step 3: Run and verify close works cleanly**

```bash
cd /home/norfolkh/os
xvfb-run -a ./v2/sim/build/acidos_sim &
SIM_PID=$!
sleep 2
kill $SIM_PID 2>/dev/null
wait $SIM_PID 2>/dev/null
```

Expected (via synthesized clicks if available, or manual interactive observation otherwise): touching the top-right corner of either demo window's own top 16-pixel strip (within `KERNEL_CLOSE_BTN_R + 3` = 8 pixels of the point `(window_w - 8, 8)` in that window's own coordinates) makes that window disappear — no crash, no hang, process keeps running. The *other* window keeps responding correctly to touches and drag afterward. Touching the title-bar strip *away* from that corner still starts a drag, not a close (confirm dragging still works on whichever window you didn't close).

- [ ] **Step 4: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/kernel/kernel_router.c
git commit -m "v2: close-button hit-testing, cooperative app teardown via the existing :close path"
```

---

## Task 6: Real chrome — title bar, close button, RaveOS-v1 palette

**Files:**
- Create: `v2/core/kernel/kernel_theme.h`
- Create: `v2/core/bindings/chrome_binding.h`
- Create: `v2/core/bindings/chrome_binding.c`
- Modify: `v2/core/hal/hal_display.h`
- Modify: `v2/core/gfx/gfx.h`
- Modify: `v2/core/gfx/gfx.c`
- Modify: `v2/sim/hal_display_sim.cpp`
- Modify: `v2/hw/main/hal_display_hw.c`
- Modify: `v2/apps/lib/acid_app.rb`
- Modify: `v2/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `kernel_layout.h` (Task 4), `kernel_app_context.h` (Task 3).
- Produces: `Kernel.acid_draw_window_frame`/`Kernel.acid_clear_user_area` (Ruby-callable; Task 7's `desktop.rb` and every future app use `AcidApp#redraw`, which wraps both), `gfx_fill_circle`/`hal_display_fill_circle` (target-agnostic circle primitive, added the same way bring-up's own `fillRect` chain was structured).

All chrome geometry and color live in C, not Ruby — every pixel of a window's title bar and close button is drawn by one C binding reading the same `kernel_layout.h` constants `kernel_router.c` already hit-tests against, so drift between "where a click lands" and "where the chrome is drawn" is structurally impossible.

- [ ] **Step 1: Write `v2/core/kernel/kernel_theme.h`**

```c
#ifndef ACID_KERNEL_THEME_H
#define ACID_KERNEL_THEME_H

/* RaveOS v1's own documented palette (docs/BUILD_LOG.md), reused verbatim so
 * acid OS v2 visually continues v1's identity. These are already correct
 * 24-bit RGB hex values (e.g. #00ff66) -- unlike bring-up's hello.rb, which
 * used RGB565-style literals (0xF800/0x07E0) that this codebase's
 * gfx_fill_rect/hal_display_fill_rect chain (verified during bring-up's
 * final review to treat a bare color argument as RGB888) rendered
 * incorrectly. These values need no such translation. */
#define THEME_BG      0x050607u  /* --bg: desktop/page background */
#define THEME_HARD    0x00FF66u  /* --hard: pure acid green -- accent, borders, pressed */
#define THEME_PANEL   0x0B1712u  /* --panel: window body, brightened for a flat renderer */
#define THEME_TEXT    0xD4E6DBu  /* --text */
#define THEME_MUTED   0x9DAAA3u  /* --muted */

#endif
```

- [ ] **Step 2: Add `hal_display_fill_circle` to `v2/core/hal/hal_display.h`**

```c
#ifndef ACID_HAL_DISPLAY_H
#define ACID_HAL_DISPLAY_H

void hal_display_init( void );
void hal_display_fill_rect( int x, int y, int w, int h, unsigned int color );
void hal_display_fill_circle( int x, int y, int r, unsigned int color );

#endif
```

- [ ] **Step 3: Implement it in `v2/sim/hal_display_sim.cpp`**

Add to the end of the file:

```cpp
extern "C" void hal_display_fill_circle( int x, int y, int r, unsigned int color )
{
    lcd.fillCircle( x, y, r, color );
}
```

(Verified real API: `LGFXBase::fillCircle(int32_t x, int32_t y, int32_t r, const T& color)` — same templated color-setting path as the already-used `fillRect`.)

- [ ] **Step 4: Stub it in `v2/hw/main/hal_display_hw.c`**

Add to the end of the file:

```c
void
hal_display_fill_circle( int x, int y, int r, unsigned int color )
{
    ESP_LOGI( TAG, "hal_display_fill_circle(%d, %d, %d, 0x%06x): stub, not drawn", x, y, r, color );
}
```

- [ ] **Step 5: Add `gfx_fill_circle` to `v2/core/gfx/gfx.h`**

```c
#ifndef ACID_GFX_H
#define ACID_GFX_H

void gfx_init( void );
void gfx_fill_rect( int x, int y, int w, int h, unsigned int color );
void gfx_fill_circle( int x, int y, int r, unsigned int color );

#endif
```

- [ ] **Step 6: Implement it in `v2/core/gfx/gfx.c`**

Add to the end of the file:

```c
void
gfx_fill_circle( int x, int y, int r, unsigned int color )
{
    hal_display_fill_circle( x, y, r, color );
}
```

- [ ] **Step 7: Write `v2/core/bindings/chrome_binding.h`**

```c
#ifndef ACID_CHROME_BINDING_H
#define ACID_CHROME_BINDING_H

#include "mruby.h"

void acid_chrome_bindings_register( mrb_state * mrb );

#endif
```

- [ ] **Step 8: Write `v2/core/bindings/chrome_binding.c`**

```c
#include "chrome_binding.h"
#include "../gfx/gfx.h"
#include "../kernel/kernel_app_context.h"
#include "../kernel/kernel_layout.h"
#include "../kernel/kernel_theme.h"

static mrb_value
acid_draw_window_frame( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;

    gfx_fill_rect( ctx->window_x, ctx->window_y, ctx->window_w, KERNEL_TITLE_BAR_H, THEME_PANEL );

    int cx = ctx->window_x + ctx->window_w - KERNEL_CLOSE_BTN_MARGIN;
    int cy = ctx->window_y + ( KERNEL_TITLE_BAR_H / 2 );
    gfx_fill_circle( cx, cy, KERNEL_CLOSE_BTN_R, THEME_HARD );

    return mrb_nil_value();
}

static mrb_value
acid_clear_user_area( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    gfx_fill_rect( ctx->window_x, ctx->window_y + KERNEL_TITLE_BAR_H,
                   ctx->window_w, ctx->window_h - KERNEL_TITLE_BAR_H, THEME_BG );
    return mrb_nil_value();
}

void
acid_chrome_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_draw_window_frame",
                                 acid_draw_window_frame, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_clear_user_area",
                                 acid_clear_user_area, MRB_ARGS_NONE() );
}
```

Note both functions call `gfx_fill_rect`/`gfx_fill_circle` with *absolute* coordinates (`ctx->window_x + ...`) directly — unlike `gfx_binding.c`'s `acid_fill_rect`, which translates a *script-supplied* window-relative coordinate. This binding computes the app's own chrome position itself, so there is no script-supplied coordinate to translate.

- [ ] **Step 9: Register the new bindings in `v2/core/vm_host/vm_host.c`**

Add the include and the registration call:

```c
#include "../bindings/chrome_binding.h"
```

(next to the existing `#include "../bindings/event_binding.h"`), and in `vm_host_task`, add:

```c
    acid_chrome_bindings_register( mrb );
```

right after the existing `acid_event_bindings_register( mrb );` line.

- [ ] **Step 10: Update `AcidApp` to draw real chrome**

Replace `v2/apps/lib/acid_app.rb`'s full content with:

```ruby
class AcidApp
  def on_create
  end

  def on_touch(x, y, pressed)
  end

  def on_destroy
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame
  end

  def start
    on_create
    redraw
    running = true
    while running
      ev = acid_poll_event(200)
      if ev == :close
        running = false
      elsif ev
        on_touch(ev[0], ev[1], ev[2])
      end
    end
    on_destroy
  end
end
```

`redraw` clears the user area to the theme background and (re)draws the frame on top — every app now gets both automatically once per `start`, without hand-painting anything.

`demo_touch.rb` and `demo_swatch.rb`'s own `on_create`/`on_touch` methods already draw within their own user-area coordinates (both start at y=0 in their own window-relative space, which after Task 3's translation lands at that window's absolute `(window_x, window_y)` — but that's now *inside* the title bar's 16-pixel strip, since neither script accounts for it yet). Update both:

In `v2/apps/demo_touch.rb`, change `on_create`'s draw call from `acid_fill_rect(0, 0, 320, 240, 0x000000)` to nothing at all — delete that line entirely (`redraw`, called by the base class before `on_create` returns control to the loop, already clears the user area; drawing it again here just duplicates that). The full file becomes:

```ruby
class DemoTouchApp < AcidApp
  def on_touch(x, y, pressed)
    acid_fill_rect(x - 5, y - 5, 10, 10, 0x00FF66) if pressed
  end
end

DemoTouchApp.new.start
```

In `v2/apps/demo_swatch.rb`, remove the `acid_fill_rect(0, 0, 140, 100, 0x050607)` line from `on_create` for the same reason (`redraw` already did it), and change `on_touch`'s draw call to leave room below the title bar — the swatch should fill the user area, not the window's full rect. The full file becomes:

```ruby
class DemoSwatchApp < AcidApp
  SWATCHES = [0x00FF66, 0xFF0066, 0x0066FF, 0xFFFFFF].freeze

  def on_create
    @index = 0
  end

  def on_touch(x, y, pressed)
    return unless pressed
    @index = (@index + 1) % SWATCHES.size
    acid_fill_rect(0, 16, 140, 100 - 16, SWATCHES[@index])
  end
end

DemoSwatchApp.new.start
```

(The `16` here is `KERNEL_TITLE_BAR_H` — a Ruby script has no way to read a C `#define`, so this is a plain literal. If a future phase adds more chrome-aware apps, revisit exposing this via `acid_window_size`-style binding rather than duplicating the literal per script; not worth it for two apps.)

`demo_touch.rb`'s `on_touch` already only ever draws a 10x10 square wherever it's touched — since `kernel_router.c` never forwards a touch inside the title-bar strip to the app at all (Task 4/5), `y` here is always already `>= KERNEL_TITLE_BAR_H` in window-relative terms, so no clamping is needed there.

- [ ] **Step 11: Update `v2/sim/CMakeLists.txt`'s source list**

```cmake
add_executable(acidos_sim
    sim_main.c
    hal_display_sim.cpp
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/vm_host/vm_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/gfx_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/event_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/chrome_binding.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/gfx/gfx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_window.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_spawn.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_router.c
    ${LGFX_SOURCES}
)
```

- [ ] **Step 12: Build**

```bash
cd /home/norfolkh/os
rm -rf v2/sim/build
cmake -S v2/sim -B v2/sim/build
cmake --build v2/sim/build
```

Expected: builds cleanly.

- [ ] **Step 13: Run and verify real chrome renders correctly**

```bash
cd /home/norfolkh/os
xvfb-run -a ./v2/sim/build/acidos_sim &
SIM_PID=$!
sleep 2
kill $SIM_PID 2>/dev/null
wait $SIM_PID 2>/dev/null
```

Expected: both windows now show a `--panel`-colored (`0x0B1712`) title-bar strip across their own top 16 pixels, with a solid acid-green (`0x00FF66`) circle near its top-right corner (the close button), and a near-black (`0x050607`) user area below it — visually distinct from a flat, chrome-less rectangle. Touching/dragging/closing behavior from Tasks 4-5 is unchanged (verify closing still works, since the close button's *drawn* position now visually matches where Task 5 already hit-tests it).

- [ ] **Step 14: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/kernel/kernel_theme.h v2/core/bindings/chrome_binding.h v2/core/bindings/chrome_binding.c \
        v2/core/hal/hal_display.h v2/core/gfx/gfx.h v2/core/gfx/gfx.c \
        v2/sim/hal_display_sim.cpp v2/hw/main/hal_display_hw.c \
        v2/apps/lib/acid_app.rb v2/apps/demo_touch.rb v2/apps/demo_swatch.rb v2/sim/CMakeLists.txt \
        v2/core/vm_host/vm_host.c
git commit -m "v2: real window chrome (title bar, close button) in RaveOS v1's acid-green palette"
```

---

## Task 7: Desktop app and final boot assembly

**Files:**
- Create: `v2/apps/desktop.rb`
- Modify: `v2/core/kernel/kernel_router.h`
- Modify: `v2/core/kernel/kernel_router.c`
- Modify: `v2/core/bindings/chrome_binding.h`
- Modify: `v2/core/bindings/chrome_binding.c`
- Modify: `v2/sim/sim_main.c`
- Modify: `v2/hw/main/CMakeLists.txt`
- Modify: `v2/hw/main/app_main.c`

**Interfaces:**
- Consumes: everything from Tasks 1-6.
- Produces: the phase's final boot sequence — nothing further builds on this within this plan; this is the plan's last task.

- [ ] **Step 1: Add a desktop-strip-drawing binding to `v2/core/bindings/chrome_binding.h`**

```c
#ifndef ACID_CHROME_BINDING_H
#define ACID_CHROME_BINDING_H

#include "mruby.h"

void acid_chrome_bindings_register( mrb_state * mrb );

#endif
```

(Unchanged — `acid_draw_desktop_strip` is registered by the same `acid_chrome_bindings_register`, no new public entry point needed.)

- [ ] **Step 2: Implement it in `v2/core/bindings/chrome_binding.c`**

Add this function above `acid_chrome_bindings_register`:

```c
static mrb_value
acid_draw_desktop_strip( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    gfx_fill_rect( ctx->window_x, ctx->window_y, ctx->window_w, ctx->window_h, THEME_PANEL );
    return mrb_nil_value();
}
```

and register it inside `acid_chrome_bindings_register`:

```c
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_draw_desktop_strip",
                                 acid_draw_desktop_strip, MRB_ARGS_NONE() );
```

- [ ] **Step 3: Write `v2/apps/desktop.rb`**

```ruby
class DesktopApp < AcidApp
  def on_create
    acid_draw_desktop_strip
  end
end

DesktopApp.new.start
```

(Desktop is spawned with `closable = 0`, so it never reaches the close-button hit-test path in `kernel_router.c` even without any special-casing there. It draws once at boot and otherwise does nothing this phase — matching the spec's "minimal desktop app... no taskbar, no launcher, no dropdown menus yet" decision.)

- [ ] **Step 4: Add the desktop-strip routing special case to `kernel_router`**

In `v2/core/kernel/kernel_router.h`, add a setter:

```c
#ifndef ACID_KERNEL_ROUTER_H
#define ACID_KERNEL_ROUTER_H

void kernel_router_task( void * pvParameters );

/* Call once at boot, right after spawning the desktop app, so the router
 * knows which window's queue owns the top strip unconditionally (see
 * kernel_router_poll). Passing NULL disables the special case. */
void kernel_router_set_desktop_task( void * task );

#endif
```

In `v2/core/kernel/kernel_router.c`, add the static variable near the other `g_*` statics:

```c
static void * g_desktop_task = NULL;
```

Add the setter function (anywhere after the includes, before `kernel_router_task`):

```c
void
kernel_router_set_desktop_task( void * task )
{
    g_desktop_task = task;
}
```

And add the strip special case as the very first check inside `kernel_router_poll`, before the existing `if (g_drag_mode == DRAG_MOVE)` block:

```c
static void
kernel_router_poll( void )
{
    int x, y;
    bool pressed;
    hal_input_poll_touch( &x, &y, &pressed );

    bool fresh_press = pressed && !g_was_pressed;
    bool fresh_release = !pressed && g_was_pressed;
    g_was_pressed = pressed;

    if( g_desktop_task != NULL && g_drag_mode == DRAG_NONE && y < KERNEL_DESKTOP_STRIP_H )
    {
        struct kernel_window * desktop = kernel_window_by_task( g_desktop_task );
        if( desktop != NULL && ( fresh_press || pressed || fresh_release ) )
        {
            /* Window-relative, same convention every other window's touch
             * event already follows (Task 4) -- numerically a no-op today
             * since the desktop sits at (0,0), but this is the correct,
             * consistent form for whoever gives desktop.rb a real on_touch
             * later, not a coincidence to leave in place. */
            send_event( desktop, KERNEL_EVENT_TOUCH, x - desktop->x, y - desktop->y, pressed ? 1 : 0 );
        }
        return;
    }

    if( g_drag_mode == DRAG_MOVE )
    {
```

(The `g_drag_mode == DRAG_NONE` guard matters: without it, dragging a window whose *current position* happens to be under the strip — after being dragged there — would get its ongoing drag hijacked by this check on every subsequent poll. Checking the strip only when nothing is already being dragged preserves an in-progress drag even if it moves through that region, while still giving the desktop first claim on any *new* press that starts there.)

- [ ] **Step 5: Wire the desktop and both demo apps into `v2/sim/sim_main.c`'s boot sequence**

Replace the file's full content with:

```c
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "../core/kernel/kernel_window.h"
#include "../core/kernel/kernel_spawn.h"
#include "../core/kernel/kernel_router.h"

void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
    fprintf( stderr, "acid OS v2 sim: assert failed at %s:%lu\n", pcFile, ulLine );
    for( ;; ) {}
}

void
sim_freertos_main( void )
{
    kernel_window_init();

    void * desktop_task = kernel_spawn_app( "v2/apps/desktop.rb", 0, 0, 320, 20, 0 );
    kernel_router_set_desktop_task( desktop_task );

    kernel_spawn_app( "v2/apps/demo_touch.rb", 10, 30, 140, 100, 1 );
    kernel_spawn_app( "v2/apps/demo_swatch.rb", 160, 70, 140, 100, 1 );

    xTaskCreate( kernel_router_task, "router", 4096, NULL, tskIDLE_PRIORITY + 2, NULL );
    vTaskStartScheduler();
    for( ;; ) {}
}
```

- [ ] **Step 6: Build**

```bash
cd /home/norfolkh/os
rm -rf v2/sim/build
cmake -S v2/sim -B v2/sim/build
cmake --build v2/sim/build
```

Expected: builds cleanly.

- [ ] **Step 7: Run and verify the full phase-2 definition of done**

```bash
cd /home/norfolkh/os
xvfb-run -a ./v2/sim/build/acidos_sim &
SIM_PID=$!
sleep 2
kill $SIM_PID 2>/dev/null
wait $SIM_PID 2>/dev/null
```

Expected, per the spec's "Testing / definition of done" section — confirm each of these (via synthesized clicks if available, or manual interactive observation otherwise):

1. Three windows visible at boot: the desktop's `--panel`-colored strip across the full top 20 pixels, and the two demo windows with real title-bar/close-button chrome at their spawn positions.
2. Dragging either demo window by its title bar moves only that window; the other is unaffected and keeps responding to its own touches afterward.
3. Touching either demo window's close button removes it cleanly (no crash, no hang); the remaining window (and the desktop) keep working.
4. Touching inside a demo window's user area reaches that specific app and only that app — `demo_touch.rb` draws a green square at the exact touch position, `demo_swatch.rb` cycles its fill color — with correct window-relative-turned-absolute coordinates.
5. Touching the desktop's own top strip does not fall through to whatever window happens to be underneath, even after dragging a demo window's title bar up so it visually overlaps that strip — confirm this specific case, since it is the one behavior this task's own code (`kernel_router_set_desktop_task`'s special case) exists to guarantee.

- [ ] **Step 8: Extend `v2/hw/main/CMakeLists.txt` with all of this phase's new sources**

Replace the file's full content with:

```cmake
idf_component_register(
    SRCS
        app_main.c
        hal_display_hw.c
        hal_input_hw.c
        ../../core/vm_host/vm_host.c
        ../../core/bindings/gfx_binding.c
        ../../core/bindings/event_binding.c
        ../../core/bindings/chrome_binding.c
        ../../core/gfx/gfx.c
        ../../core/kernel/kernel_window.c
        ../../core/kernel/kernel_spawn.c
        ../../core/kernel/kernel_router.c
    INCLUDE_DIRS
        ../../core/vm_host
        ../../core/hal
        ../../core/gfx
        ../../core/bindings
        ../../core/kernel
        freertos_compat
    REQUIRES mruby_component
)
```

- [ ] **Step 9: Update `v2/hw/main/app_main.c` to boot the same way `sim` does**

Replace the file's full content with:

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../../core/kernel/kernel_window.h"
#include "../../core/kernel/kernel_spawn.h"
#include "../../core/kernel/kernel_router.h"

void
app_main( void )
{
    kernel_window_init();

    void * desktop_task = kernel_spawn_app( "v2/apps/desktop.rb", 0, 0, 320, 20, 0 );
    kernel_router_set_desktop_task( desktop_task );

    kernel_spawn_app( "v2/apps/demo_touch.rb", 10, 30, 140, 100, 1 );
    kernel_spawn_app( "v2/apps/demo_swatch.rb", 160, 70, 140, 100, 1 );

    xTaskCreate( kernel_router_task, "router", 4096, NULL, 5, NULL );
}
```

(Per this plan's Global Constraints, `idf.py build` itself cannot be run on this machine — no ESP-IDF install exists here, the same pre-existing gap bring-up documented. Do not attempt to install it. If whoever executes this task has ESP-IDF available, run `cd v2/hw && idf.py set-target esp32p4 && idf.py build` and report the real result; otherwise, do the same kind of careful static check bring-up's own Task 6 did: confirm every `../../core/...` path in the `CMakeLists.txt` above actually resolves against the real directory tree, and confirm `hal_input_hw.c`/`hal_display_hw.c`'s stub signatures still match `core/hal/hal_input.h`/`core/hal/hal_display.h` exactly now that both have grown a new function this phase.)

- [ ] **Step 10: Commit**

```bash
cd /home/norfolkh/os
git add v2/apps/desktop.rb v2/core/kernel/kernel_router.h v2/core/kernel/kernel_router.c \
        v2/core/bindings/chrome_binding.c v2/sim/sim_main.c \
        v2/hw/main/CMakeLists.txt v2/hw/main/app_main.c
git commit -m "v2: desktop app, desktop-strip routing, full three-window boot sequence"
```

---

## Definition of done (per spec)

- `sim`: kernel spawns a desktop app plus two demo windows at boot, all visible with correct chrome and z-order; dragging moves only the grabbed window; the close button cleanly removes a window without affecting others; touches inside a window's body reach that specific app with correct coordinates; the desktop's own top strip always wins hit-testing in its own row range, even when a dragged window overlaps it.
- `hw`: all of this phase's new source files are added to `v2/hw/main/CMakeLists.txt` and `app_main.c` boots the same way `sim` does. `idf.py build` succeeding remains unverified on this machine (no ESP-IDF installed) — the same carried-forward, plan-anticipated gap bring-up already documented, not a new one this phase introduces.
