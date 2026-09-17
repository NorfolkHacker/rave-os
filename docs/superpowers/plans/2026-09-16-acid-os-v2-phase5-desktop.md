# acid OS v2: desktop shell, keyboard, file manager, editor (Phase 5) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** give acid OS v2 real keyboard input, a desktop shell that actually shows and
lets you switch between running apps (replacing the current 20px placeholder strip), a
file manager, and a text editor — all working end-to-end on `v2/sim`, unmodified on
`v2/hw` (statically verified only, no ESP-IDF on this machine, same pre-existing gap as
every prior phase).

**Architecture:** a new `hal_input_poll_key` HAL function (sim: diffs
`SDL_GetKeyboardState` frame-to-frame — *not* a second `SDL_PollEvent` consumer, which
would race `Panel_sdl`'s own internal one); a `KERNEL_EVENT_KEY` event type and a
click-to-focus model in `kernel_router.c` (`kernel_router_activate_window` both raises a
window and sets keyboard focus to it — the one function both a direct click and a new
taskbar tap call); a new `acid_poll_event` return shape (`[:key, code, pressed]`) and
`on_key`/`on_idle` hooks on `AcidApp`/`AcidGame`; a new `window_binding.c` exposing the
window list to Ruby (`acid_window_max`/`acid_window_info`/`acid_activate_window`); a
rewritten `desktop.rb` taskbar built on that binding; a `file_manager.rb` and
`editor.rb`, both pure Ruby against this build's already-present `File`/`Dir` classes
(confirmed working by live throwaway-app testing during design, no new C binding
needed for filesystem access).

**Tech Stack:** C (HAL/kernel/bindings, both `v2/sim` and ESP-IDF `v2/hw`), mruby
(app-side Ruby, using this build's existing `mruby-io`/`mruby-dir`/`mruby-array-ext`/
`mruby-string-ext` gems — no new gems), SDL2 (`SDL_GetKeyboardState`), LovyanGFX
(existing `acid_fill_rect`/`acid_draw_text`/`acid_fill_circle` primitives — no new
drawing primitives this phase).

**Spec:** `docs/superpowers/specs/2026-09-16-acid-os-v2-phase5-desktop-design.md`

## Global Constraints

- No RaveOS v1 files (`kernel/`, `boot/`, `programs/`, `demos/`) touched.
- No cross-app dynamic spawn (file manager launching the editor with a runtime file
  argument) — `kernel_spawn_app` stays C-boot-only, not exposed to Ruby. The editor
  always opens the fixed path `v2/home/notes.txt`. See spec's Scope decisions.
- No new gfx/drawing primitives. Every app in this plan draws with `acid_fill_rect`,
  `acid_draw_text`, `acid_fill_circle`, `acid_clear_user_area`, `acid_draw_window_frame`,
  `acid_draw_desktop_strip` — all already exist.
- New UI surface (taskbar buttons, file manager listing, editor body) uses
  `THEME_PANEL`/`THEME_TEXT` (mirrored as Ruby hex literals with a `# THEME_*` comment,
  matching `acid_blaster.rb`'s existing convention) for surfaces and body text;
  `THEME_HARD` (acid green) is reserved for accents/selection-highlight/cursor only —
  this phase's concrete answer to the "just green dots" legibility complaint. See
  spec's Scope decisions for what is and isn't in scope for that complaint.
- Keyboard vocabulary is exactly: printable ASCII 32-126 (Shift-resolved) plus
  `KERNEL_KEY_ENTER/BACKSPACE/ESCAPE/TAB/DELETE/UP/DOWN/LEFT/RIGHT`. No Ctrl/Alt, no
  key-repeat-while-held, no IME. `hal_keycode.h`'s `KERNEL_KEY_*` values and
  `acid_keys.rb`'s `AcidKeys::*` values must stay numerically identical — checked in
  this plan's own self-review (Task 2) and whenever either file changes later.
- `v2/hw` gets the same source files added to `v2/hw/main/CMakeLists.txt` and the same
  boot-sequence spawn calls as `v2/sim`, matching every prior phase's pattern.
  `idf.py build` is not runnable on this machine — changes here are statically
  verified only, same pre-existing gap as every prior phase. **This phase's own
  additional hw gap, explicitly flagged, not silently carried:** `File`/`Dir` require a
  mounted VFS on ESP-IDF that `v2/hw` does not set up; the file manager/editor will
  build for `hw` but their filesystem calls will not function on real hardware without
  it (real hw-bring-up scope, a later roadmap phase).
- Real verification only, matching every prior phase's discipline: builds that succeed
  and processes that don't crash are never sufficient evidence on their own. This
  plan's tasks use real screenshots (Xvfb + `xdotool` + ImageMagick's `import`/
  `convert`), real `xdotool key`/`keydown`/`keyup` keyboard injection against the live
  SDL window, and `puts`-based throwaway-app checks for anything not visually
  observable — the same screenshot pipeline the game phase's plan already verified
  working on this exact machine (`Xvfb :N -screen 0 320x240x24`, `DISPLAY=:N
  v2/sim/build/acidos_sim &` launched from the repo root — script paths resolve
  relative to the process's own cwd, not the binary's location — `xdotool search
  --name "LGFX Simulator"` finds the one SDL window, `import -window "$WIN" shot.png`
  captures it, `convert shot.png -crop 1x1+X+Y txt:-` reads a pixel).
- Every task must leave `git status` clean of anything but its own intended changes —
  no temporary test apps, no leftover `sim_main.c`/`app_main.c` edits, no stray
  screenshot files, committed by accident.

---

### Task 1: Keyboard HAL — `hal_input_poll_key`, `KERNEL_EVENT_KEY`, router focus model

**Files:**
- Create: `v2/core/hal/hal_keycode.h`
- Create: `v2/sim/hal_input_sim.cpp`
- Modify: `v2/core/hal/hal_input.h`
- Modify: `v2/hw/main/hal_input_hw.c`
- Modify: `v2/core/kernel/kernel_event.h`
- Modify: `v2/core/kernel/kernel_router.h`
- Modify: `v2/core/kernel/kernel_router.c`
- Modify: `v2/sim/CMakeLists.txt`

**Interfaces:**
- Produces: `void hal_input_poll_key(int *keycode, bool *pressed)` (edge-triggered — see
  header doc comment); `KERNEL_KEY_ENTER`/`BACKSPACE`/`ESCAPE`/`TAB`/`DELETE`/`UP`/
  `DOWN`/`LEFT`/`RIGHT` constants (257-265); `KERNEL_EVENT_KEY = 3`;
  `void kernel_router_activate_window(void *task)`.

- [ ] **Step 1: Keycode vocabulary**

Create `v2/core/hal/hal_keycode.h`:

```c
#ifndef ACID_HAL_KEYCODE_H
#define ACID_HAL_KEYCODE_H

/* hal_input_poll_key's keycode vocabulary. Printable keys report their
 * plain ASCII value (32-126) directly -- correct case/symbol already
 * resolved against the Shift state at the point the HAL detects the press,
 * so app code never handles modifier state itself, matching this project's
 * "translate at the HAL boundary" convention (hal_input_poll_touch already
 * hands back resolved coordinates, not raw device units). Non-printable
 * keys use one of the named constants below, chosen from a range (256+)
 * that can never collide with a printable ASCII value.
 *
 * These values must stay numerically in sync with v2/apps/lib/acid_keys.rb
 * -- there is no shared header between C and mruby in this project (see
 * vm_host.c's own comment on why: no require/require_relative gem), so the
 * two are hand-kept in agreement. Checked in phase 5's plan self-review;
 * check again if either file changes later. */
#define KERNEL_KEY_ENTER     257
#define KERNEL_KEY_BACKSPACE 258
#define KERNEL_KEY_ESCAPE    259
#define KERNEL_KEY_TAB       260
#define KERNEL_KEY_DELETE    261
#define KERNEL_KEY_UP        262
#define KERNEL_KEY_DOWN      263
#define KERNEL_KEY_LEFT      264
#define KERNEL_KEY_RIGHT     265

#endif
```

- [ ] **Step 2: Extend the HAL interface**

Append to `v2/core/hal/hal_input.h` (after the existing `hal_input_poll_touch`
declaration):

```c
/* Polls the keyboard for at most one fresh key-press transition since the
 * last call. Unlike hal_input_poll_touch's level-triggered "is it down
 * right now" contract, this is edge-triggered: *pressed is true exactly
 * when a new key was pressed since the last poll (never for a release, and
 * never for a key that was already held down last poll -- no repeat-while-
 * held in this first pass, see the phase 5 keyboard design spec).
 * *keycode is only meaningful when *pressed is true, same convention as
 * hal_input_poll_touch. A keycode is either a plain printable ASCII value
 * (32-126) or one of the KERNEL_KEY_* named constants in hal_keycode.h for
 * non-printable keys. */
void hal_input_poll_key( int * keycode, bool * pressed );
```

- [ ] **Step 3: `hw` stub**

Append to `v2/hw/main/hal_input_hw.c` (after the existing `hal_input_poll_touch`):

```c
void
hal_input_poll_key( int * keycode, bool * pressed )
{
    ( void ) keycode;
    *pressed = false;
}
```

- [ ] **Step 4: `sim` implementation**

Create `v2/sim/hal_input_sim.cpp`. Uses `SDL_GetKeyboardState` (a live snapshot array,
*not* `SDL_PollEvent` -- see the design spec's "SDL_GetKeyboardState, not a second
SDL_PollEvent consumer" decision for why a second `SDL_PollEvent` loop would race
`Panel_sdl::_event_proc`'s own):

```cpp
#include <SDL.h>

extern "C" {
#include "../core/hal/hal_keycode.h"
}

#define KEY_QUEUE_CAP 16

static int s_key_queue[ KEY_QUEUE_CAP ];
static int s_queue_head = 0;
static int s_queue_tail = 0;
static Uint8 s_prev_state[ SDL_NUM_SCANCODES ];

static void
queue_push( int code )
{
    int next = ( s_queue_tail + 1 ) % KEY_QUEUE_CAP;
    if( next == s_queue_head )
    {
        return; /* full: drop this transition, same best-effort tradeoff as
                  * every other high-frequency input path in this project
                  * (kernel_router.c's non-blocking send_event) */
    }
    s_key_queue[ s_queue_tail ] = code;
    s_queue_tail = next;
}

static bool
queue_pop( int * code )
{
    if( s_queue_head == s_queue_tail )
    {
        return false;
    }
    *code = s_key_queue[ s_queue_head ];
    s_queue_head = ( s_queue_head + 1 ) % KEY_QUEUE_CAP;
    return true;
}

/* Translates one SDL scancode (a physical key position) to this project's
 * keycode vocabulary (hal_keycode.h), resolving Shift at translate time so
 * every caller downstream (kernel_router, Ruby apps) only ever sees a
 * resolved character or a named KERNEL_KEY_* constant, never a raw
 * scancode or a modifier bit to interpret itself. Returns 0 for anything
 * unmapped (silently ignored -- e.g. function keys, Ctrl/Alt, which this
 * first pass doesn't support, see the phase 5 keyboard design spec). */
static int
translate_scancode( SDL_Scancode sc, bool shift )
{
    if( sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z )
    {
        int letter = 'a' + ( sc - SDL_SCANCODE_A );
        return shift ? ( letter - 32 ) : letter;
    }

    if( sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9 )
    {
        static const char unshifted[] = "123456789";
        static const char shifted[]   = "!@#$%^&*(";
        int i = sc - SDL_SCANCODE_1;
        return shift ? shifted[ i ] : unshifted[ i ];
    }
    if( sc == SDL_SCANCODE_0 )
    {
        return shift ? ')' : '0';
    }

    switch( sc )
    {
        case SDL_SCANCODE_SPACE:        return ' ';
        case SDL_SCANCODE_RETURN:       return KERNEL_KEY_ENTER;
        case SDL_SCANCODE_KP_ENTER:     return KERNEL_KEY_ENTER;
        case SDL_SCANCODE_BACKSPACE:    return KERNEL_KEY_BACKSPACE;
        case SDL_SCANCODE_ESCAPE:       return KERNEL_KEY_ESCAPE;
        case SDL_SCANCODE_TAB:          return KERNEL_KEY_TAB;
        case SDL_SCANCODE_DELETE:       return KERNEL_KEY_DELETE;
        case SDL_SCANCODE_UP:           return KERNEL_KEY_UP;
        case SDL_SCANCODE_DOWN:         return KERNEL_KEY_DOWN;
        case SDL_SCANCODE_LEFT:         return KERNEL_KEY_LEFT;
        case SDL_SCANCODE_RIGHT:        return KERNEL_KEY_RIGHT;
        case SDL_SCANCODE_MINUS:        return shift ? '_' : '-';
        case SDL_SCANCODE_EQUALS:       return shift ? '+' : '=';
        case SDL_SCANCODE_LEFTBRACKET:  return shift ? '{' : '[';
        case SDL_SCANCODE_RIGHTBRACKET: return shift ? '}' : ']';
        case SDL_SCANCODE_BACKSLASH:    return shift ? '|' : '\\';
        case SDL_SCANCODE_SEMICOLON:    return shift ? ':' : ';';
        case SDL_SCANCODE_APOSTROPHE:   return shift ? '"' : '\'';
        case SDL_SCANCODE_COMMA:        return shift ? '<' : ',';
        case SDL_SCANCODE_PERIOD:       return shift ? '>' : '.';
        case SDL_SCANCODE_SLASH:        return shift ? '?' : '/';
        case SDL_SCANCODE_GRAVE:        return shift ? '~' : '`';
        default:                        return 0;
    }
}

