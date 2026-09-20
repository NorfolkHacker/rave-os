# Terminal Easter Eggs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Typing `dave`, `joe` or `maximbady` in the acid OS v2 terminal flies a pixel
sprite across the whole 640x360 screen, over every window, with no output to the
scrollback.

**Architecture:** A single kernel-owned offscreen canvas ("the overlay") is composited
last, with magenta (`ACID_OVERLAY_KEY`) treated as transparent, so anything the overlay
leaves that colour shows whatever is underneath. It is not a window: no task, no mruby
VM, no taskbar entry, no hit-testing. The terminal draws into it from its own event loop
through four new `acid_overlay_*` bindings, using two new Ruby libraries — `AcidSprite`
(pixel picture-strings) and `AcidEggs` (the three animations).

**Tech Stack:** C99 (FreeRTOS + LovyanGFX), mruby 3.x, CMake for the sim, ESP-IDF for
hw.

**Spec:** `docs/superpowers/specs/2026-09-20-acid-os-v2-terminal-easter-eggs-design.md`

## Global Constraints

- **Repo root is `/home/norfolkh/os`.** All paths below are relative to it. The OS lives
  under `v2/`.
- **Build and run the sim:** `cmake -S v2/sim -B v2/sim/build && cmake --build v2/sim/build && ./v2/sim/build/acidos_sim`
  (first build only: `git submodule update --init --recursive`).
- **`hw` is compile-verified only, never run.** There is no ESP-IDF in this environment.
  Every C source change must be mirrored into `v2/hw/main/CMakeLists.txt` where it adds a
  file, and `v2/hw/main/hal_display_hw.c` gets stub implementations matching the sim's,
  exactly as its existing stubs do.
- **Screen is 640x360** (`KERNEL_SCREEN_W` / `KERNEL_SCREEN_H`, `v2/core/kernel/kernel_layout.h`).
  Never hardcode those numbers in C; include the header.
- **C style in this codebase:** return type on its own line above the function name,
  Allman braces, spaces inside parentheses — `void kernel_overlay_close( void )`,
  `if( g_canvas == NULL )`. Comments are long and explain *why*, often citing the bug or
  decision behind the code. Match that; it is the house style, not decoration.
- **Ruby style:** mruby with no `require` — files are concatenated into a VM by
  `vm_host.c`. No `Struct`, no `Comparable`, no stdlib beyond what the vendored build
  has. `while` loops over `each_with_index`, matching the existing apps.
- **Colours are 24-bit RGB888 literals** (`0x00FF66`), per `kernel_theme.h`.
- **`ACID_OVERLAY_KEY` is `0xFF00FF`.** No sprite or font colour anywhere in this plan
  may equal it.
- **Commit messages** start `v2: ` and end with the line
  `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>` (blank line
  before it).

---

### Task 1: The overlay's lifetime (`kernel_overlay`)

The overlay's open/close/canvas bookkeeping, with no drawing and no compositor wiring
yet. Pure logic, so it gets a real standalone unit test with the gfx layer stubbed out.

**Files:**
- Create: `v2/core/kernel/kernel_overlay.h`
- Create: `v2/core/kernel/kernel_overlay.c`
- Test: `v2/core/kernel/test_kernel_overlay.c`
- Modify: `v2/core/kernel/kernel_theme.h` (append `ACID_OVERLAY_KEY` before `#endif`)
- Modify: `v2/sim/CMakeLists.txt:~75` (the `add_executable(acidos_sim ...)` source list,
  after the `kernel_router.c` line)
- Modify: `v2/hw/main/CMakeLists.txt:~24` (the `SRCS` list, after the `kernel_router.c` line)

**Interfaces:**
- Consumes: `gfx_create_canvas`, `gfx_fill_rect`, `gfx_mark_dirty` (`v2/core/gfx/gfx.h`);
  `KERNEL_SCREEN_W` / `KERNEL_SCREEN_H` (`v2/core/kernel/kernel_layout.h`).
- Produces: `int kernel_overlay_open( void )` (1 on success, 0 if the canvas could not be
  allocated), `void kernel_overlay_close( void )`, `int kernel_overlay_is_open( void )`,
  `void * kernel_overlay_canvas( void )` (NULL when closed), and the macro
  `ACID_OVERLAY_KEY`.

- [ ] **Step 1: Write the failing test**

Create `v2/core/kernel/test_kernel_overlay.c`. Note it declares the three gfx functions
itself rather than including `gfx.h` — that keeps FreeRTOS headers out of the test
binary, and the stubs below are what `kernel_overlay.c` links against.

```c
#include <assert.h>
#include <stdio.h>

#include "kernel_overlay.h"
#include "kernel_theme.h"
#include "kernel_layout.h"

/* Declared here rather than by including gfx.h: gfx.h pulls in FreeRTOS.h
 * and semphr.h for its lock accessor, none of which this test needs. These
 * three are the only gfx entry points kernel_overlay.c actually calls, and
 * these definitions are what it links against. */
void * gfx_create_canvas( int w, int h );
void gfx_fill_rect( void * target, int x, int y, int w, int h, unsigned int color );
void gfx_mark_dirty( void );

static int g_create_calls = 0;
static int g_dirty_calls = 0;
static int g_last_fill_w = 0;
static int g_last_fill_h = 0;
static unsigned int g_last_fill_color = 0;
static int g_fill_calls = 0;
static char g_fake_canvas[ 4 ];

void *
gfx_create_canvas( int w, int h )
{
    g_create_calls++;
    assert( w == KERNEL_SCREEN_W );
    assert( h == KERNEL_SCREEN_H );
    return g_fake_canvas;
}

void
gfx_fill_rect( void * target, int x, int y, int w, int h, unsigned int color )
{
    ( void ) target;
    ( void ) x;
    ( void ) y;
    g_fill_calls++;
    g_last_fill_w = w;
    g_last_fill_h = h;
    g_last_fill_color = color;
}

void
gfx_mark_dirty( void )
{
    g_dirty_calls++;
}

int
main( void )
{
    /* Closed before anything opens it: nothing to composite, no canvas
     * allocated. A user who never types an easter egg never pays the
     * overlay's ~450KB. */
    assert( kernel_overlay_is_open() == 0 );
    assert( kernel_overlay_canvas() == NULL );
    assert( g_create_calls == 0 );

    /* Opening allocates once, clears the whole canvas to the key colour
     * (so the very first composited frame shows nothing rather than the
     * black createSprite zero-fills it with), and marks the screen dirty
     * -- the open itself draws nothing, so nothing else would. */
    assert( kernel_overlay_open() == 1 );
    assert( kernel_overlay_is_open() == 1 );
    assert( kernel_overlay_canvas() == g_fake_canvas );
    assert( g_create_calls == 1 );
    assert( g_fill_calls == 1 );
    assert( g_last_fill_w == KERNEL_SCREEN_W );
    assert( g_last_fill_h == KERNEL_SCREEN_H );
    assert( g_last_fill_color == ACID_OVERLAY_KEY );
    assert( g_dirty_calls == 1 );

    /* Opening again reuses the same canvas -- never a second allocation. */
    assert( kernel_overlay_open() == 1 );
    assert( g_create_calls == 1 );
    assert( kernel_overlay_canvas() == g_fake_canvas );

    /* Closing hides it from the compositor but deliberately does NOT free
     * the canvas: the task that closes it is an app task, while the router
     * task may be mid-blit on those same pixels. See kernel_overlay.c's own
     * comment. */
    kernel_overlay_close();
    assert( kernel_overlay_is_open() == 0 );
    assert( kernel_overlay_canvas() == NULL );

    /* Reopening after a close reuses the surviving canvas and re-clears it,
     * so the previous egg's last frame can't flash up. */
    int fills_before = g_fill_calls;
    assert( kernel_overlay_open() == 1 );
    assert( g_create_calls == 1 );
    assert( g_fill_calls == fills_before + 1 );
    kernel_overlay_close();

    printf( "test_kernel_overlay: all assertions passed\n" );
    return 0;
}
```

