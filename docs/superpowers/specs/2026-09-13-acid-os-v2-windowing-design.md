# acid OS v2 — windowing/GUI design

Date: 2026-09-13
Status: approved design, pending spec self-review sign-off

## Context

Bring-up (roadmap phase 1) is complete and merged to `main`: a real FreeRTOS
scheduler runs a single mruby VM task (`vm_host_task`) that loads
`v2/apps/hello.rb`, which draws through `core/gfx`/`core/bindings` to a
LovyanGFX-backed display (SDL2 window on `sim`, a stub HAL on `hw`). This spec
covers phase 2: introducing real windowing — multiple concurrent app tasks,
each owning an on-screen window, managed by a kernel that routes input and
tracks geometry but never draws.

**This phase is modeled closely on family-mruby-os's real architecture**, not
the earlier bring-up spec's more abstract gestures at it. During brainstorming
for this phase, the actual `family-mruby/fmruby-core` and
`family-mruby/fmruby-graphics-audio` repositories were cloned and read
directly (not just their docs/blog posts, as bring-up had done) —
specifically `main/prebuild_scripts/kernel/fmrb_kernel/{window_manager,
input_router, app_lifecycle}.rb` and `lib/add/picoruby-fmrb-app/mrblib/
fmrb-app.rb`. Facts below marked **[confirmed]** come directly from reading
that source; this project's own choices are marked **[this project]**.

**[confirmed]** family-mruby-os's kernel never draws: it holds a plain window
list (array of hashes: `pid, app_name, x, y, width, height, z_order,
resizable, min_width, min_height`) used only for hit-testing, z-order, and
input routing. Apps are separate FreeRTOS tasks; each owns its entire
rectangular canvas *including its own title bar and close button* — there is
no separate window-manager-drawn chrome layer. Concretely, every app inherits
a shared `FmrbApp` base class whose `draw_window_frame`/`clear_user_area`
methods (backed by a native "frame block" draw-command batch) paint the title
bar, rounded corners, and close button; apps call these, they never hand-paint
chrome pixels themselves. Theme colors (`theme_bg`/`theme_fg`/`theme_accent`/
`theme_border`) come from one central `system_conf.toml`, so the whole OS's
chrome restyles from one file. A privileged `system_desktop` app (not special
kernel code) owns a top menu-bar strip; the kernel special-cases only the
*geometry* of that strip (routes clicks there to the desktop pid), not its
drawing. Drag moves a window immediately (kernel updates position directly,
no per-pixel-move round trip to the app); resize is an outline-only live
preview (drawn by the desktop's overlay layer) committed once on mouse-up.
High-frequency pointer-move IPC uses a non-blocking "latch the newest, drop
stale ones" send so a slow/busy app task cannot stall the whole input path.

**[confirmed]** This is also a large, multi-year, feature-accumulated system:
dual Retro (ESP32-S3, NTSC+external WROVER compositor chip, custom
`fmrb_transport` link protocol) / Modern (ESP32-P4, LovyanGFX) hardware
targets, RGB332 hardware-composited canvases, a bytecode "GfxBlock" sprite VM,
fullscreen park/unpark stacks (nested fullscreen apps suspending/resuming each
other), a service-host process with crash-loop restart policy, and multiple
guest language runtimes (mruby, MicroPython, Lua, BASIC, Spinel/native) beside
the primary PicoRuby-based one. **[this project]** None of that accumulated
complexity is in scope for this phase — acid OS v2 targets one board (Tab5,
ESP32-P4, no Retro/dual-chip split), real mruby only (no multi-runtime
support), and this phase proves the *core shape* (kernel-as-router, apps own
their canvas via a shared base-class helper, plain-data window list, IPC-based
input routing) with everything family-mruby-os built on top of that shape
over time deliberately deferred. See "Explicitly deferred" below for the full
cut list.

**[this project]** Visual identity: acid OS v2 does not adopt
family-mruby-os's own plain Windows-3.1/FM-TOWNS-inspired look (as their own
site describes it), nor invent a new palette from scratch. It reuses RaveOS
v1's existing, already-documented palette (`docs/BUILD_LOG.md`): `--bg
#050607` (near-black desktop/page background), `--hard #00ff66` (pure acid
green — accent, borders, pressed states, cursor), a brightened `--panel`
(`0x0B1712` window body; `0x0A1A12`–`0x123322` button idle/hover, brightened
by hand from the literal alpha-composite because it read as invisible on a
flat renderer), and `--text`/`--muted` (`0xD4E6DB`/`0x9DAAA3`). v2 visually
continues v1's identity rather than starting a new one.