/* SDL_GetKeyboardState's backing array is kept current by Panel_sdl's own
 * SDL_PollEvent loop, which runs continuously on the main thread
 * (Panel_sdl::loop -> _event_proc). Reading it here, from the FreeRTOS
 * thread, is the same category of cross-thread read hal_input_poll_touch's
 * sim implementation already relies on for monitor.touch_x/y/touched (set
 * by that same main-thread event loop) -- no new synchronization is
 * introduced here because none was needed there either. */
static void
key_scan( void )
{
    int numkeys = 0;
    const Uint8 * state = SDL_GetKeyboardState( &numkeys );
    bool shift = state[ SDL_SCANCODE_LSHIFT ] || state[ SDL_SCANCODE_RSHIFT ];

    int limit = numkeys < SDL_NUM_SCANCODES ? numkeys : SDL_NUM_SCANCODES;
    for( int i = 0; i < limit; i++ )
    {
        bool down = state[ i ] != 0;
        bool was_down = s_prev_state[ i ] != 0;
        if( down && !was_down )
        {
            int code = translate_scancode( ( SDL_Scancode ) i, shift );
            if( code != 0 )
            {
                queue_push( code );
            }
        }
        s_prev_state[ i ] = state[ i ];
    }
}

extern "C" void
hal_input_poll_key( int * keycode, bool * pressed )
{
    key_scan();
    int code;
    if( queue_pop( &code ) )
    {
        *keycode = code;
        *pressed = true;
    }
    else
    {
        *pressed = false;
    }
}
```

- [ ] **Step 5: Add the new sim source file to the build**

In `v2/sim/CMakeLists.txt`, add `hal_input_sim.cpp` to the `add_executable(acidos_sim
...)` source list, right after the existing `hal_display_sim.cpp`:

```
add_executable(acidos_sim
    sim_main.c
    hal_display_sim.cpp
    hal_input_sim.cpp
    hal_audio_sim.c
    main.cpp
```

- [ ] **Step 6: New event type**

In `v2/core/kernel/kernel_event.h`, change:

```c
enum kernel_event_type
{
    KERNEL_EVENT_TOUCH = 0,
    KERNEL_EVENT_CLOSE = 1,
    KERNEL_EVENT_MOVED = 2
};
```

to:

```c
enum kernel_event_type
{
    KERNEL_EVENT_TOUCH = 0,
    KERNEL_EVENT_CLOSE = 1,
    KERNEL_EVENT_MOVED = 2,
    /* x carries the keycode (hal_keycode.h), pressed is always 1 (only
     * presses are ever generated this phase, see hal_input_poll_key's
     * edge-triggered contract), y is unused. */
    KERNEL_EVENT_KEY = 3
};
```

- [ ] **Step 7: Router focus model + key routing**

In `v2/core/kernel/kernel_router.h`, add (after `kernel_router_set_desktop_task`):

```c
/* Raises a window to front AND makes it the keyboard focus target, in one
 * step -- the one function both a direct click on a window (this file's
 * own fresh_press handling) and a taskbar tap (Task 3's acid_activate_
 * window binding) call, so there is exactly one rule for how a window
 * gains keyboard focus, not two competing ones. */
void kernel_router_activate_window( void * task );
```

In `v2/core/kernel/kernel_router.c`:

Add a new static near the top, alongside the existing statics:

```c
static void * g_focus_task = NULL;
```

Add the new function (after `kernel_router_set_desktop_task`, before
`send_event_timeout`):

```c
void
kernel_router_activate_window( void * task )
{
    kernel_window_bring_to_front( task );
    g_focus_task = task;
}
```

Change the `fresh_press` branch's window-raise call from:

```c
        kernel_window_bring_to_front( win->task );
```

to:

```c
        kernel_router_activate_window( win->task );
```

Change the close-button branch from:

```c
                if( ( dx * dx + dy * dy ) <= ( hit_r * hit_r ) )
                {
                    send_event( win, KERNEL_EVENT_CLOSE, 0, 0, 0 );
                    kernel_window_unregister( win->task );
                    return;
                }
```

to:

```c
                if( ( dx * dx + dy * dy ) <= ( hit_r * hit_r ) )
                {
                    send_event( win, KERNEL_EVENT_CLOSE, 0, 0, 0 );
                    kernel_window_unregister( win->task );
                    if( g_focus_task == win->task )
                    {
                        /* Don't leave a dead task handle as the keyboard
                         * focus target. A later fresh press elsewhere, or
                         * a taskbar tap (Task 3), will pick a real one;
                         * until then key events are simply dropped -- the
                         * same "no window, no-op" behavior every other
                         * nowhere-to-deliver input path in this file
                         * already has. */
                        g_focus_task = NULL;
                    }
                    return;
                }
```

Change `kernel_router_poll`'s opening lines from:

```c
static void
kernel_router_poll( void )
{
    int x, y;
    bool pressed;
    hal_input_poll_touch( &x, &y, &pressed );
```

to:

```c
static void
kernel_router_poll( void )
{
    int key_code;
    bool key_pressed;
    hal_input_poll_key( &key_code, &key_pressed );
    if( key_pressed && g_focus_task != NULL )
    {
        struct kernel_window * focus_win = kernel_window_by_task( g_focus_task );
        if( focus_win != NULL )
        {
            send_event( focus_win, KERNEL_EVENT_KEY, key_code, 0, 1 );
        }
    }

    int x, y;
    bool pressed;
    hal_input_poll_touch( &x, &y, &pressed );
```

(Key polling runs unconditionally, first, every tick -- before any of the touch state
machine's own early returns -- so an in-progress touch drag targeting one window never
suppresses keyboard delivery to a *different* focused window.)

- [ ] **Step 8: Build**

```bash
cd v2/sim/build && cmake --build . -j4
```
Expected: builds clean, no warnings/errors.

- [ ] **Step 9: Real verification — temporary stderr instrumentation + real key injection**

This step verifies the HAL-through-router chain before Task 2 adds the Ruby-visible
binding that would otherwise be the only way to observe it. Temporarily add one
`fprintf` line in `v2/core/kernel/kernel_router.c` right after the new
`send_event( focus_win, KERNEL_EVENT_KEY, key_code, 0, 1 );` call (NOT committed):

```c
            fprintf( stderr, "PHASE5_KEY_ROUTED code=%d\n", key_code );
```

`kernel_router.c` does not currently include `<stdio.h>` (confirmed: `grep -n stdio
v2/core/kernel/kernel_router.c` returns nothing) -- add `#include <stdio.h>` at the
top of the file for this temporary step, and remove it again in Step 10 along with the
`fprintf` line itself.

Temporarily add a `kernel_router_activate_window` call is not needed for this test --
instead, temporarily hardcode `g_focus_task` to the desktop task for this one test run
only, by adding this line right after `kernel_router_set_desktop_task( desktop_task );`
in `v2/sim/sim_main.c` (NOT committed):

```c
    kernel_router_activate_window( desktop_task );
```

Build and run under Xvfb, then inject real key presses at the live window with
`xdotool key` (which the X server delivers as real X11 key events; SDL translates
those to `SDL_KEYDOWN`/`SDL_KEYUP`, which `Panel_sdl::_event_proc`'s own
`SDL_PollEvent` loop keeps `SDL_GetKeyboardState`'s snapshot current for):

```bash
cd v2/sim/build && cmake --build . -j4
Xvfb :91 -screen 0 320x240x24 &
sleep 1
DISPLAY=:91 ./acidos_sim > /tmp/phase5_key_test.log 2>&1 &
sleep 1.5
WIN=$(DISPLAY=:91 xdotool search --name "LGFX Simulator")
DISPLAY=:91 xdotool key --window "$WIN" a
sleep 0.3
DISPLAY=:91 xdotool key --window "$WIN" shift+a
sleep 0.3
DISPLAY=:91 xdotool key --window "$WIN" Return
sleep 0.3
kill %1 %2
grep PHASE5_KEY_ROUTED /tmp/phase5_key_test.log
```

Expected output: three `PHASE5_KEY_ROUTED code=N` lines -- `code=97` ('a'), `code=65`
('A', confirming Shift resolution), `code=257` (`KERNEL_KEY_ENTER`). If no lines
appear, check `xdotool search --name "LGFX Simulator"` actually found the window
first (same troubleshooting note as every prior phase's screenshot steps).

- [ ] **Step 10: Clean up the temporary instrumentation**

Revert the temporary `fprintf` line (and its `#include <stdio.h>` if added) in
`kernel_router.c`, and the temporary `kernel_router_activate_window( desktop_task );`
line in `sim_main.c`. Confirm:

```bash
git status
git diff --stat
```
Only the files listed under **Files** above should show as changed/new.

- [ ] **Step 11: Commit**

```bash
git add v2/core/hal/hal_keycode.h v2/sim/hal_input_sim.cpp v2/core/hal/hal_input.h \
        v2/hw/main/hal_input_hw.c v2/core/kernel/kernel_event.h \
        v2/core/kernel/kernel_router.h v2/core/kernel/kernel_router.c \
        v2/sim/CMakeLists.txt
git commit -m "v2: keyboard HAL, KERNEL_EVENT_KEY, click-to-focus routing"
```

---

### Task 2: Ruby keyboard surface — `acid_keys.rb`, `on_key`/`on_idle`, binding

**Files:**
- Create: `v2/apps/lib/acid_keys.rb`
- Modify: `v2/core/bindings/event_binding.c`
- Modify: `v2/apps/lib/acid_app.rb`
- Modify: `v2/apps/lib/acid_game.rb`
- Modify: `v2/core/vm_host/vm_host.c`

**Interfaces:**
- Consumes: `KERNEL_EVENT_KEY` (Task 1).
- Produces: `module AcidKeys` constants; `acid_poll_event` returning `[:key, code,
  pressed]` for a key event; `AcidApp#on_key(code, pressed)`/`#on_idle` hooks (default
  no-op), dispatched from both `AcidApp#start` and `AcidGame#start`.

- [ ] **Step 1: `AcidKeys` constants**

Create `v2/apps/lib/acid_keys.rb`. Values must stay numerically identical to
`v2/core/hal/hal_keycode.h`'s `KERNEL_KEY_*` (see Task 1, Global Constraints):

```ruby
module AcidKeys
  ENTER     = 257
  BACKSPACE = 258
  ESCAPE    = 259
  TAB       = 260
  DELETE    = 261
  UP        = 262
  DOWN      = 263
  LEFT      = 264
  RIGHT     = 265
end
```

- [ ] **Step 2: `acid_poll_event`'s key-event branch**

In `v2/core/bindings/event_binding.c`, insert a new branch after the existing
`KERNEL_EVENT_MOVED` check and before the final generic (touch) return:

```c
    if( ev.type == KERNEL_EVENT_MOVED )
    {
        ctx->window_x = ev.x;
        ctx->window_y = ev.y;
        return mrb_symbol_value( mrb_intern_cstr( mrb, "moved" ) );
    }

    if( ev.type == KERNEL_EVENT_KEY )
    {
        mrb_value values[ 3 ];
        values[ 0 ] = mrb_symbol_value( mrb_intern_cstr( mrb, "key" ) );
        values[ 1 ] = mrb_fixnum_value( ev.x );
        values[ 2 ] = mrb_bool_value( ev.pressed != 0 );
        return mrb_ary_new_from_values( mrb, 3, values );
    }

    mrb_value values[ 3 ];
    values[ 0 ] = mrb_fixnum_value( ev.x );
    values[ 1 ] = mrb_fixnum_value( ev.y );
    values[ 2 ] = mrb_bool_value( ev.pressed != 0 );
    return mrb_ary_new_from_values( mrb, 3, values );
```

(The file already includes `"mruby/array.h"`, needed for `mrb_ary_new_from_values` --
no new include required.)

- [ ] **Step 3: `AcidApp`'s `on_key`/`on_idle` hooks and dispatch**

In `v2/apps/lib/acid_app.rb`, change:

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
      elsif ev == :moved
        redraw
      elsif ev
        on_touch(ev[0], ev[1], ev[2])
      end
    end
    on_destroy
  end
end
```

to:

```ruby
class AcidApp
  def on_create
  end

  def on_touch(x, y, pressed)
  end

  def on_key(code, pressed)
  end

  def on_idle
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
      elsif ev == :moved
        redraw
      elsif ev.is_a?(Array) && ev[0] == :key
        on_key(ev[1], ev[2])
      elsif ev
        on_touch(ev[0], ev[1], ev[2])
      else
        on_idle
      end
    end
    on_destroy
  end
end
```

- [ ] **Step 4: `AcidGame`'s matching dispatch**

`AcidGame` does not subclass `AcidApp#start` (it has its own fixed-tick loop), so it
needs the same `[:key, ...]` dispatch branch added to its own loop -- its `on_key`
default still comes from `AcidApp` via inheritance, only the dispatch is duplicated
here, matching this file's pre-existing duplication of `:close`/`:moved` handling (not
new duplication this task introduces). `AcidGame` does not get its own `on_idle`: its
`on_tick` already fires unconditionally every tick, which already is its idle tick.

In `v2/apps/lib/acid_game.rb`, change:

```ruby
      ev = acid_poll_event(remaining_ms)
      if ev == :close
        running = false
      elsif ev == :moved
        # No-op: unlike AcidApp, a game redraws its whole scene every
        # tick (on_tick's contract, see the design spec), so a stale
        # chrome position after a drag self-corrects on the very next
        # tick without a special case here.
      elsif ev
        on_touch(ev[0], ev[1], ev[2])
      end
```

to:

```ruby
      ev = acid_poll_event(remaining_ms)
      if ev == :close
        running = false
      elsif ev == :moved
        # No-op: unlike AcidApp, a game redraws its whole scene every
        # tick (on_tick's contract, see the design spec), so a stale
        # chrome position after a drag self-corrects on the very next
        # tick without a special case here.
      elsif ev.is_a?(Array) && ev[0] == :key
        on_key(ev[1], ev[2])
      elsif ev
        on_touch(ev[0], ev[1], ev[2])
      end
```

- [ ] **Step 5: Wire `acid_keys.rb` into `vm_host.c`**

In `v2/core/vm_host/vm_host.c`, add a third path constant:

```c
#define ACID_APP_LIB_PATH "v2/apps/lib/acid_app.rb"
#define ACID_GAME_LIB_PATH "v2/apps/lib/acid_game.rb"
#define ACID_KEYS_LIB_PATH "v2/apps/lib/acid_keys.rb"
```

And load it first, before `AcidApp`/`AcidGame` (order doesn't functionally matter here
-- constants have no load-time dependency on the classes -- but constants-then-base-
classes-then-app-code reads clearest):

```c
    load_file_into_vm( mrb, cxt, ACID_KEYS_LIB_PATH );
    load_file_into_vm( mrb, cxt, ACID_APP_LIB_PATH );
    load_file_into_vm( mrb, cxt, ACID_GAME_LIB_PATH );
    load_file_into_vm( mrb, cxt, params->script_path );
```

- [ ] **Step 6: Build**

```bash
cd v2/sim/build && cmake --build . -j4
```

- [ ] **Step 7: Real verification — a temp app that echoes keystrokes on screen**

Create a temporary throwaway app (NOT committed) at `v2/apps/_test_on_key.rb`:

```ruby
class TestOnKey < AcidApp
  def on_create
    @last = "none"
  end

  def on_key(code, pressed)
    @last = "#{code}:#{pressed}"
    redraw
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame
    acid_draw_text(@last, 4, 20, 0xD4E6DB, 0x050607)
  end
end
TestOnKey.new.start
```

Temporarily add to `v2/sim/sim_main.c` (after the existing spawn calls, NOT
committed):

```c
    kernel_spawn_app( "v2/apps/_test_on_key.rb", 20, 40, 120, 100, 1 );
```

Build, run, click the window to focus it (Task 1 gives the click-to-focus behavior
already, via `kernel_router_activate_window` in the `fresh_press` path -- clicking the
test window's body focuses it), then send a real keystroke and screenshot:

```bash
cd v2/sim/build && cmake --build . -j4
Xvfb :92 -screen 0 320x240x24 &
sleep 1
DISPLAY=:92 ./acidos_sim &
sleep 1.5
WIN=$(DISPLAY=:92 xdotool search --name "LGFX Simulator")
# Click inside the test window's body (window at (20,40), well below its
# title bar) to focus it.
DISPLAY=:92 xdotool mousemove --window "$WIN" 60 90 click 1
sleep 0.3
DISPLAY=:92 xdotool key --window "$WIN" x
sleep 0.3
DISPLAY=:92 import -window "$WIN" /tmp/onkey_shot.png
# Text drawn at local (4,20) in a window at (20,40) -> absolute (24,60)-ish.
convert /tmp/onkey_shot.png -crop 1x1+28+64 txt:-
kill %1 %2
```

Expected: the pixel is not the untouched background color `#050607` (text was drawn --
`on_key` fired with `code=120` ('x'), `pressed=true`, proving the whole HAL -> router
-> binding -> `AcidApp#on_key` chain works end to end).

- [ ] **Step 8: Clean up**

Revert the temporary `sim_main.c` line, delete `v2/apps/_test_on_key.rb`.

```bash
git status
git diff --stat
```
Confirm only the five files under **Files** above show as changed/new.

- [ ] **Step 9: Commit**

```bash
git add v2/apps/lib/acid_keys.rb v2/core/bindings/event_binding.c \
        v2/apps/lib/acid_app.rb v2/apps/lib/acid_game.rb v2/core/vm_host/vm_host.c
git commit -m "v2: acid_keys.rb, on_key/on_idle hooks, key-event dispatch"
```

---

### Task 3: Window enumeration + activation bindings

**Files:**
- Modify: `v2/core/kernel/kernel_router.h`
- Modify: `v2/core/kernel/kernel_router.c`
- Create: `v2/core/bindings/window_binding.h`
- Create: `v2/core/bindings/window_binding.c`
- Modify: `v2/core/vm_host/vm_host.c`
- Modify: `v2/sim/CMakeLists.txt`
- Modify: `v2/hw/main/CMakeLists.txt`

**Interfaces:**
- Consumes: `kernel_window_at_index`/`KERNEL_WINDOW_MAX` (existing, `kernel_window.h`),
  `kernel_router_activate_window` (Task 1).
- Produces: `void * kernel_router_get_focus(void)`; Ruby `acid_window_max()`,
  `acid_window_info(i)` -> `nil` or `[name, x, y, w, h, focused]`,
  `acid_activate_window(i)`.

- [ ] **Step 1: Focus getter**

In `v2/core/kernel/kernel_router.h`, add:

```c
/* Which window's task currently has keyboard focus, or NULL if none does
 * (nothing has been clicked/activated yet). Used by window_binding.c so
 * Ruby can report a window's focused state (the taskbar highlights it). */
void * kernel_router_get_focus( void );
```

In `v2/core/kernel/kernel_router.c`, add (after `kernel_router_activate_window`):

```c
void *
kernel_router_get_focus( void )
{
    return g_focus_task;
}
```

- [ ] **Step 2: New binding module**

Create `v2/core/bindings/window_binding.h`:

```c
#ifndef ACID_WINDOW_BINDING_H
#define ACID_WINDOW_BINDING_H

#include "mruby.h"

void acid_window_bindings_register( mrb_state * mrb );

#endif
```

Create `v2/core/bindings/window_binding.c`:

```c
#include "window_binding.h"
#include "../kernel/kernel_window.h"
#include "../kernel/kernel_router.h"

#include "mruby/array.h"

static mrb_value
acid_window_max( mrb_state * mrb, mrb_value self )
{
    ( void ) mrb;
    ( void ) self;
    return mrb_fixnum_value( KERNEL_WINDOW_MAX );
}

static mrb_value
acid_window_info( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int index;
    mrb_get_args( mrb, "i", &index );

    if( index < 0 || index >= KERNEL_WINDOW_MAX )
    {
        return mrb_nil_value();
    }

    struct kernel_window * win = kernel_window_at_index( ( int ) index );
    if( win == NULL || !win->in_use )
    {
        return mrb_nil_value();
    }

    mrb_value values[ 6 ];
    values[ 0 ] = mrb_str_new_cstr( mrb, win->app_name );
    values[ 1 ] = mrb_fixnum_value( win->x );
    values[ 2 ] = mrb_fixnum_value( win->y );
    values[ 3 ] = mrb_fixnum_value( win->w );
    values[ 4 ] = mrb_fixnum_value( win->h );
    values[ 5 ] = mrb_bool_value( win->task == kernel_router_get_focus() );
    return mrb_ary_new_from_values( mrb, 6, values );
}

static mrb_value
acid_activate_window( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int index;
    mrb_get_args( mrb, "i", &index );

    if( index < 0 || index >= KERNEL_WINDOW_MAX )
    {
        return mrb_nil_value();
    }

    struct kernel_window * win = kernel_window_at_index( ( int ) index );
    if( win != NULL && win->in_use )
    {
        kernel_router_activate_window( win->task );
    }
    return mrb_nil_value();
}

void
acid_window_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_window_max",
                                 acid_window_max, MRB_ARGS_NONE() );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_window_info",
                                 acid_window_info, MRB_ARGS_REQ( 1 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_activate_window",
                                 acid_activate_window, MRB_ARGS_REQ( 1 ) );
}
```

- [ ] **Step 3: Register in `vm_host.c`**

In `v2/core/vm_host/vm_host.c`, add the include (alongside the existing binding
includes):

```c
#include "../bindings/window_binding.h"
```

And the registration call (alongside the existing ones, in `vm_host_task`):

```c
    acid_bindings_register( mrb );
    acid_event_bindings_register( mrb );
    acid_chrome_bindings_register( mrb );
    acid_audio_bindings_register( mrb );
    acid_window_bindings_register( mrb );
```

- [ ] **Step 4: Add the new binding source to both builds**

In `v2/sim/CMakeLists.txt`, add to `add_executable(acidos_sim ...)`'s source list
(alongside the other binding `.c` files):

```
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/bindings/window_binding.c
```

In `v2/hw/main/CMakeLists.txt`, add to the `SRCS` list (alongside the other binding
`.c` files):

```
        ../../core/bindings/window_binding.c
```

- [ ] **Step 5: Build**

```bash
cd v2/sim/build && cmake --build . -j4
```

- [ ] **Step 6: Real verification**

Create a temporary throwaway app (NOT committed) at `v2/apps/_test_window_binding.rb`:

```ruby
class TestWindowBinding < AcidApp
  def on_create
    lines = []
    i = 0
    while i < acid_window_max
      info = acid_window_info(i)
      lines << "#{i}:#{info[0]}" if info
      i += 1
    end
    @summary = lines.join(" ")
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame
    acid_draw_text(@summary[0, 30], 4, 20, 0xD4E6DB, 0x050607)
  end
end
TestWindowBinding.new.start
```

Temporarily add to `v2/sim/sim_main.c` (after the existing spawn calls, NOT
committed):

```c
    kernel_spawn_app( "v2/apps/_test_window_binding.rb", 20, 40, 140, 100, 1 );
```

Build, run, screenshot, and confirm the summary text (listing every currently-spawned
window's `app_name`, including this test app's own) was actually drawn:

```bash
cd v2/sim/build && cmake --build . -j4
Xvfb :93 -screen 0 320x240x24 &
sleep 1
DISPLAY=:93 ./acidos_sim &
sleep 1.5
WIN=$(DISPLAY=:93 xdotool search --name "LGFX Simulator")
DISPLAY=:93 import -window "$WIN" /tmp/winfo_shot.png
convert /tmp/winfo_shot.png -crop 1x1+28+64 txt:-
kill %1 %2
```

Expected: not the untouched background color `#050607` (the summary text was drawn,
proving `acid_window_max`/`acid_window_info` returned real data from the live window
list -- at minimum the desktop, `demo_touch`, `demo_swatch`, `acid_blaster`, and this
test app itself, five entries).

Also verify `acid_activate_window`: from the same running session, before killing it,
click a different window (e.g. `demo_swatch` at its default position) to focus it,
confirm via `acid_window_info`'s `focused` flag would flip -- this is most simply
re-verified together with Task 4's own taskbar-driven activation test, so a full
standalone check here is not required as long as this task's build and the summary
check above both pass; Task 4 exercises `acid_activate_window` for real via the
taskbar UI.

- [ ] **Step 7: Clean up**

Revert the temporary `sim_main.c` line, delete `v2/apps/_test_window_binding.rb`.

```bash
git status
git diff --stat
```
Confirm only the seven files under **Files** above show as changed/new.

- [ ] **Step 8: Commit**

```bash
git add v2/core/kernel/kernel_router.h v2/core/kernel/kernel_router.c \
        v2/core/bindings/window_binding.h v2/core/bindings/window_binding.c \
        v2/core/vm_host/vm_host.c v2/sim/CMakeLists.txt v2/hw/main/CMakeLists.txt
git commit -m "v2: window enumeration/activation bindings for the desktop shell"
```

---

### Task 4: Desktop shell — real taskbar

**Files:**
- Modify: `v2/core/kernel/kernel_layout.h`
- Modify: `v2/apps/desktop.rb`
- Modify: `v2/sim/sim_main.c`
- Modify: `v2/hw/main/app_main.c`

**Interfaces:**
- Consumes: `acid_window_max`/`acid_window_info`/`acid_activate_window` (Task 3),
  `AcidApp#on_idle` (Task 2), `acid_draw_desktop_strip`/`acid_fill_rect`/
  `acid_draw_text` (existing).
- Produces: a real taskbar replacing the placeholder strip.

- [ ] **Step 1: Taller desktop strip**

In `v2/core/kernel/kernel_layout.h`, change:

```c
/* The desktop's own top strip is a separate, larger concept (Task 7) --
 * defined here now so Task 5's close-button geometry and this one never
 * get confused with each other. */
#define KERNEL_DESKTOP_STRIP_H 20
```

to:

```c
/* The desktop's own top strip is a separate, larger concept from the
 * per-window title bar above -- kept as its own constant so the two never
 * get confused with each other. 24px (not the original 20) leaves real
 * room for phase 5's taskbar buttons (label + padding), not just a bare
 * accent bar. */
#define KERNEL_DESKTOP_STRIP_H 24
```

- [ ] **Step 2: Match the desktop's own spawn height**

In `v2/sim/sim_main.c`, change:

```c
    void * desktop_task = kernel_spawn_app( "v2/apps/desktop.rb", 0, 0, 320, 20, 0 );
```

to:

```c
    void * desktop_task = kernel_spawn_app( "v2/apps/desktop.rb", 0, 0, 320, 24, 0 );
```

In `v2/hw/main/app_main.c`, make the identical change:

```c
    void * desktop_task = kernel_spawn_app( "v2/apps/desktop.rb", 0, 0, 320, 24, 0 );
```

- [ ] **Step 3: Rewrite the desktop app as a taskbar**

Replace `v2/apps/desktop.rb` entirely:

```ruby
class DesktopApp < AcidApp
  # Must match the kernel_spawn_app(...) script_path that spawns this app
  # (sim_main.c / app_main.c) -- app_name is literally that path
  # (kernel_spawn.c registers it verbatim), which is how the taskbar
  # recognizes and skips its own window.
  MY_APP_NAME = "v2/apps/desktop.rb"

  BUTTON_W = 60
  BUTTON_H = 18
  BUTTON_MARGIN_X = 2
  BUTTON_MARGIN_Y = 2

  BG_COLOR = 0x0B1712      # THEME_PANEL
  ACCENT_COLOR = 0x00FF66  # THEME_HARD
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  TEXT_DARK = 0x050607     # THEME_BG -- used as the label color on an
                           # accent-filled focused button, for contrast
                           # (mirrors docs/BUILD_LOG.md's documented
                           # "pressed state inverts to a solid --hard
                           # fill, label switches to a dark color" rule).

  # No on_create override needed: AcidApp#start already calls redraw once,
  # automatically, right after on_create -- DesktopApp has no other setup
  # to do (active_windows is computed fresh on every redraw, not cached),
  # matching file_manager.rb/editor.rb/acid_blaster.rb's own convention of
  # never calling redraw from inside on_create itself.

  def on_idle
    # Nothing has to touch the desktop strip itself for the taskbar to go
    # stale -- clicking directly from one app window to another is the
    # common case. Redrawing on every idle timeout (roughly 5Hz, the
    # existing 200ms acid_poll_event timeout) keeps the focus highlight
    # and window list live without a dedicated notification channel.
    redraw
  end

  def redraw
    acid_draw_desktop_strip
    active_windows.each_with_index do |entry, slot|
      draw_button(slot, entry[1])
    end
  end

  def on_touch(x, y, pressed)
    return unless pressed
    slot = x / BUTTON_W
    entry = active_windows[slot]
    return unless entry
    acid_activate_window(entry[0])
    redraw
  end

  private

  # [[kernel_index, info], ...] for every in-use window except this one.
  # Recomputed on every call rather than cached -- at most 8 entries, and
  # avoids any staleness between what's drawn and what a tap acts on.
  def active_windows
    list = []
    i = 0
    max = acid_window_max
    while i < max
      info = acid_window_info(i)
      list << [i, info] if info && info[0] != MY_APP_NAME
      i += 1
    end
    list
  end

  def draw_button(slot, info)
    name = info[0]
    focused = info[5]
    x = slot * BUTTON_W + BUTTON_MARGIN_X
    y = BUTTON_MARGIN_Y
    w = BUTTON_W - BUTTON_MARGIN_X * 2
    h = BUTTON_H
    bg = focused ? ACCENT_COLOR : BG_COLOR
    fg = focused ? TEXT_DARK : TEXT_COLOR
    acid_fill_rect(x, y, w, h, bg)
    acid_draw_text(short_name(name), x + 3, y + 5, fg, bg)
  end

  # app_name is a full script path (e.g. "v2/apps/acid_blaster.rb") -- show
  # just the filename, stripped of directory and extension, truncated to
  # fit the button.
  def short_name(path)
    slash = path.rindex("/")
    base = slash ? path[slash + 1, path.length - slash - 1] : path
    dot = base.rindex(".")
    base = base[0, dot] if dot
    base = base[0, 8] if base.length > 8
    base
  end
end

DesktopApp.new.start
```

- [ ] **Step 4: Build**

```bash
cd v2/sim/build && cmake --build . -j4
```

- [ ] **Step 5: Real verification — taskbar shows real apps and switches focus**

```bash
Xvfb :94 -screen 0 320x240x24 &
sleep 1
DISPLAY=:94 v2/sim/build/acidos_sim &
sleep 1.5
WIN=$(DISPLAY=:94 xdotool search --name "LGFX Simulator")
DISPLAY=:94 import -window "$WIN" /tmp/taskbar_shot1.png
```

Confirm at least one taskbar button's background is drawn (not still the bare
`THEME_PANEL` strip with nothing on it) by checking a pixel inside where the first
button's fill should be (slot 0, `x=2..58`, `y=2..20` -- check the middle,
`(30, 11)`):

```bash
convert /tmp/taskbar_shot1.png -crop 1x1+30+11 txt:-
```
Expected: `#0b1712` (`THEME_PANEL`, an unfocused button's fill) -- confirms a button
was drawn there at all, distinct from the strip's own bare background one pixel above
the button row would also show (both are `THEME_PANEL` in this state since nothing has
been clicked yet, so this alone only confirms geometry; the focus check below is the
real proof).

Click a specific app window directly (not via the taskbar) to focus it -- e.g.
`demo_swatch` at its known default spawn position (160, 70) -- then confirm the
taskbar redrew (via its `on_idle` tick) to show that button highlighted:

```bash
DISPLAY=:94 xdotool mousemove --window "$WIN" 200 100 click 1
sleep 1.0  # give on_idle's ~200ms poll timeout time to fire and redraw
DISPLAY=:94 import -window "$WIN" /tmp/taskbar_shot2.png
```

Find `demo_swatch`'s button slot from the taskbar's button order (it's the second app
spawned after the desktop in `sim_main.c`, so likely slot 1, `x=62..118`) and confirm
its fill is now the accent color:

```bash
convert /tmp/taskbar_shot2.png -crop 1x1+90+11 txt:-
```
Expected: `#00ff66` (`THEME_HARD`/`ACCENT_COLOR`) -- confirms `focused` flowed from a
direct click through `kernel_router_activate_window` to `acid_window_info`'s returned
`focused` flag to the taskbar's redraw. If the slot guessed above isn't the right one,
scan the button row (`y=11`, `x=0..320` in steps of `BUTTON_W`) for the accent-colored
pixel instead of assuming the position.

Now click that same app's taskbar button instead of the app window directly, then
confirm the app itself received focus by typing into it (reuses Task 2's `on_key`
verification technique, applied here to prove the *taskbar path* specifically, not
just the direct-click path already proven above) -- or, simpler and sufficient: click
a *different* app's taskbar button and confirm the highlighted slot moves accordingly
in a third screenshot, proving `acid_activate_window` (not just direct clicks) drives
focus:

```bash
DISPLAY=:94 xdotool mousemove --window "$WIN" 30 11 click 1   # tap slot 0's button
sleep 1.0
DISPLAY=:94 import -window "$WIN" /tmp/taskbar_shot3.png
convert /tmp/taskbar_shot3.png -crop 1x1+30+11 txt:-  # slot 0 should now be accent
convert /tmp/taskbar_shot3.png -crop 1x1+90+11 txt:-  # slot 1 (demo_swatch) should be back to panel
kill %1 %2
```

Expected: slot 0 now reads `#00ff66` and slot 1 has reverted to `#0b1712` --
confirming `acid_activate_window` (the taskbar-tap path) moved focus exactly like a
direct click does.

- [ ] **Step 6: Commit**

```bash
git add v2/core/kernel/kernel_layout.h v2/apps/desktop.rb \
        v2/sim/sim_main.c v2/hw/main/app_main.c
git commit -m "v2: real taskbar desktop shell, replacing the placeholder strip"
```

---

### Task 5: File manager

**Files:**
- Create: `v2/home/README.txt`
- Create: `v2/home/notes.txt`
- Create: `v2/apps/file_manager.rb`

**Interfaces:**
- Consumes: `File`/`Dir` (already present in this build, confirmed by live testing
  during design -- see spec), `AcidKeys` (Task 2), `acid_fill_rect`/`acid_draw_text`/
  `acid_clear_user_area`/`acid_draw_window_frame` (existing).
- Produces: a working file browser + text preview app. Not wired into boot yet --
  Task 7.

- [ ] **Step 1: Seed content**

Create `v2/home/README.txt`:

```
acid OS v2 -- home directory

This is the file manager's browsing root.
Open notes.txt from the file manager, or edit it directly with the editor app.
```

Create `v2/home/notes.txt`:

```
acid OS v2 notes

Type here. Press ESC to save.
```

- [ ] **Step 2: Write the app**

Create `v2/apps/file_manager.rb`:

```ruby
class FileManagerApp < AcidApp
  # Must match the kernel_spawn_app(...) call that spawns this app (Task 7)
  # and kernel_layout.h's KERNEL_TITLE_BAR_H.
  WINDOW_W = 220
  WINDOW_H = 160
  TITLE_BAR_H = 16
  ROW_H = 12
  ROOT_DIR = "v2/home"

  BG_COLOR = 0x0B1712      # THEME_PANEL -- header row
  BODY_BG = 0x050607       # THEME_BG -- list/preview rows
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  DIR_COLOR = 0x00FF66     # THEME_HARD -- accent for directory entries
  SEL_BG = 0x123322        # THEME_PANEL's documented button-hover shade,
                           # reused for the selected-row highlight

  def on_create
    @dir = ROOT_DIR
    @entries = []
    @selected = 0
    @preview = nil       # nil = browsing; a String = previewing this
                          # file's content
    @preview_name = nil
    scan_dir
  end

  def scan_dir
    @entries = []
    @entries << { name: "..", dir: true, size: 0 } unless @dir == ROOT_DIR
    begin
      d = Dir.open(@dir)
      names = []
      while (ent = d.read)
        names << ent unless ent == "." || ent == ".."
      end
      d.close
      names.sort.each do |name|
        path = "#{@dir}/#{name}"
        is_dir = false
        size = 0
        begin
          sub = Dir.open(path)
          sub.close
          is_dir = true
        rescue
          begin
            size = File.size(path)
          rescue
            size = 0
          end
        end
        @entries << { name: name, dir: is_dir, size: size }
      end
    rescue => e
      @entries << { name: "(error: #{e.message})", dir: false, size: 0 }
    end
    @selected = 0
  end

  def visible_rows
    (WINDOW_H - TITLE_BAR_H) / ROW_H
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame
    if @preview
      draw_preview
    else
      draw_listing
    end
  end

  def draw_listing
    y = TITLE_BAR_H
    acid_fill_rect(0, y, WINDOW_W, ROW_H, BG_COLOR)
    acid_draw_text(@dir, 2, y + 2, TEXT_COLOR, BG_COLOR)
    y += ROW_H
    i = 0
    while i < @entries.length && i < visible_rows - 1
      e = @entries[i]
      row_bg = (i == @selected) ? SEL_BG : BODY_BG
      acid_fill_rect(0, y, WINDOW_W, ROW_H, row_bg)
      label = e[:dir] ? "[#{e[:name]}]" : " #{e[:name]} (#{e[:size]}B)"
      color = e[:dir] ? DIR_COLOR : TEXT_COLOR
      acid_draw_text(label[0, 34], 2, y + 2, color, row_bg)
      y += ROW_H
      i += 1
    end
  end

  def draw_preview
    acid_fill_rect(0, TITLE_BAR_H, WINDOW_W, ROW_H, BG_COLOR)
    acid_draw_text(@preview_name, 2, TITLE_BAR_H + 2, TEXT_COLOR, BG_COLOR)
    lines = @preview.split("\n")
    y = TITLE_BAR_H + ROW_H
    i = 0
    while i < lines.length && i < visible_rows - 1
      acid_fill_rect(0, y, WINDOW_W, ROW_H, BODY_BG)
      acid_draw_text(lines[i][0, 34], 2, y + 2, TEXT_COLOR, BODY_BG)
      y += ROW_H
      i += 1
    end
  end

  def on_touch(x, y, pressed)
    return unless pressed
    if @preview
      @preview = nil
      redraw
      return
    end
    row = (y - TITLE_BAR_H) / ROW_H - 1
    return if row < 0 || row >= @entries.length
    @selected = row
    activate_selected
  end

  def on_key(code, pressed)
    return unless pressed
    if @preview
      if code == AcidKeys::ESCAPE
        @preview = nil
        redraw
      end
      return
    end
    if code == AcidKeys::UP
      @selected -= 1 if @selected > 0
      redraw
    elsif code == AcidKeys::DOWN
      @selected += 1 if @selected < @entries.length - 1
      redraw
    elsif code == AcidKeys::ENTER
      activate_selected
    elsif code == AcidKeys::BACKSPACE
      go_up
    end
  end

  def activate_selected
    entry = @entries[@selected]
    return unless entry
    if entry[:name] == ".."
      go_up
    elsif entry[:dir]
      @dir = "#{@dir}/#{entry[:name]}"
      scan_dir
      redraw
    else
      open_preview(entry[:name])
    end
  end

  def go_up
    return if @dir == ROOT_DIR
    slash = @dir.rindex("/")
    @dir = slash ? @dir[0, slash] : ROOT_DIR
    scan_dir
    redraw
  end

  def open_preview(name)
    path = "#{@dir}/#{name}"
    begin
      f = File.open(path, "r")
      @preview = f.read
      f.close
      @preview_name = name
    rescue => e
      @preview = "(cannot open: #{e.message})"
      @preview_name = name
    end
    redraw
  end
end

FileManagerApp.new.start
```

- [ ] **Step 3: Build**

```bash
cd v2/sim/build && cmake --build . -j4
```

- [ ] **Step 4: Real verification — browse, preview, keyboard nav**

Temporarily add to `v2/sim/sim_main.c` (after the existing spawn calls, NOT
committed):

```c
    kernel_spawn_app( "v2/apps/file_manager.rb", 40, 50, 220, 160, 1 );
```

```bash
cd v2/sim/build && cmake --build . -j4
Xvfb :95 -screen 0 320x240x24 &
sleep 1
DISPLAY=:95 ./acidos_sim &
sleep 1.5
WIN=$(DISPLAY=:95 xdotool search --name "LGFX Simulator")
DISPLAY=:95 import -window "$WIN" /tmp/fmgr_shot1.png
```

Confirm the listing shows real entries (`README.txt`/`notes.txt`, and the directory
header text) by checking that the header row's text pixel isn't the untouched
background -- header text is at local `(2, 18)` in a window at `(40, 50)` ->
absolute `(42, 68)`-ish:

```bash
convert /tmp/fmgr_shot1.png -crop 1x1+46+70 txt:-
```
Expected: not `#0b1712` unchanged-fill (text was drawn on top of the header's panel
background).

Click on the `notes.txt` row (second listed entry after `README.txt`, roughly row 1,
around local `y = 16 + 12*2 = 40` -> absolute `y = 90`) to open its preview, and
confirm the preview shows real file content:

```bash
DISPLAY=:95 xdotool mousemove --window "$WIN" 60 90 click 1
sleep 0.3
DISPLAY=:95 import -window "$WIN" /tmp/fmgr_shot2.png
convert /tmp/fmgr_shot2.png -crop 1x1+46+70 txt:-
kill %1 %2
```
Expected: not the raw `THEME_BG`/`THEME_PANEL` unchanged fill (the preview header --
the filename -- was drawn). If the row tapped wasn't actually `notes.txt` (listing
order may put `README.txt` first alphabetically, `notes.txt` second, or vice versa --
confirm from `/tmp/fmgr_shot1.png` which row is which before tapping), adjust the
tapped y-coordinate accordingly rather than assuming.

Separately, confirm keyboard navigation works: relaunch fresh, click the window to
focus it, then send Down/Down/Enter/Escape via `xdotool key` and confirm the same
preview-opens-then-closes behavior via two more screenshots, the same technique as
above but driven by `AcidKeys::DOWN`/`ENTER`/`ESCAPE` instead of taps.

- [ ] **Step 5: Clean up**

Revert the temporary `sim_main.c` line.

```bash
git status
git diff --stat
```
Confirm only `v2/home/README.txt`, `v2/home/notes.txt`, `v2/apps/file_manager.rb` show
as new.

- [ ] **Step 6: Commit**

```bash
git add v2/home/README.txt v2/home/notes.txt v2/apps/file_manager.rb
git commit -m "v2: file manager app -- browse and preview v2/home"
```

---

### Task 6: Text editor

**Files:**
- Create: `v2/apps/editor.rb`

**Interfaces:**
- Consumes: `File` (existing), `AcidKeys` (Task 2), `acid_fill_rect`/`acid_draw_text`/
  `acid_clear_user_area`/`acid_draw_window_frame` (existing).
- Produces: a working single-file load/edit/save text editor, always operating on
  `v2/home/notes.txt` (Task 5's seed file). Not wired into boot yet -- Task 7.

- [ ] **Step 1: Write the app**

Create `v2/apps/editor.rb`:

```ruby
class EditorApp < AcidApp
  # Must match the kernel_spawn_app(...) call that spawns this app (Task 7)
  # and kernel_layout.h's KERNEL_TITLE_BAR_H.
  WINDOW_W = 240
  WINDOW_H = 170
  TITLE_BAR_H = 16
  LINE_H = 10
  EDIT_FILE = "v2/home/notes.txt"

  BG_COLOR = 0x0B1712      # THEME_PANEL -- status line
  BODY_BG = 0x050607       # THEME_BG -- text body
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  CURSOR_COLOR = 0x00FF66  # THEME_HARD
  STATUS_COLOR = 0x9DAAA3  # THEME_MUTED

  def on_create
    load_file
    @cx = 0
    @cy = 0
    @scroll_y = 0
    @status = "ESC=save  #{@lines.length} lines"
  end

  def load_file
    begin
      f = File.open(EDIT_FILE, "r")
      text = f.read
      f.close
      @lines = text.split("\n")
    rescue
      @lines = [""]
    end
    @lines = [""] if @lines.empty?
  end

  def save_file
    begin
      f = File.open(EDIT_FILE, "w")
      f.write(@lines.join("\n"))
      f.close
      @status = "saved (#{@lines.length} lines)"
    rescue => e
      @status = "save failed: #{e.message}"
    end
  end

  def visible_lines
    (WINDOW_H - TITLE_BAR_H - LINE_H) / LINE_H
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame
    draw_status
    draw_lines
    draw_cursor
  end

  def draw_status
    acid_fill_rect(0, TITLE_BAR_H, WINDOW_W, LINE_H, BG_COLOR)
    acid_draw_text(@status, 2, TITLE_BAR_H + 1, STATUS_COLOR, BG_COLOR)
  end

  def draw_lines
    y = TITLE_BAR_H + LINE_H
    i = 0
    while i < visible_lines
      idx = @scroll_y + i
      text = idx < @lines.length ? @lines[idx] : ""
      acid_fill_rect(0, y, WINDOW_W, LINE_H, BODY_BG)
      acid_draw_text(text[0, 38], 2, y + 1, TEXT_COLOR, BODY_BG)
      y += LINE_H
      i += 1
    end
  end

  def draw_cursor
    row = @cy - @scroll_y
    return if row < 0 || row >= visible_lines
    x = 2 + @cx * 6
    y = TITLE_BAR_H + LINE_H + row * LINE_H
    acid_fill_rect(x, y + LINE_H - 2, 6, 2, CURSOR_COLOR)
  end

  def on_key(code, pressed)
    return unless pressed
    if code == AcidKeys::ESCAPE
      save_file
    elsif code == AcidKeys::UP
      move_cursor(0, -1)
    elsif code == AcidKeys::DOWN
      move_cursor(0, 1)
    elsif code == AcidKeys::LEFT
      move_cursor(-1, 0)
    elsif code == AcidKeys::RIGHT
      move_cursor(1, 0)
    elsif code == AcidKeys::ENTER
      split_line
    elsif code == AcidKeys::BACKSPACE
      backspace
    elsif code >= 32 && code <= 126
      insert_char(code)
    end
    ensure_scroll
    redraw
  end

  def current_line
    @lines[@cy]
  end

  def move_cursor(dx, dy)
    if dy != 0
      @cy += dy
      @cy = 0 if @cy < 0
      @cy = @lines.length - 1 if @cy >= @lines.length
      @cx = current_line.length if @cx > current_line.length
    end
    if dx != 0
      @cx += dx
      if @cx < 0
        if @cy > 0
          @cy -= 1
          @cx = current_line.length
        else
          @cx = 0
        end
      elsif @cx > current_line.length
        if @cy < @lines.length - 1
          @cy += 1
          @cx = 0
        else
          @cx = current_line.length
        end
      end
    end
  end

  def insert_char(code)
    line = current_line
    ch = code.chr
    @lines[@cy] = line[0, @cx] + ch + line[@cx, line.length - @cx]
    @cx += 1
  end

  def split_line
    line = current_line
    before = line[0, @cx]
    after = line[@cx, line.length - @cx]
    @lines[@cy] = before
    @lines.insert(@cy + 1, after)
    @cy += 1
    @cx = 0
  end

  def backspace
    if @cx > 0
      line = current_line
      @lines[@cy] = line[0, @cx - 1] + line[@cx, line.length - @cx]
      @cx -= 1
    elsif @cy > 0
      prev_len = @lines[@cy - 1].length
      @lines[@cy - 1] = @lines[@cy - 1] + @lines[@cy]
      @lines.delete_at(@cy)
      @cy -= 1
      @cx = prev_len
    end
  end

  def ensure_scroll
    if @cy < @scroll_y
      @scroll_y = @cy
    elsif @cy >= @scroll_y + visible_lines
      @scroll_y = @cy - visible_lines + 1
    end
  end
end

EditorApp.new.start
```

- [ ] **Step 2: Build**

```bash
cd v2/sim/build && cmake --build . -j4
```

- [ ] **Step 3: Real verification — load, type, save, confirm on disk**

Temporarily add to `v2/sim/sim_main.c` (after the existing spawn calls, NOT
committed):

```c
    kernel_spawn_app( "v2/apps/editor.rb", 60, 60, 240, 170, 1 );
```

```bash
cd v2/sim/build && cmake --build . -j4
Xvfb :96 -screen 0 320x240x24 &
sleep 1
DISPLAY=:96 ./acidos_sim &
sleep 1.5
WIN=$(DISPLAY=:96 xdotool search --name "LGFX Simulator")
DISPLAY=:96 import -window "$WIN" /tmp/editor_shot1.png
```

Confirm real seed content loaded (status line shows a line count > 0, body shows
`v2/home/notes.txt`'s real text) by checking the status-line text pixel isn't
untouched panel background -- status at local `(2, 17)` in a window at `(60, 60)` ->
absolute `(62, 77)`-ish:

```bash
convert /tmp/editor_shot1.png -crop 1x1+66+79 txt:-
```
Expected: not `#0b1712` unchanged.

Click the window body to focus it, move down to the last seed line (`DOWN` twice --
`AcidKeys::DOWN` is real, `End`/`Home` are not in this phase's keycode vocabulary, see
`hal_keycode.h`/`translate_scancode`, so navigation in this verification only uses keys
the HAL actually maps), type a marker string, press Escape to save, then screenshot:

```bash
DISPLAY=:96 xdotool mousemove --window "$WIN" 100 100 click 1
sleep 0.3
DISPLAY=:96 xdotool key --window "$WIN" Down
DISPLAY=:96 xdotool key --window "$WIN" Down
DISPLAY=:96 xdotool type --window "$WIN" "helloacid"
DISPLAY=:96 xdotool key --window "$WIN" Escape
sleep 0.3
DISPLAY=:96 import -window "$WIN" /tmp/editor_shot2.png
kill %1 %2
```

Confirm the save round-tripped to real disk content by reading the file directly (not
through the running process, which has already exited):

```bash
grep helloacid v2/home/notes.txt
```
Expected: one match -- `insert_char` landed the typed text at the cursor's actual
position (beginning of the third seed line, column 0, since neither `Home` nor `End`
exists in this vocabulary and the cursor starts each line-move at whatever column
`move_cursor`'s clamping left it at) — the exact column isn't asserted, only that real
new bytes reached the file on disk. **Revert this test edit before continuing**
(`git checkout -- v2/home/notes.txt`) so Task 5's seed content stays exactly as
committed.

- [ ] **Step 4: Clean up**

Revert the temporary `sim_main.c` line and the test edit to `v2/home/notes.txt`.

```bash
git status
git diff --stat
```
Confirm only `v2/apps/editor.rb` shows as new.

- [ ] **Step 5: Commit**

```bash
git add v2/apps/editor.rb
git commit -m "v2: text editor app -- load/edit/save v2/home/notes.txt"
```

---

### Task 7: Wire into boot sequence (both targets) + full integration verification

**Files:**
- Modify: `v2/sim/sim_main.c`
- Modify: `v2/hw/main/app_main.c`

**Interfaces:**
- Consumes: `v2/apps/file_manager.rb` (Task 5), `v2/apps/editor.rb` (Task 6),
  `kernel_spawn_app` (existing).

- [ ] **Step 1: Wire `sim_main.c`**

In `v2/sim/sim_main.c`, add these two lines permanently, after the existing
`kernel_spawn_app( "v2/apps/acid_blaster.rb", ...)` call and before the
`xTaskCreate( kernel_router_task, ...)` call:

```c
    kernel_spawn_app( "v2/apps/file_manager.rb", 40, 50, 220, 160, 1 );
    kernel_spawn_app( "v2/apps/editor.rb", 60, 60, 240, 170, 1 );
```

- [ ] **Step 2: Wire `app_main.c`**

In `v2/hw/main/app_main.c`, add the identical two lines in the identical position:

```c
    kernel_spawn_app( "v2/apps/file_manager.rb", 40, 50, 220, 160, 1 );
    kernel_spawn_app( "v2/apps/editor.rb", 60, 60, 240, 170, 1 );
```

- [ ] **Step 3: Build `v2/sim`**

```bash
cd v2/sim/build && cmake --build . -j4
```

- [ ] **Step 4: Real verification — full boot, every subsystem, together**

```bash
Xvfb :97 -screen 0 320x240x24 &
sleep 1
DISPLAY=:97 v2/sim/build/acidos_sim &
sleep 1.5
WIN=$(DISPLAY=:97 xdotool search --name "LGFX Simulator")
DISPLAY=:97 import -window "$WIN" /tmp/boot_shot.png
```

Confirm the full boot has six windows total (desktop, `demo_touch`, `demo_swatch`,
`acid_blaster`, `file_manager`, `editor` -- under `KERNEL_WINDOW_MAX`'s cap of 8) and
every pre-existing app still works, spot-checking one pixel from each known region
against its expected color (same technique used throughout this plan and the game
phase's plan):

```bash
# acid_blaster's play area still has at least one enemy (existing behavior unchanged)
convert /tmp/boot_shot.png -crop 250x164+30+56 txt:- | grep -ci "00ff66"
```
Expected: nonzero.

**Taskbar shows five app buttons** (every window except the desktop's own) -- scan the
button row for five distinct button-background regions:

```bash
convert /tmp/boot_shot.png -crop 320x1+0+11 txt:- | grep -ci "0b1712\|00ff66"
```
Expected: a substantial count of matching pixels across the row (five ~56px-wide
button fills back to back).

**Click the editor's taskbar button, type real text, save, confirm on disk** -- the
full stack (taskbar tap -> `acid_activate_window` -> focus -> keyboard routed to the
now-focused editor -> `on_key` -> buffer edit -> Escape -> `File.open(...,"w")`) in one
pass:

```bash
# Find the editor's button slot: it's the 6th app spawned (desktop, demo_touch,
# demo_swatch, acid_blaster, file_manager, editor), so slot 4 (0-indexed, after the
# desktop is excluded) at roughly x = 4*60+30 = 270, y = 11.
DISPLAY=:97 xdotool mousemove --window "$WIN" 270 11 click 1
sleep 0.3
# Down, not End/Home -- neither exists in this phase's keycode vocabulary
# (hal_keycode.h/translate_scancode), same reasoning as Task 6's own
# verification.
DISPLAY=:97 xdotool key --window "$WIN" Down
DISPLAY=:97 xdotool key --window "$WIN" Down
DISPLAY=:97 xdotool type --window "$WIN" "integrationok"
DISPLAY=:97 xdotool key --window "$WIN" Escape
sleep 0.3
kill %1 %2
grep integrationok v2/home/notes.txt
```
Expected: one match -- confirms real new bytes reached `v2/home/notes.txt` via the
full taskbar-tap -> focus -> keyboard -> save pipeline in one pass, the same
"real bytes changed on disk" bar Task 6 already established (exact cursor column not
asserted, same reasoning). **Revert this test edit** (`git checkout --
v2/home/notes.txt`) before finishing.

**Navigate the file manager with the keyboard** (a second, independent exercise of the
same keyboard-focus pipeline, proving it isn't editor-specific): relaunch fresh, tap
the file manager's taskbar button, send `Down`/`Enter`/`Escape` via `xdotool key`,
screenshot before/after to confirm a preview opened and closed, same technique as
Task 5's own verification.

- [ ] **Step 5: Statically verify `v2/hw`**

`idf.py build` cannot run on this machine (no ESP-IDF installed -- same gap as every
prior phase). Instead: re-read the final `app_main.c` and confirm the two new lines'
syntax matches `kernel_spawn_app`'s real signature (`const char*, int, int, int, int,
int`) exactly; confirm `v2/hw/main/CMakeLists.txt` includes every source file every
task in this plan touched (`hal_input_hw.c` already listed and modified in place,
Task 1; `window_binding.c` added in Task 3); and confirm the explicitly-flagged hw gap
from the spec (`File`/`Dir` need a mounted VFS this project's `v2/hw` doesn't set up)
is stated in this plan's own Global Constraints, not silently dropped. State all of
this explicitly in the final report rather than silently skipping it.

- [ ] **Step 6: Commit**

```bash
git add v2/sim/sim_main.c v2/hw/main/app_main.c
git commit -m "v2: wire file manager and editor into boot sequence (sim + hw)"
```