The allocation-failure branch is deliberately absent here: once this process has cached a
canvas, that branch is unreachable, so it gets its own tiny binary in Step 2 rather than
contorting this one with a failure toggle.

- [ ] **Step 2: Write the failing allocation-failure test**

Create `v2/core/kernel/test_kernel_overlay_alloc_fail.c`:

```c
#include <assert.h>
#include <stdio.h>

#include "kernel_overlay.h"

void * gfx_create_canvas( int w, int h );
void gfx_fill_rect( void * target, int x, int y, int w, int h, unsigned int color );
void gfx_mark_dirty( void );

void *
gfx_create_canvas( int w, int h )
{
    ( void ) w;
    ( void ) h;
    return NULL;
}

void
gfx_fill_rect( void * target, int x, int y, int w, int h, unsigned int color )
{
    ( void ) target; ( void ) x; ( void ) y; ( void ) w; ( void ) h; ( void ) color;
    /* Reaching here would mean kernel_overlay_open() tried to clear a
     * canvas it never got. */
    assert( 0 );
}

void
gfx_mark_dirty( void )
{
}

int
main( void )
{
    assert( kernel_overlay_open() == 0 );
    assert( kernel_overlay_is_open() == 0 );
    assert( kernel_overlay_canvas() == NULL );
    printf( "test_kernel_overlay_alloc_fail: all assertions passed\n" );
    return 0;
}
```

- [ ] **Step 3: Run both tests to verify they fail**

```bash
cd /home/norfolkh/os/v2
gcc -o /tmp/t_overlay core/kernel/test_kernel_overlay.c core/kernel/kernel_overlay.c \
  -I core/gfx -I core/kernel -I sim \
  -I components/freertos-kernel/include \
  -I components/freertos-kernel/portable/ThirdParty/GCC/Posix \
  -I components/freertos-kernel/portable/ThirdParty/GCC/Posix/utils
```

Expected: FAIL — `core/kernel/kernel_overlay.c: No such file or directory` and
`kernel_overlay.h: No such file or directory`.

- [ ] **Step 4: Add the key colour**

Append to `v2/core/kernel/kernel_theme.h`, immediately before its closing `#endif`:

```c
/* The overlay's transparent colour (see kernel_overlay.h). Every pixel an
 * overlay leaves this colour shows whatever is underneath it instead --
 * this codebase has no alpha compositing, and gfx_blit_canvas is a plain
 * opaque copy, so a colour key is how a full-screen effect draws over the
 * wallpaper and other windows without erasing them. Magenta because
 * nothing in the five theme colours above, nor in the wallpaper's own neon
 * palette (gfx/wallpaper_data.h), quantises onto it in RGB565 -- a sprite
 * colour that did would punch holes in itself. */
#define ACID_OVERLAY_KEY 0xFF00FFu
```

- [ ] **Step 5: Write the header**

Create `v2/core/kernel/kernel_overlay.h`:

```c
#ifndef ACID_KERNEL_OVERLAY_H
#define ACID_KERNEL_OVERLAY_H

/* One screen-sized canvas, owned by the kernel rather than by any window,
 * composited last (kernel_router.c's kernel_router_composite_frame) with
 * ACID_OVERLAY_KEY treated as transparent. It is how an app draws over the
 * WHOLE screen -- across the wallpaper, the desktop strip and every open
 * window -- which nothing else here can do: every app draws only into its
 * own window's canvas in window-relative coordinates, and each of those is
 * blitted opaquely.
 *
 * Deliberately not a window. It owns no task and no mruby VM, is never
 * hit-tested (so clicks land on whatever is really underneath), never takes
 * keyboard focus, and never appears in the taskbar -- none of which needs
 * any code here, because a window is the only thing that gets any of it.
 * The terminal's easter eggs (apps/lib/acid_eggs.rb) are the first caller;
 * spawning an app for them would have cost an 8KB task stack, a whole
 * mrb_open() and six Ruby files compiled from source (kernel_spawn.c:75,
 * vm_host.c:260-276) to fly a sprite for three seconds.
 *
 * Not thread-safe against itself: one caller at a time, which is what the
 * single acid_overlay_* binding surface gives. Drawing into the canvas from
 * an app task while the router task blits it is the same benign race every
 * window canvas already has -- the blit is a plain memory copy, so the
 * worst case is one frame showing a sprite mid-move. */

/* Opens the overlay, allocating its canvas on first use and clearing it to
 * ACID_OVERLAY_KEY. Returns 1, or 0 if the canvas could not be allocated
 * (in which case the overlay stays closed and the caller should simply do
 * nothing -- an easter egg that doesn't fire is not an error worth
 * reporting to a user). */
int kernel_overlay_open( void );

/* Hides the overlay from the compositor. Deliberately does NOT free the
 * canvas -- see kernel_overlay.c. */
void kernel_overlay_close( void );

int kernel_overlay_is_open( void );

/* The canvas to draw into, or NULL when closed. */
void * kernel_overlay_canvas( void );

#endif
```

- [ ] **Step 6: Write the implementation**

Create `v2/core/kernel/kernel_overlay.c`:

```c
#include <stddef.h>

#include "kernel_overlay.h"
#include "kernel_layout.h"
#include "kernel_theme.h"
#include "../gfx/gfx.h"

/* Allocated on the first open and then kept for the life of the OS. Freeing
 * it on close would mean an app task destroying memory the router task can
 * be blitting at that very moment: exactly the use-after-free this codebase
 * already hit once with window canvases, where the fix was to move the free
 * onto the owning task after its VM had stopped (see kernel_window_
 * unregister's own comment). There is no equivalent "the owner has stopped"
 * moment for something no task owns, so this design never creates the
 * hazard. The cost is ~450KB (640*360*2) retained after the first egg --
 * and only after the first one, so it is charged solely to a user who
 * triggers it. On the sim that comes from LovyanGFX's own allocator
 * (hal_display_create_canvas), not the 4MB FreeRTOS heap in
 * sim/FreeRTOSConfig.h; on hw the display layer is a stub. */
static void * g_canvas = NULL;
static int g_open = 0;

int
kernel_overlay_open( void )
{
    if( g_canvas == NULL )
    {
        g_canvas = gfx_create_canvas( KERNEL_SCREEN_W, KERNEL_SCREEN_H );
        if( g_canvas == NULL )
        {
            return 0;
        }
    }

    /* Always cleared on open, not just on allocation: createSprite
     * zero-fills to black, which would otherwise blacken the whole screen
     * for one frame, and a reopen would otherwise flash the previous
     * egg's last frame. */
    gfx_fill_rect( g_canvas, 0, 0, KERNEL_SCREEN_W, KERNEL_SCREEN_H, ACID_OVERLAY_KEY );
    g_open = 1;

    /* gfx_fill_rect already marks dirty for a non-NULL target, but an open
     * has to survive that being true for other reasons too -- this is the
     * call that guarantees the next tick recomposites with the overlay in
     * the frame. The same goes for close, which draws nothing at all. */
    gfx_mark_dirty();
    return 1;
}

void
kernel_overlay_close( void )
{
    g_open = 0;
    gfx_mark_dirty();
}

int
kernel_overlay_is_open( void )
{
    return g_open;
}

void *
kernel_overlay_canvas( void )
{
    return g_open ? g_canvas : NULL;
}
```

- [ ] **Step 7: Run both tests to verify they pass**

```bash
cd /home/norfolkh/os/v2
gcc -o /tmp/t_overlay core/kernel/test_kernel_overlay.c core/kernel/kernel_overlay.c \
  -I core/gfx -I core/kernel -I sim \
  -I components/freertos-kernel/include \
  -I components/freertos-kernel/portable/ThirdParty/GCC/Posix \
  -I components/freertos-kernel/portable/ThirdParty/GCC/Posix/utils && /tmp/t_overlay
gcc -o /tmp/t_overlay_fail core/kernel/test_kernel_overlay_alloc_fail.c core/kernel/kernel_overlay.c \
  -I core/gfx -I core/kernel -I sim \
  -I components/freertos-kernel/include \
  -I components/freertos-kernel/portable/ThirdParty/GCC/Posix \
  -I components/freertos-kernel/portable/ThirdParty/GCC/Posix/utils && /tmp/t_overlay_fail
```