## Decisions made during brainstorming

- **Scope:** multi-app now, not single-app-first. The kernel can spawn 2+
  mruby app tasks, each owning a window; hit-testing, z-order, and IPC
  routing are for-real exercised in this phase, not deferred to a later one.
- **Desktop:** a minimal `system_desktop`-equivalent app exists — a real,
  privileged app task owning a thin top strip (acid-green accent bar; a
  clock is a reasonable "something to draw" but not a requirement) — but no
  taskbar, no launcher, no dropdown menus yet.
- **Chrome drawing:** a shared Ruby base class (this project's `AcidApp`,
  mirroring `FmrbApp`) provides `draw_window_frame`/theme accessors; no app
  hand-paints its own title bar.
- **Palette:** RaveOS v1's exact colors (see Context), not a new one.
- **Input model [this project, verified against vendored source]:**
  single-pointer touch, not family-mruby-os's mouse+keyboard+wheel model.
  Tab5 is a touchscreen device; LovyanGFX's public `LGFX_Device::getTouch
  (touch_point_t *tp, uint8_t count = 1)` (declared
  `v2/components/lovyangfx/src/lgfx/v1/LGFXBase.hpp:1465`) is real, stable
  API, and on the `sim` target it is already backed by SDL mouse events with
  zero extra glue needed — confirmed by reading
  `Panel_sdl::getTouchRaw()` (`v2/components/lovyangfx/src/lgfx/v1/
  platforms/sdl/Panel_sdl.cpp:460`), which maps `SDL_MOUSEBUTTONDOWN/UP/
  MOTION` to a touch point (`monitor.touch_x/y`, `monitor.touched`). One
  touch point, pressed/moved/released, is this phase's entire input
  vocabulary — no mouse wheel, no keyboard, no multi-touch.
- **YAGNI cut — resize:** not in this phase. Family-mruby-os's resize
  (outline-only live preview drawn by the desktop overlay, committed on
  release, per-app minimum size) proves nothing new about the core
  architecture that drag-to-move and click-to-close don't already prove, and
  adds real complexity (a second desktop-overlay draw path, minimum-size
  negotiation). Windows in this phase have a fixed size set at spawn time.
  Revisit once an app actually wants to be resizable.
- **YAGNI cut — keyboard:** not in this phase, for the same reason as
  resize, plus Tab5 has no built-in physical keyboard. Revisit once an app
  needs text input (e.g. a future terminal/editor).

## Explicitly deferred (not this phase, not necessarily ever for this project)

Fullscreen mode and its park/unpark stack; a service-host process with
crash-restart policy; the taskbar, launcher, and any dropdown/menu overlay;
multiple guest-language runtimes (this project is mruby-only, decided in
bring-up); the GfxBlock bytecode sprite VM and sprite system generally;
hardware-composited canvas regions / viewport scrolling (`fmrb_gfx_set_
composite_regions`, `fmrb_gfx_set_canvas_viewport` — Modern/P4-PPA-specific
optimizations family-mruby-os built for their dual-chip Retro/Modern split,
which does not exist in this project's single-board target); rounded window
corners (family-mruby-os's `CORNER_R = 4` + color-key transparency dance —
cosmetic, revisit once the base chrome works); window resize; keyboard
input; audio (already out of scope per the bring-up spec, still true here).

## Architecture

Everything here is target-agnostic C living in `v2/core/`, extending (not
replacing) bring-up's `core/vm_host/`, `core/gfx/`, `core/bindings/`,
`core/hal/` — both targets (`sim`, `hw`) share it unchanged, per the
established HAL-boundary pattern.

**`core/kernel/` (new).** Three cooperating modules, deliberately kept as
plain C structs/arrays rather than a message-passing protocol of their own
(FreeRTOS's native primitives already do that job — see IPC below):

- `kernel_window.{c,h}`: the window list. A fixed-size array (small, e.g. 8
  entries — this phase has at most 3 windows: desktop + 2 demo apps) of
  `struct kernel_window { TaskHandle_t task; const char *app_name; int x, y,
  w, h; int z_order; bool closable; }`. Functions: register/unregister a
  window, `kernel_window_find_at(x, y)` (hit-test, mirrors
  `find_window_at` — desktop's strip special-cased the same way: any y less
  than the strip height routes to the desktop regardless of z-order),
  `kernel_window_bring_to_front(task)` (reassigns z-order), `kernel_window_
  by_task(task)`.
- `kernel_router.{c,h}`: owns polling `hal_input_poll` (see HAL extension
  below) once per kernel tick, hit-testing the result against the window
  list, translating the touch point to window-relative coordinates, and
  posting it to the target window's input queue (see IPC below). Mirrors
  `input_router.rb`'s coordinate-translation and drag-tracking logic
  (`@capture_mode`/`@capture_task` equivalent — a single in-flight drag
  target, since there is only one touch point to track), minus everything
  resize-related (cut, see above).
- `kernel_spawn.{c,h}`: `kernel_spawn_app(const char *script_path, int x, int
  y, int w, int h, bool closable)` — creates a FreeRTOS task running a fresh
  mruby VM against the given script, creates that task's input queue, and
  registers it in the window list. Returns the task handle (this project's
  pid equivalent — no separate integer pid namespace is needed while
  `TaskHandle_t` itself is a stable, comparable identity). Each spawned
  task's own C host loop is a real generalization of bring-up's
  `vm_host_task`, not a reuse of it unchanged: bring-up's version loaded a
  script that did all its drawing once and then only parked, watching for
  quit. This phase's version loads the script once (which defines an
  `AcidApp` subclass and instantiates/starts it — an initial
  `draw_window_frame` + `clear_user_area` call included), then loops:
  drain the task's touch queue, translate a delivered event into a call
  into the live mruby VM instance (`mrb_funcall` against the app instance's
  `on_touch`), and watch for the close message (see Data flow below) to
  end the loop and close the VM. The exact mruby C API for calling a method
  on an already-running instance from the host loop (as opposed to
  bring-up's one-shot `mrb_load_detect_file_cxt`) needs verifying against
  the vendored `mruby.h`/`mruby/value.h` when this phase is planned, the
  same way bring-up verified its own mruby API calls before writing them
  into a plan.

**IPC — FreeRTOS queues, not a custom transport.** Family-mruby-os's
`fmrb_transport` exists because their Retro target's app tasks and the
kernel/render pipeline run in different memory domains across a physical
link to a second WROVER chip. Nothing here has that constraint — kernel task
and every app task share one address space in one FreeRTOS instance on both
`sim` and `hw`. Each spawned app task gets one `QueueHandle_t` (created via
FreeRTOS's own `xQueueCreate`, sized for a handful of pending touch events)
that the kernel posts window-relative touch events to
(`xQueueSend`/non-blocking `xQueueSendToBack` with a zero timeout for the
same "latch the newest, drop stale ones under load" reason family-mruby-os
gives) and the app task's own event loop drains
(`xQueueReceive`) between draw calls.

**`core/hal/hal_input.h` (extended, not replaced).** Bring-up's
`hal_input_should_quit()` stays (it is `sim`-only window-close plumbing, not
part of the app-facing input model). Add `hal_input_poll_touch(int *x, int
*y, bool *pressed)` — `sim`'s implementation (in `v2/sim/`, alongside the
existing `hal_display_sim.cpp`) calls the LovyanGFX instance's own
`getTouch()` (verified above); `hw`'s stub implementation (extending
`v2/hw/main/hal_input_hw.c`) always reports not-pressed, same honest-stub
pattern bring-up already established for display, since Tab5's real touch
driver is real hardware-bring-up scope (roadmap phase 5), not this phase.

**Ruby-side app framework — `v2/apps/lib/acid_app.rb` (new).** This
project's `AcidApp` base class, mirroring `FmrbApp`'s shape but only the
parts this phase needs: `draw_window_frame` (title bar height, close button
circle in the top-right, both in the v1-derived palette),
`clear_user_area`, `theme_bg`/`theme_fg`/`theme_accent`/`theme_border`
reading from one small Ruby constants module (this phase's equivalent of
`system_conf.toml` — a file, not a runtime-configurable setting yet), and an
`on_touch(x, y, pressed)` callback apps override. Every app script
(`v2/apps/desktop.rb` and at least one more demonstrating multi-window,
extending or replacing bring-up's `hello.rb`) requires this file and
subclasses `AcidApp`.

**`core/bindings/` (extended).** New mruby bindings alongside bring-up's
`acid_fill_rect`: something callable from `draw_window_frame` to draw the
chrome (a rect + a small circle primitive is enough — LovyanGFX's own
`fillRect`/`fillCircle` already exist behind the HAL, so this is a thin
binding, not new drawing logic), and a binding exposing the app's own
window geometry (`w`, `h`) so `AcidApp` can compute where the title bar and
close button sit without the kernel pushing that down separately.

## Data flow — one input event

1. `kernel_router`'s poll calls `hal_input_poll_touch`, gets `(x, y,
   pressed)`.
2. If `y` is inside the desktop strip's height (and no drag is in
   progress), the event routes to the desktop's queue unconditionally —
   mirrors `find_window_at`'s desktop-strip special case.
3. Otherwise, `kernel_window_find_at(x, y)` walks the window list
   front-to-back by z-order (skipping none — there is no suspended/parked
   state in this phase) and returns the topmost window under the point, or
   none.
4. On a fresh press inside a window's own top title-bar strip (not its
   close-button corner): start a drag, remembering the offset — subsequent
   moves while still pressed update that window's position directly
   (`kernel_window` fields), no round-trip to the app, exactly matching
   family-mruby-os's "drag moves the window immediately" behavior.
5. On a fresh press inside the close-button corner: unregister the window
   from the list immediately (so it stops receiving further input and
   disappears from hit-testing), then post a close event to that app's
   queue instead of deleting its task from the kernel side. This matters
   for a real reason, not just fidelity to the reference: the app task's C
   host loop (this phase's per-app generalization of bring-up's
   `vm_host_task`, which already owns `mrb_open`/`mrb_close` around the
   mruby VM it hosts) is the only code in a position to close that VM
   safely — a kernel-side `vTaskDelete` on a task with a live VM would skip
   `mrb_close()` and leak its heap. The close event is a message the C host
   loop itself recognizes (same loop that already drains the touch queue
   and drives `on_touch`/redraw calls into the VM via `mrb_funcall`); on
   seeing it, the loop stops calling into Ruby, calls `mrb_close()` from C
   (outside any in-flight `mrb_funcall`, so this is safe), and ends the
   task with `vTaskDelete(NULL)` — the standard, safe FreeRTOS
   self-deletion pattern. `AcidApp` itself needs no close-handling code;
   the C host loop never hands the VM a callback for an event it isn't
   going to survive.
6. Any other press/move/release inside a window's body: translate to
   window-relative coordinates and post to that window's queue; the app's
   own loop calls `AcidApp#on_touch` with those coordinates on its next
   iteration.