Expected: `test_kernel_overlay: all assertions passed` and
`test_kernel_overlay_alloc_fail: all assertions passed`.

- [ ] **Step 8: Add the source to both builds**

In `v2/sim/CMakeLists.txt`, in the `add_executable(acidos_sim ...)` list, directly after
the `kernel_router.c` line:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/kernel/kernel_overlay.c
```

In `v2/hw/main/CMakeLists.txt`, in the `SRCS` list, directly after its `kernel_router.c`
line:

```cmake
        ../../core/kernel/kernel_overlay.c
```

- [ ] **Step 9: Verify the sim still builds and runs**

```bash
cd /home/norfolkh/os
cmake --build v2/sim/build 2>&1 | tail -5
```

Expected: builds with no errors. Then run `./v2/sim/build/acidos_sim`, confirm the
desktop appears and a window still opens from the Menu, and close it. Nothing should
look different yet — `kernel_overlay` is compiled but nothing calls it.

- [ ] **Step 10: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/kernel/kernel_overlay.h v2/core/kernel/kernel_overlay.c \
  v2/core/kernel/test_kernel_overlay.c v2/core/kernel/test_kernel_overlay_alloc_fail.c \
  v2/core/kernel/kernel_theme.h v2/sim/CMakeLists.txt v2/hw/main/CMakeLists.txt
git commit -m "$(cat <<'EOF'
v2: a kernel-owned overlay canvas, with standalone tests

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Colour-keyed blit through the HAL and gfx

The one new drawing primitive: blit a canvas while skipping every pixel of one colour.
LovyanGFX already implements it (`components/lovyangfx/src/lgfx/v1/LGFX_Sprite.hpp:336`
— `pushSprite(dst, x, y, transp)`), so the sim side is one line.

**Files:**
- Modify: `v2/core/hal/hal_display.h` (after the `hal_display_blit_canvas` declaration)
- Modify: `v2/sim/hal_display_sim.cpp:112-123` (after `hal_display_blit_canvas`)
- Modify: `v2/hw/main/hal_display_hw.c:47-51` (after its `hal_display_blit_canvas` stub)
- Modify: `v2/core/gfx/gfx.h` (after the `gfx_blit_canvas` declaration)
- Modify: `v2/core/gfx/gfx.c:149-154` (after `gfx_blit_canvas`)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `void hal_display_blit_canvas_keyed( void * target, void * canvas, int x, int y, unsigned int key )`
  and `void gfx_blit_canvas_keyed( void * canvas, int x, int y, unsigned int key )`.

- [ ] **Step 1: Declare the HAL entry point**

In `v2/core/hal/hal_display.h`, directly after the `hal_display_blit_canvas`
declaration and its comment:

```c
/* As hal_display_blit_canvas, but every pixel of `canvas` equal to `key` is
 * left untouched in `target` instead of copied. The one thing in this
 * codebase that isn't an opaque copy: it is how the kernel's overlay
 * (kernel_overlay.h) draws over the whole screen without erasing the
 * wallpaper and windows it passes over. `key` is an RGB888 literal, the
 * same as every other colour argument here. */
void hal_display_blit_canvas_keyed( void * target, void * canvas, int x, int y,
                                     unsigned int key );
```

- [ ] **Step 2: Implement it on the sim**

In `v2/sim/hal_display_sim.cpp`, directly after `hal_display_blit_canvas`:

```cpp
extern "C" void hal_display_blit_canvas_keyed( void * target, void * canvas, int x, int y,
                                                unsigned int key )
{
    /* LovyanGFX's own transparent-colour overload (LGFX_Sprite.hpp) runs
     * `key` through the same _write_conv.convert path every fillRect colour
     * in this project already goes through, so a key written as 0xFF00FF
     * matches pixels filled as 0xFF00FF, with no separate RGB565
     * translation needed at the call site. Same target convention as
     * hal_display_blit_canvas above. */
    if( target == NULL ) { as_canvas( canvas )->pushSprite( &lcd, x, y, key ); return; }
    as_canvas( canvas )->pushSprite( as_canvas( target ), x, y, key );
}
```

- [ ] **Step 3: Stub it on hw**

In `v2/hw/main/hal_display_hw.c`, directly after the `hal_display_blit_canvas` stub,
matching its existing stub style exactly:

```c
void
hal_display_blit_canvas_keyed( void * target, void * canvas, int x, int y,
                                unsigned int key )
{
    ESP_LOGI( TAG, "hal_display_blit_canvas_keyed(target=%p, canvas=%p, %d, %d, 0x%06x): stub, not drawn",
              target, canvas, x, y, key );
}
```

- [ ] **Step 4: Add the gfx wrapper**

In `v2/core/gfx/gfx.h`, directly after the `gfx_blit_canvas` declaration:

```c
/* As gfx_blit_canvas, but pixels equal to `key` are skipped -- see
 * hal_display.h. Used only by the compositor, only for the kernel overlay
 * (kernel_overlay.h). */
void gfx_blit_canvas_keyed( void * canvas, int x, int y, unsigned int key );
```

In `v2/core/gfx/gfx.c`, directly after `gfx_blit_canvas`:

```c
void
gfx_blit_canvas_keyed( void * canvas, int x, int y, unsigned int key )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    hal_display_blit_canvas_keyed( g_backbuffer, canvas, x, y, key );
    xSemaphoreGive( g_gfx_lock );
}
```

- [ ] **Step 5: Build and verify no regression**

```bash
cd /home/norfolkh/os
cmake --build v2/sim/build 2>&1 | tail -5
./v2/sim/build/acidos_sim
```

Expected: clean build. In the sim, open two windows from the Menu, drag one over the
other, and close them. Compositing must look exactly as before — nothing calls the new
path yet, and this step is here to prove the existing one was not disturbed.

- [ ] **Step 6: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/hal/hal_display.h v2/sim/hal_display_sim.cpp v2/hw/main/hal_display_hw.c \
  v2/core/gfx/gfx.h v2/core/gfx/gfx.c
git commit -m "$(cat <<'EOF'
v2: colour-keyed canvas blit through the HAL and gfx

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Composite the overlay, and expose it to Ruby

Wires Task 1 and Task 2 together and gives Ruby the four calls it needs. Ends with a
small probe app so the whole path can be seen working before any sprite exists.

**Files:**
- Modify: `v2/core/kernel/kernel_router.c:107-123` (`kernel_router_composite_frame`) and
  its include block at `:9-15`
- Modify: `v2/core/bindings/gfx_binding.c` (new statics before `acid_bindings_register`,
  new registrations inside it)
- Create: `v2/apps/overlay_probe.rb`
- Create: `v2/apps/overlay_probe.app.toml`

**Interfaces:**
- Consumes: `kernel_overlay_open/close/is_open/canvas`, `ACID_OVERLAY_KEY` (Task 1);
  `gfx_blit_canvas_keyed` (Task 2).
- Produces, to Ruby: `acid_overlay_open` → true/false, `acid_overlay_clear`,
  `acid_overlay_fill_rect(x, y, w, h, color)` (screen-absolute, clipped to the screen,
  silently ignored when closed), `acid_overlay_close`.

- [ ] **Step 1: Composite the overlay**

In `v2/core/kernel/kernel_router.c`, add to the include block after `"kernel_layout.h"`:

```c
#include "kernel_overlay.h"
#include "kernel_theme.h"
```

In `kernel_router_composite_frame`, between the window loop's closing brace and the
`gfx_present()` call (keep the existing comment above `gfx_present` where it is):

```c
    /* Last, on top of every window: the kernel overlay, blitted with its
     * key colour treated as transparent, so it draws over the whole screen
     * while leaving everything it isn't actually painting visible
     * underneath. Nothing else in this frame is keyed -- see
     * kernel_overlay.h for why this is not a window. */
    if( kernel_overlay_is_open() )
    {
        gfx_blit_canvas_keyed( kernel_overlay_canvas(), 0, 0, ACID_OVERLAY_KEY );
    }
```

- [ ] **Step 2: Add the bindings**

In `v2/core/bindings/gfx_binding.c`, add to the includes at the top:

```c
#include "../kernel/kernel_overlay.h"
#include "../kernel/kernel_layout.h"
#include "../kernel/kernel_theme.h"
```

Add these four functions directly above `acid_bindings_register`:

```c
/* The overlay (kernel_overlay.h) is the one drawing surface that is NOT
 * this app's own window: coordinates are screen-absolute, and what is
 * drawn here appears over every window including the caller's. Clipping is
 * therefore against the screen, not ctx->window_w/h, and there is no
 * ctx->canvas anywhere in these four functions. */
static mrb_value
acid_overlay_open( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_bool_value( kernel_overlay_open() != 0 );
}

static mrb_value
acid_overlay_clear( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    void * canvas = kernel_overlay_canvas();
    if( canvas == NULL )
    {
        return mrb_nil_value();
    }
    gfx_fill_rect( canvas, 0, 0, KERNEL_SCREEN_W, KERNEL_SCREEN_H, ACID_OVERLAY_KEY );
    return mrb_nil_value();
}

static mrb_value
acid_overlay_fill_rect( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int x, y, w, h, color;
    mrb_get_args( mrb, "iiiii", &x, &y, &w, &h, &color );

    void * canvas = kernel_overlay_canvas();
    if( canvas == NULL )
    {
        /* Closed overlay: silently nothing. A sprite whose animation ended
         * a frame ago, or an egg that never got a canvas, must not be an
         * error a user sees. */
        return mrb_nil_value();
    }

    /* Same clipping discipline as acid_fill_rect above, against the screen
     * rather than a window -- a sprite flying in from off-screen has
     * negative coordinates by design, so this path is normal, not
     * defensive. */
    int cx = ( int ) x;
    int cy = ( int ) y;
    int cw = ( int ) w;
    int ch = ( int ) h;
    if( cx < 0 ) { cw += cx; cx = 0; }
    if( cy < 0 ) { ch += cy; cy = 0; }
    if( cx + cw > KERNEL_SCREEN_W ) { cw = KERNEL_SCREEN_W - cx; }
    if( cy + ch > KERNEL_SCREEN_H ) { ch = KERNEL_SCREEN_H - cy; }
    if( cw <= 0 || ch <= 0 )
    {
        return mrb_nil_value();
    }

    gfx_fill_rect( canvas, cx, cy, cw, ch, ( unsigned int ) color );
    return mrb_nil_value();
}

static mrb_value
acid_overlay_close( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    kernel_overlay_close();
    return mrb_nil_value();
}
```

And inside `acid_bindings_register`, after the existing three:

```c
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_overlay_open",
                                 acid_overlay_open, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_overlay_clear",
                                 acid_overlay_clear, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_overlay_fill_rect",
                                 acid_overlay_fill_rect, MRB_ARGS_REQ( 5 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_overlay_close",
                                 acid_overlay_close, MRB_ARGS_NONE() );
```

- [ ] **Step 3: Write the probe app**

This is the overlay's manual test fixture and stays in the tree, the same way
`core/audio/test_synth.c` and `tools/test_editor.rb` do. `menu = false` keeps it out of
the Menu; `run overlay_probe` in the terminal launches it.

Create `v2/apps/overlay_probe.app.toml`:

```toml
name = Overlay Probe
w = 150
h = 70
menu = false
desc = Manual test fixture for the kernel overlay
```

Create `v2/apps/overlay_probe.rb`:

```ruby
# Manual test fixture for the kernel overlay (core/kernel/kernel_overlay.h),
# not a user-facing app -- `menu = false`, launched with `run overlay_probe`
# from the terminal. Paints one green bar and one key-coloured hole inside
# it across the middle of the screen and leaves them there until its window
# is closed, which is long enough to drag other windows over and under the
# area and confirm three things by eye: the bar draws on top of every
# window, the hole shows whatever is really underneath, and clicking on the
# bar hits whatever window is beneath it rather than the overlay.
class OverlayProbeApp < AcidApp
  BAR_X = 120
  BAR_Y = 140
  BAR_W = 400
  BAR_H = 80
  HOLE_INSET = 30

  BAR_COLOR = 0x00FF66   # THEME_HARD
  KEY_COLOR = 0xFF00FF   # ACID_OVERLAY_KEY -- must match kernel_theme.h

  def on_create
    @opened = acid_overlay_open
    return unless @opened
    acid_overlay_clear
    acid_overlay_fill_rect(BAR_X, BAR_Y, BAR_W, BAR_H, BAR_COLOR)
    acid_overlay_fill_rect(BAR_X + HOLE_INSET, BAR_Y + HOLE_INSET,
                           BAR_W - 2 * HOLE_INSET, BAR_H - 2 * HOLE_INSET,
                           KEY_COLOR)
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    acid_draw_text(@opened ? "overlay open" : "overlay failed", 6, 24, 0xD4E6DB, 0x050607)
    acid_draw_text("close me to clear", 6, 36, 0x9DAAA3, 0x050607)
    acid_draw_window_border
  end

  def on_destroy
    acid_overlay_close
  end
end

OverlayProbeApp.new.start
```

- [ ] **Step 4: Build and verify the overlay end to end**

```bash
cd /home/norfolkh/os
cmake --build v2/sim/build 2>&1 | tail -5
./v2/sim/build/acidos_sim
```

In the sim: open the Terminal from the Menu, type `run overlay_probe`, and check every
one of these:

1. A green bar with a rectangular hole appears across the middle of the screen.
2. The hole shows the wallpaper — not black, not magenta.
3. Open another app (e.g. Piano) and drag it under the bar: the bar draws **over** it,
   and the hole shows that window's own pixels through it.
4. Click on the green bar where a window sits underneath: the click must land on that
   window (it raises/responds), not on the bar.
5. Drag a window across the bar and away: no smearing or leftover sprite pixels.
6. Close the probe's window: the bar disappears completely and the screen underneath is
   intact.

- [ ] **Step 5: Commit**

```bash
cd /home/norfolkh/os
git add v2/core/kernel/kernel_router.c v2/core/bindings/gfx_binding.c \
  v2/apps/overlay_probe.rb v2/apps/overlay_probe.app.toml
git commit -m "$(cat <<'EOF'
v2: composite the kernel overlay and expose it to Ruby

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: `AcidSprite` — sprites as pictures

**Files:**
- Create: `v2/apps/lib/acid_sprite.rb`
- Test: `v2/tools/test_acid_sprite.rb`

**Interfaces:**
- Consumes: `acid_overlay_fill_rect(x, y, w, h, color)` (Task 3).
- Produces: `AcidSprite.width(rows)`, `AcidSprite.height(rows)`,
  `AcidSprite.draw(rows, x, y, scale, palette, flip = false)` — all module functions on
  `AcidSprite`. `rows` is an Array of equal-length Strings, one character per pixel;
  `"."` is transparent; `palette` is a Hash of single-character String to RGB888 Integer.

- [ ] **Step 1: Write the failing test**

Create `v2/tools/test_acid_sprite.rb`:

```ruby
# Headless tests for AcidSprite (apps/lib/acid_sprite.rb). The module's only
# OS dependency is acid_overlay_fill_rect, stubbed below to record calls, so
# this runs under the vendored host mruby with no OS underneath it.
#
# Run (this runtime has no require, so the sources are concatenated in,
# exactly the way vm_host loads them into a real app VM):
#
#   cd /home/norfolkh/os && cat v2/apps/lib/acid_sprite.rb \
#     v2/tools/test_acid_sprite.rb | ./v2/components/mruby/build/host/bin/mruby -

$fails = 0

def eq(actual, expected, what)
  if actual == expected
    puts "  ok  #{what}"
  else
    $fails += 1
    puts "FAIL  #{what}"
    puts "      expected #{expected.inspect}"
    puts "      got      #{actual.inspect}"
  end
end

def group(name)
  puts name
end

$rects = []

def acid_overlay_fill_rect(x, y, w, h, color)
  $rects << [x, y, w, h, color]
end

PAL = { "R" => 0xFF0000, "B" => 0x0000FF }

group("AcidSprite: geometry")

eq(AcidSprite.width(["..RR", "RRRR"]), 4, "width is the row length")
eq(AcidSprite.height(["..RR", "RRRR"]), 2, "height is the row count")

group("AcidSprite: drawing")

$rects = []
AcidSprite.draw(["R"], 10, 20, 1, PAL)
eq($rects, [[10, 20, 1, 1, 0xFF0000]], "a single pixel is one 1x1 rect at the origin")

$rects = []
AcidSprite.draw(["R"], 10, 20, 3, PAL)
eq($rects, [[10, 20, 3, 3, 0xFF0000]], "scale 3 makes each pixel a 3x3 block")

$rects = []
AcidSprite.draw([".R"], 10, 20, 3, PAL)
eq($rects, [[13, 20, 3, 3, 0xFF0000]], "'.' is transparent and shifts the next pixel")

$rects = []
AcidSprite.draw(["R", "B"], 10, 20, 2, PAL)
eq($rects, [[10, 20, 2, 2, 0xFF0000], [10, 22, 2, 2, 0x0000FF]],
   "rows advance by scale in y")

$rects = []
AcidSprite.draw(["..."], 10, 20, 2, PAL)
eq($rects, [], "an all-transparent row draws nothing")

$rects = []
AcidSprite.draw(["X"], 10, 20, 2, PAL)
eq($rects, [], "a character missing from the palette draws nothing")

group("AcidSprite: horizontal run merging")

# One fill per pixel would be ~190 binding calls per frame at 30fps for a
# 16x12 sprite, each taking the gfx lock and crossing into LGFX. Merging
# runs of the same colour cuts that by an order of magnitude and is why
# draw walks runs rather than pixels.
$rects = []
AcidSprite.draw(["RRR"], 0, 0, 2, PAL)
eq($rects, [[0, 0, 6, 2, 0xFF0000]], "three same-colour pixels become one wide rect")

$rects = []
AcidSprite.draw(["RRBB"], 0, 0, 1, PAL)
eq($rects, [[0, 0, 2, 1, 0xFF0000], [2, 0, 2, 1, 0x0000FF]],
   "a colour change ends the run")

$rects = []
AcidSprite.draw(["RR.R"], 0, 0, 1, PAL)
eq($rects, [[0, 0, 2, 1, 0xFF0000], [3, 0, 1, 1, 0xFF0000]],
   "a transparent gap ends the run")

group("AcidSprite: flip")

$rects = []
AcidSprite.draw([".R"], 0, 0, 1, PAL, true)
eq($rects, [[0, 0, 1, 1, 0xFF0000]], "flip mirrors the row horizontally")

$rects = []
AcidSprite.draw(["RRB"], 0, 0, 1, PAL, true)
eq($rects, [[0, 0, 1, 1, 0x0000FF], [1, 0, 2, 1, 0xFF0000]],
   "flip preserves run merging in mirrored order")

puts ""
puts $fails == 0 ? "all passed" : "#{$fails} FAILED"
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /home/norfolkh/os && cat v2/apps/lib/acid_sprite.rb v2/tools/test_acid_sprite.rb \
  | ./v2/components/mruby/build/host/bin/mruby -
```

Expected: FAIL — `cat` reports `v2/apps/lib/acid_sprite.rb: No such file or directory`
and mruby raises `undefined method 'width' for AcidSprite`.

- [ ] **Step 3: Write the implementation**

Create `v2/apps/lib/acid_sprite.rb`:

```ruby
# Video-game sprites for the kernel overlay (core/kernel/kernel_overlay.h).
#
# A sprite is written as a picture: an array of equal-length strings, one
# character per pixel, plus a palette mapping each character to a colour.
# "." is transparent. That means a sprite is edited by redrawing it in the
# source, not by recomputing coordinates:
#
#   TEAPOT = [ "..WWW..",
#              ".WWWWW.",
#              "WWWWWWW" ]
#   PALETTE = { "W" => 0xE8E8F0 }
#
# Draws through acid_overlay_fill_rect, so coordinates are screen-absolute
# and the result appears over every window -- see kernel_overlay.h. Nothing
# here is window-aware, and nothing here is specific to the easter eggs
# (apps/lib/acid_eggs.rb) that are its first caller.
module AcidSprite
  def self.width(rows)
    rows[0].length
  end

  def self.height(rows)
    rows.length
  end

  # `flip` mirrors horizontally, so one drawing of a character can face
  # either way -- what makes a sprite flying right-to-left look like it is
  # facing the way it is going rather than flying backwards.
  #
  # Draws runs of identical colour as single rects rather than one rect per
  # pixel. A 16x12 sprite is ~190 pixels; at 30fps that would be ~5,700
  # binding calls a second, each taking gfx's lock and crossing into LGFX.
  # Merged, the same sprite is a few dozen.
  def self.draw(rows, x, y, scale, palette, flip = false)
    r = 0
    while r < rows.length
      row = rows[r]
      draw_row(row, x, y + r * scale, scale, palette, flip)
      r += 1
    end
  end

  def self.draw_row(row, x, y, scale, palette, flip)
    len = row.length
    c = 0
    while c < len
      ch = flip ? row[len - 1 - c, 1] : row[c, 1]
      color = (ch == ".") ? nil : palette[ch]
      if color.nil?
        c += 1
        next
      end

      run = 1
      while c + run < len
        nch = flip ? row[len - 1 - (c + run), 1] : row[c + run, 1]
        break unless nch == ch
        run += 1
      end

      acid_overlay_fill_rect(x + c * scale, y, run * scale, scale, color)
      c += run
    end
  end
end
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd /home/norfolkh/os && cat v2/apps/lib/acid_sprite.rb v2/tools/test_acid_sprite.rb \
  | ./v2/components/mruby/build/host/bin/mruby -
```

Expected: every line `ok`, final line `all passed`.

- [ ] **Step 5: Commit**

```bash
cd /home/norfolkh/os
git add v2/apps/lib/acid_sprite.rb v2/tools/test_acid_sprite.rb
git commit -m "$(cat <<'EOF'
v2: AcidSprite -- pixel sprites drawn from picture-strings

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: `AcidEggs` — the three animations

**Files:**
- Create: `v2/apps/lib/acid_eggs.rb`
- Test: `v2/tools/test_acid_eggs.rb`

**Interfaces:**
- Consumes: `AcidSprite.draw/width/height` (Task 4); `acid_overlay_open`,
  `acid_overlay_clear`, `acid_overlay_close` (Task 3); `acid_play_note(voice, ona, volume)`,
  `acid_trigger_arp(voice, n0, n1, n2, n3, count, rate_ms)`, `acid_stop_note(voice)`,
  `acid_configure_voice(voice, filter_route, attack_ms, decay_ms, sustain_percent, release_ms)`.
- Produces: `AcidEggs::TICK_MS` (Integer, 33), `AcidEggs.names` (Array of String),
  `AcidEggs.start(name, now_ms = nil)` → true/false, `AcidEggs.active?` → true/false,
  `AcidEggs.step(now_ms = nil)`, `AcidEggs.abort`, `AcidEggs.word_box` → `[x, y, w, h]`.
  `now_ms` is an Integer of milliseconds; when omitted it comes from `Time.now`.

- [ ] **Step 1: Write the failing test**

Create `v2/tools/test_acid_eggs.rb`:

```ruby
# Headless tests for AcidEggs (apps/lib/acid_eggs.rb) -- the state machine
# and geometry of the three terminal easter eggs. Every OS call it makes is
# stubbed below, and every time-dependent entry point takes an explicit
# now_ms, so the whole animation can be stepped deterministically with no OS
# and no clock underneath it.
#
# Run (this runtime has no require, so the sources are concatenated in,
# exactly the way vm_host loads them into a real app VM):
#
#   cd /home/norfolkh/os && cat v2/apps/lib/acid_sprite.rb v2/apps/lib/acid_eggs.rb \
#     v2/tools/test_acid_eggs.rb | ./v2/components/mruby/build/host/bin/mruby -