## Error handling

An app task's mruby VM faulting (parse error, unhandled exception) already
degrades to a parked loop rather than taking the process down — this is
bring-up's existing `vm_host_task` behavior (see `core/vm_host/vm_host.c`),
unchanged by this phase. What's new: since multiple app tasks now exist
side by side, one task parking must not affect the kernel task's own
routing loop or any other app task — this falls out naturally from each app
being its own FreeRTOS task with its own queue, not from any new code this
phase adds. No auto-restart, no crash-loop policy (family-mruby-os's
service-host concept) — explicitly deferred, per Context.

## Testing / definition of done

Same discipline as bring-up: no automated test suite, "it draws the thing
and responds to real input" verified by running it, on `sim` (this phase's
target; `hw`'s scaffold gets the same kernel/router/spawn code added so it
is not left behind, but actual hardware verification stays blocked on
ESP-IDF access, same honest gap bring-up already carries forward).

Done when, on `sim`:
- The kernel spawns the desktop app plus at least two more app windows at
  distinct positions, all visible at once with correct z-order (the most
  recently focused one drawn on top).
- Each window shows real chrome (title bar, close button) in the v1-derived
  palette, drawn via `AcidApp#draw_window_frame` — not hand-painted per app.
- Clicking (mouse, simulating touch, same as bring-up's Xvfb-based
  verification) a window's title bar and dragging moves only that window;
  the other windows do not move and keep receiving their own input
  correctly afterward.
- Clicking a window's close-button corner removes that window and its task
  cleanly (no crash, no hang) — the remaining windows keep working.
- Clicking inside a window's body (not the title bar or close corner)
  reaches that specific app's `on_touch` with correct window-relative
  coordinates — demonstrated by each demo app doing something visibly
  different on touch (mirroring bring-up's own hello.rb-draws-two-rects
  proof, but interactive this time).

## Roadmap (unchanged from bring-up, this is phase 2 of 6)

1. Bring-up (done, merged)
2. Windowing/GUI framework (this spec)
3. Audio subsystem — port `synth.c`'s DSP
4. First real app/game end-to-end
5. Real M5Stack Tab5 hardware bring-up (flash + run)
6. Custom cyberdeck hardware