$fails = 0

def eq(actual, expected, what)
  if actual == expected
    puts "  ok  #{what}"
  else
    $fails += 1
    puts "FAIL  #{what}"
    puts "      expected #{expected.inspect}"
    puts "      got      #{actual.inspect}"
  end
end

def group(name)
  puts name
end

$rects = []
$clears = 0
$opens = 0
$closes = 0
$open_result = true
$notes = []

def acid_overlay_fill_rect(x, y, w, h, color)
  $rects << [x, y, w, h, color]
end

def acid_overlay_clear
  $clears += 1
end

def acid_overlay_open
  $opens += 1
  $open_result
end

def acid_overlay_close
  $closes += 1
end

def acid_play_note(voice, ona, volume)
  $notes << [:play, voice, ona, volume]
end

def acid_trigger_arp(voice, n0, n1, n2, n3, count, rate_ms)
  $notes << [:arp, voice, n0, n1, n2, n3, count, rate_ms]
end

def acid_stop_note(voice)
  $notes << [:stop, voice]
end

def acid_configure_voice(voice, filter_route, attack_ms, decay_ms, sustain_percent, release_ms)
  $notes << [:configure, voice]
end

def reset!
  $rects = []
  $clears = 0
  $opens = 0
  $closes = 0
  $open_result = true
  $notes = []
  AcidEggs.abort
  $closes = 0
end

group("AcidEggs: starting and stopping")

reset!
eq(AcidEggs.active?, false, "inactive before anything starts")
eq(AcidEggs.start("dave", 1000), true, "dave starts")
eq(AcidEggs.active?, true, "active once started")
eq($opens, 1, "start opens the overlay")

reset!
eq(AcidEggs.start("nope", 1000), false, "an unknown name does not start")
eq(AcidEggs.active?, false, "and leaves the eggs inactive")
eq($opens, 0, "and never opens the overlay")

reset!
$open_result = false
eq(AcidEggs.start("dave", 1000), false, "a failed overlay open does not start")
eq(AcidEggs.active?, false, "and leaves the eggs inactive")

reset!
AcidEggs.start("dave", 1000)
AcidEggs.abort
eq(AcidEggs.active?, false, "abort stops the animation")
eq($closes, 1, "abort closes the overlay")

reset!
AcidEggs.start("dave", 1000)
eq(AcidEggs.start("joe", 1000), false, "a second egg is refused while one is in flight")

group("AcidEggs: the tick guard")

reset!
AcidEggs.start("dave", 1000)
before = $clears
AcidEggs.step(1000 + AcidEggs::TICK_MS - 1)
eq($clears, before, "no frame is drawn before TICK_MS has elapsed")
AcidEggs.step(1000 + AcidEggs::TICK_MS)
eq($clears, before + 1, "a frame is drawn once TICK_MS has elapsed")

group("AcidEggs: dave and joe fly and leave")

["dave", "joe"].each do |name|
  reset!
  AcidEggs.start(name, 0)
  t = 0
  frames = 0
  while AcidEggs.active? && frames < 500
    t += AcidEggs::TICK_MS
    AcidEggs.step(t)
    frames += 1
  end
  eq(AcidEggs.active?, false, "#{name} finishes on its own")
  eq(frames < 500, true, "#{name} finishes within 500 frames (took #{frames})")
  eq($closes, 1, "#{name} closes the overlay exactly once")
  eq($rects.length > 0, true, "#{name} actually drew something")
end

group("AcidEggs: dave and joe stay on screen vertically")

# Run each egg many times: the height and direction are random per run, and
# a sprite half off the top or bottom of the screen is the bug this catches.
i = 0
while i < 40
  reset!
  AcidEggs.start("dave", 0)
  t = 0
  while AcidEggs.active? && t < 20000
    t += AcidEggs::TICK_MS
    AcidEggs.step(t)
  end
  ys = $rects.map { |r| r[1] }
  eq(ys.min >= 0, true, "dave never draws above the top of the screen")
  eq(ys.max < AcidEggs::SCREEN_H, true, "dave never draws below the bottom")
  i += 1
end

group("AcidEggs: maximbady bounces then shouts")

reset!
AcidEggs.start("maximbady", 0)
t = 0
saw_bounce_sound = false
while AcidEggs.active? && t < 60000
  t += AcidEggs::TICK_MS
  AcidEggs.step(t)
  saw_bounce_sound = true if $notes.length > 0
end
eq(AcidEggs.active?, false, "maximbady finishes on its own")
eq($closes, 1, "maximbady closes the overlay exactly once")
eq(saw_bounce_sound, true, "maximbady makes a sound")

# Centring is asserted against the layout box, not against the drawn pixels:
# both glyphs have blank columns in some of their rows, so the ink's own
# bounding box is narrower than the space the word occupies and is the wrong
# thing to measure.
box = AcidEggs.word_box
eq(box[0], (AcidEggs::SCREEN_W - box[2]) / 2, "the word box is horizontally centred")
eq(box[1], (AcidEggs::SCREEN_H - box[3]) / 2, "the word box is vertically centred")
eq(box[0] >= 0 && box[0] + box[2] <= AcidEggs::SCREEN_W, true,
   "the word fits across the screen")
eq(box[1] >= 0 && box[1] + box[3] <= AcidEggs::SCREEN_H, true,
   "the word fits down the screen")

# ...and the ink actually lands inside that box.
reset!
AcidEggs.start("maximbady", 0)
t = 0
word_rects = []
while AcidEggs.active? && t < 60000
  t += AcidEggs::TICK_MS
  before = $rects.length
  AcidEggs.step(t)
  frame = $rects[before, $rects.length - before]
  # The word frame is the one with far more rects than a single small figure.
  word_rects = frame if frame.length > word_rects.length
end
eq(word_rects.length > 0, true, "the word phase drew something")
eq(word_rects.map { |r| r[0] }.min >= box[0], true, "no glyph ink starts left of the box")
eq(word_rects.map { |r| r[0] + r[2] }.max <= box[0] + box[2], true,
   "no glyph ink runs past the right of the box")
eq(word_rects.map { |r| r[1] }.min >= box[1], true, "no glyph ink starts above the box")
eq(word_rects.map { |r| r[1] + r[3] }.max <= box[1] + box[3], true,
   "no glyph ink runs below the box")

puts ""
puts $fails == 0 ? "all passed" : "#{$fails} FAILED"
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /home/norfolkh/os && cat v2/apps/lib/acid_sprite.rb v2/apps/lib/acid_eggs.rb \
  v2/tools/test_acid_eggs.rb | ./v2/components/mruby/build/host/bin/mruby -
```

Expected: FAIL — `cat` reports `v2/apps/lib/acid_eggs.rb: No such file or directory` and
mruby raises `uninitialized constant AcidEggs`.

- [ ] **Step 3: Write the implementation**

Create `v2/apps/lib/acid_eggs.rb`:

```ruby
# The terminal's three easter eggs (see apps/terminal.rb): `dave` flies a
# superman across the screen, `joe` flies a teapot, `maximbady` bounces a
# figure around and then shouts. All three draw on the kernel overlay
# (core/kernel/kernel_overlay.h) -- over the wallpaper, the taskbar and
# every open window.
#
# A module, not an app, and deliberately so. Spawning an app for this would
# have cost an 8KB FreeRTOS task stack, a whole mrb_open() and six Ruby
# files compiled from source (kernel_spawn.c:75, vm_host.c:260-276) every
# time someone types `dave`. Instead the terminal drives it from its own
# event loop: see AcidApp#poll_timeout_ms and TerminalApp#on_idle.
#
# Every entry point takes an explicit now_ms so the whole animation can be
# stepped deterministically with no clock (tools/test_acid_eggs.rb).
module AcidEggs
  SCREEN_W = 640   # KERNEL_SCREEN_W (core/kernel/kernel_layout.h)
  SCREEN_H = 360   # KERNEL_SCREEN_H
  TICK_MS = 33     # ~30fps
  SCALE = 3

  # Voices 0-4 belong to the games (acid_blaster.rb, breakout.rb) and 5 to
  # the piano (piano.rb) out of SYNTH_NUM_VOICES 8, so 6 is free.
  VOICE = 6
  NOTE_TICKS = 6

  FLY_SPEED = 7
  BOUNCE_SPEED = 5
  BOUNCE_LIMIT = 6
  WORD_FRAMES = 60

  # A bob table instead of Math.sin: no dependency on whether this mruby
  # build carries mruby-math, and an integer pixel offset is what actually
  # gets drawn anyway.
  BOB = [ 0, 1, 2, 3, 3, 2, 1, 0, -1, -2, -3, -3, -2, -1 ]

  PALETTE = {
    "K" => 0x101010,   # hair, outline
    "S" => 0xE8B48A,   # skin
    "B" => 0x2050E0,   # superman blue / maximbady trousers
    "R" => 0xE01020,   # cape / maximbady top
    "Y" => 0xFFD400,   # chest emblem
    "W" => 0xE8E8F0,   # teapot body
    "G" => 0x9AA4B0,   # teapot shadow
    "N" => 0x18B830    # maximbady green
  }

  # Flying right: cape trails to the LEFT, arms reach right. AcidSprite's
  # flip handles the other direction.
  DAVE = [ "......KKKK....",
           ".....KSSSSK...",
           "RR...KSSSSK...",
           "RRR..KSSSSK...",
           "RRRRBBBBBBSSS.",
           "RRRRBBYBBBSSSS",
           "RRRRBBBBBBSSS.",
           "RRR..BBBBB....",
           "RR...BB..BB...",
           ".....RR..RR..." ]

  JOE = [ "....WWWWW.....",
          "...WWWWWWW....",
          "..WWWWWWWWW.WW",
          "WWWWWWWWWWWWWW",
          "WWWWWWWWWWWW.W",
          "WWWWWWWWWWW...",
          ".GWWWWWWWWG...",
          "..GGGGGGGG...." ]

  # Blue trousers, red top, green head and arms, exactly as asked for.
  MAXIMBADY = [ "..NNN..",
                "..NNN..",
                "N.RRR.N",
                "NRRRRRN",
                "NRRRRRN",
                "..RRR..",
                "..BBB..",
                "..B.B..",
                "..B.B.." ]

  # The two glyphs "SOOOOOOOOO" needs, at 5x7. The built-in 6px font would
  # render the whole word 60px wide -- far too small for a screen-centre
  # gag, which is why the word is drawn as sprites like everything else.
  GLYPH_SCALE = 6
  GLYPH_GAP = 6
  FONT = {
    "S" => [ ".YYYY",
             "Y....",
             "Y....",
             ".YYY.",
             "....Y",
             "....Y",
             "YYYY." ],
    "O" => [ ".YYY.",
             "Y...Y",
             "Y...Y",
             "Y...Y",
             "Y...Y",
             "Y...Y",
             ".YYY." ]
  }
  WORD = "SOOOOOOOOO"

  def self.names
    [ "dave", "joe", "maximbady" ]
  end

  def self.active?
    !@egg.nil?
  end

  def self.start(name, now_ms = nil)
    return false if active?
    return false unless names.include?(name)
    return false unless acid_overlay_open

    @egg = name
    @now = now_ms.nil? ? now_millis : now_ms
    @last_ms = @now - TICK_MS   # draw the first frame immediately
    @frame = 0
    @note_ticks = 0
    @phase = :fly

    acid_configure_voice(VOICE, 1, 4, 60, 40, 90)

    if name == "maximbady"
      @sprite = MAXIMBADY
      @bounces = 0
      @x = 40
      @y = 40
      @vx = BOUNCE_SPEED
      @vy = BOUNCE_SPEED
      @word_frames = 0
    else
      @sprite = (name == "dave") ? DAVE : JOE
      w = AcidSprite.width(@sprite) * SCALE
      h = AcidSprite.height(@sprite) * SCALE
      # Random height that keeps the whole sprite on screen even at the
      # extremes of the bob, and a random direction each time.
      bob = 3 * SCALE
      @y = bob + rand(SCREEN_H - h - 2 * bob)
      @flip = rand(2) == 0
      if @flip
        @x = SCREEN_W
        @vx = -FLY_SPEED
      else
        @x = -w
        @vx = FLY_SPEED
      end
      whoosh
    end

    true
  end

  def self.step(now_ms = nil)
    return unless active?
    now = now_ms.nil? ? now_millis : now_ms
    return if now - @last_ms < TICK_MS
    @last_ms = now
    @frame += 1

    tick_note

    if @phase == :word
      step_word
    elsif @egg == "maximbady"
      step_bounce
    else
      step_fly
    end
  end

  def self.abort
    return unless active?
    acid_stop_note(VOICE)
    acid_overlay_close
    @egg = nil
    @phase = nil
  end

  # ------------------------------------------------------------- phases

  def self.step_fly
    @x += @vx
    w = AcidSprite.width(@sprite) * SCALE
    if @x > SCREEN_W || @x + w < 0
      abort
      return
    end

    bob = BOB[@frame % BOB.length] * ((@egg == "dave") ? 1 : 0)
    wobble = (@egg == "joe") ? BOB[(@frame / 2) % BOB.length] / 2 : 0
    acid_overlay_clear
    AcidSprite.draw(@sprite, @x, @y + bob + wobble, SCALE, PALETTE, @flip)
  end

  def self.step_bounce
    w = AcidSprite.width(@sprite) * SCALE
    h = AcidSprite.height(@sprite) * SCALE

    @x += @vx
    @y += @vy

    bounced = false
    if @x <= 0
      @x = 0
      @vx = -@vx
      bounced = true
    elsif @x + w >= SCREEN_W
      @x = SCREEN_W - w
      @vx = -@vx
      bounced = true
    end
    if @y <= 0
      @y = 0
      @vy = -@vy
      bounced = true
    elsif @y + h >= SCREEN_H
      @y = SCREEN_H - h
      @vy = -@vy
      bounced = true
    end

    if bounced
      @bounces += 1
      boing
    end

    if @bounces >= BOUNCE_LIMIT
      @phase = :word
      @word_frames = 0
      slide
      acid_overlay_clear
      return
    end

    acid_overlay_clear
    AcidSprite.draw(@sprite, @x, @y, SCALE, PALETTE)
  end

  def self.step_word
    @word_frames += 1
    if @word_frames > WORD_FRAMES
      abort
      return
    end

    # Redrawn every frame rather than drawn once: the overlay canvas is not
    # persistent across an app's other drawing, and a single clear+draw per
    # frame is the same path every other phase uses.
    acid_overlay_clear
    draw_word
  end

  # [x, y, w, h] of the space the whole word occupies, centred on screen.
  # Separate from draw_word because the glyphs have blank columns in some of
  # their rows, so the drawn pixels' own bounding box is narrower than this
  # and is the wrong thing for anything (a test, a future backdrop) to
  # measure centring against.
  def self.word_box
    glyph_w = 5 * GLYPH_SCALE
    glyph_h = 7 * GLYPH_SCALE
    total = WORD.length * glyph_w + (WORD.length - 1) * GLYPH_GAP
    [ (SCREEN_W - total) / 2, (SCREEN_H - glyph_h) / 2, total, glyph_h ]
  end

  def self.draw_word
    box = word_box
    glyph_w = 5 * GLYPH_SCALE
    i = 0
    while i < WORD.length
      AcidSprite.draw(FONT[WORD[i, 1]], box[0] + i * (glyph_w + GLYPH_GAP), box[1],
                      GLYPH_SCALE, PALETTE)
      i += 1
    end
  end

  # -------------------------------------------------------------- sound

  def self.whoosh
    if @egg == "dave"
      acid_play_note(VOICE, 28, 40)
      acid_trigger_arp(VOICE, 28, 35, 40, 47, 4, 60)
    else
      acid_play_note(VOICE, 52, 35)
      acid_trigger_arp(VOICE, 52, 56, 52, 56, 2, 90)
    end
    @note_ticks = NOTE_TICKS * 2
  end

  def self.boing
    acid_play_note(VOICE, 40 + rand(8), 45)
    @note_ticks = NOTE_TICKS
  end

  def self.slide
    acid_play_note(VOICE, 50, 55)
    acid_trigger_arp(VOICE, 50, 45, 38, 31, 4, 110)
    @note_ticks = NOTE_TICKS * 4
  end

  def self.tick_note
    return if @note_ticks <= 0
    @note_ticks -= 1
    acid_stop_note(VOICE) if @note_ticks == 0
  end

  def self.now_millis
    (Time.now.to_f * 1000).to_i
  end
end
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd /home/norfolkh/os && cat v2/apps/lib/acid_sprite.rb v2/apps/lib/acid_eggs.rb \
  v2/tools/test_acid_eggs.rb | ./v2/components/mruby/build/host/bin/mruby -
```

Expected: every line `ok`, final line `all passed`. If the centring assertion fails,
fix `draw_word`'s arithmetic — not the test.

- [ ] **Step 5: Re-run the sprite test to confirm nothing regressed**

```bash
cd /home/norfolkh/os && cat v2/apps/lib/acid_sprite.rb v2/tools/test_acid_sprite.rb \
  | ./v2/components/mruby/build/host/bin/mruby -
```

Expected: `all passed`.

- [ ] **Step 6: Commit**

```bash
cd /home/norfolkh/os
git add v2/apps/lib/acid_eggs.rb v2/tools/test_acid_eggs.rb
git commit -m "$(cat <<'EOF'
v2: AcidEggs -- the three terminal easter egg animations

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Wire the eggs into the terminal

**Files:**
- Modify: `v2/apps/lib/acid_app.rb` (the `start` loop's `acid_poll_event(200)` call, and
  a new method above `start`)
- Modify: `v2/apps/terminal.app.toml` (add a `libs` line)
- Modify: `v2/apps/terminal.rb` (new constant, `run_command` hook, four new methods)

**Interfaces:**
- Consumes: `AcidEggs.names/start/step/abort/active?`, `AcidEggs::TICK_MS` (Task 5).
- Produces: `AcidApp#poll_timeout_ms` (Integer, default 200), overridable by any app.

- [ ] **Step 1: Make the poll timeout overridable**

In `v2/apps/lib/acid_app.rb`, add directly above `def start`:

```ruby
  # How long acid_poll_event blocks when there is no event waiting, and so
  # how often on_idle fires. 200ms is right for an app that only redraws in
  # response to input; an app animating something (the terminal, while an
  # easter egg is in flight -- see apps/lib/acid_eggs.rb) overrides this to
  # a frame interval while the animation runs and returns to 200 after. Not
  # a constant, because the answer changes while the app is running.
  def poll_timeout_ms
    200
  end
```

And in `start`, replace:

```ruby
      ev = acid_poll_event(200)
```

with:

```ruby
      ev = acid_poll_event(poll_timeout_ms)
```

- [ ] **Step 2: Load the libraries into the terminal's VM**

In `v2/apps/terminal.app.toml`, add a `libs` line (the same mechanism the Editor uses
for its five modules):

```toml
libs = lib/acid_sprite.rb, lib/acid_eggs.rb
```

- [ ] **Step 3: Add the triggers**

In `v2/apps/terminal.rb`, insert this as the very first thing in `run_command`, before
`parts = line.split(" ")`:

```ruby
    # The easter eggs (apps/lib/acid_eggs.rb), matched before any real
    # command and deliberately undocumented: nothing is printed, they are
    # absent from cmd_help, and the animation is the whole response. Matched
    # case-insensitively on the whole line, which submit has already
    # stripped, so only a bare word with no arguments fires one.
    #
    # Returns whether or not the egg actually started: a refused start (one
    # already in flight, or the overlay's canvas failed to allocate) must
    # stay just as silent, rather than falling through to "command not
    # found: dave" and announcing that the word means something.
    if AcidEggs.names.include?(line.downcase)
      AcidEggs.start(line.downcase)
      return
    end
```

Then add these four methods at the end of the class, before its closing `end`:

```ruby
  # While an egg is in flight the loop needs to wake up every frame rather
  # than every 200ms. Typing is unaffected: a keystroke still arrives as an
  # event the moment it happens.
  def poll_timeout_ms
    AcidEggs.active? ? AcidEggs::TICK_MS : 200
  end

  def on_idle
    AcidEggs.step
  end

  def on_destroy
    # Closing the terminal mid-flight takes the overlay with it, rather
    # than leaving a sprite frozen on the screen with nothing left running
    # to clear it.
    AcidEggs.abort
  end
```

And in `on_key`, after the existing final `redraw` line, add:

```ruby
    # on_idle only fires when acid_poll_event times out, so a burst of
    # keystrokes would otherwise stall the animation. AcidEggs.step is
    # guarded by its own TICK_MS check, which makes this call free whenever
    # it isn't time for a frame yet.
    AcidEggs.step
```

- [ ] **Step 4: Verify the terminal still works with no egg involved**

```bash
cd /home/norfolkh/os && ./v2/sim/build/acidos_sim
```

Open the Terminal from the Menu and check the ordinary paths still behave:

1. `help` lists the same commands as before, with no eggs in it.
2. `ls`, `cd App`, `ls`, `cat about.txt`, `pwd` all work.
3. `banana` prints `command not found: banana`.
4. Up/down arrow history still works.

- [ ] **Step 5: Verify all three eggs**

In the same terminal session:

1. Type `dave` — a superman flies across the screen at a random height, in a random
   direction, over the wallpaper, over the taskbar and over the terminal window itself.
   Nothing is printed to the scrollback except the echoed `$ dave`.
2. Type `dave` several more times — the height and direction vary, and he faces the way
   he is flying.
3. Type `DAVE` and `Dave` — both fire (matching is case-insensitive).
4. Type `joe` — a teapot does the same.
5. Type `maximbady` — a figure with blue trousers, a red top and a green head and arms
   bounces off all four edges six times, then **SOOOOOOOOO** appears large and centred,
   holds for about two seconds, and disappears cleanly.
6. Type continuously while an egg is in flight — characters appear in the input line and
   the animation keeps moving.
7. Type `dave` then immediately `joe` — the second is ignored while the first is flying,
   and `joe` works normally once Dave has gone.
8. Start an egg and close the terminal window mid-flight — the sprite disappears with
   the window and the screen underneath is intact.
9. After every egg, confirm the screen is undamaged: wallpaper, taskbar and window
   contents all as they were.
10. Open the Piano and play a note while an egg is flying — the egg's sound uses voice 6
    and must not cut the piano off.

- [ ] **Step 6: Re-run both Ruby test suites**

```bash
cd /home/norfolkh/os
cat v2/apps/lib/acid_sprite.rb v2/tools/test_acid_sprite.rb | ./v2/components/mruby/build/host/bin/mruby -
cat v2/apps/lib/acid_sprite.rb v2/apps/lib/acid_eggs.rb v2/tools/test_acid_eggs.rb | ./v2/components/mruby/build/host/bin/mruby -
```

Expected: `all passed` from both.

- [ ] **Step 7: Commit**

```bash
cd /home/norfolkh/os
git add v2/apps/lib/acid_app.rb v2/apps/terminal.rb v2/apps/terminal.app.toml
git commit -m "$(cat <<'EOF'
v2: dave, joe and maximbady fly across the terminal's screen

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Verification checklist for the whole feature

- [ ] `v2/sim/build` builds with no new warnings.
- [ ] `test_kernel_overlay` and `test_kernel_overlay_alloc_fail` pass.
- [ ] `test_acid_sprite.rb` and `test_acid_eggs.rb` pass.
- [ ] `run overlay_probe` still demonstrates transparency and click-through.
- [ ] All three eggs run over the wallpaper, the taskbar and other windows.
- [ ] The terminal is fully usable during and after every egg, and `help` never mentions
      them.
- [ ] `v2/hw/main/CMakeLists.txt` lists `kernel_overlay.c` and
      `v2/hw/main/hal_display_hw.c` has the keyed-blit stub (compile-verified only — no
      ESP-IDF here).
