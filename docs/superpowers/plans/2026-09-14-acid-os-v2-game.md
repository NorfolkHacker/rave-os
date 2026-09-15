# acid OS v2: First Real App/Game (Phase 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ship "Acid Blaster," a small tap-to-shoot survival arcade game, as
acid OS v2's first real Ruby app — exercising the windowing (phase 2) and
audio (phase 3) subsystems together from an actual app, running unmodified
on both `v2/sim` and `v2/hw`.

**Architecture:** two new Ruby-facing gfx bindings (`acid_fill_circle`,
`acid_draw_text`); a new `AcidGame < AcidApp` base class providing a
fixed-tick (`TICK_MS = 50`) game loop, loaded into every app VM via a new
`vm_host.c` load call (this project's mruby build has no `require`); the
game itself (`v2/apps/acid_blaster.rb`) built on top of both, in two passes
(mechanics first, then audio); wired into both targets' boot sequences.

**Tech Stack:** C (bindings/HAL, both `v2/sim` and ESP-IDF `v2/hw`), mruby
(app-side Ruby), LovyanGFX (`drawString`/`setTextColor` for the new text
primitive), the project's own `synth.c`-backed audio API from phase 3.

**Spec:** `docs/superpowers/specs/2026-09-14-acid-os-v2-arcade-game-design.md`

## Global Constraints

- No RaveOS v1 files (`kernel/`, `boot/`, `programs/`, `demos/`) touched.
- `v2/apps/lib/acid_app.rb` (`AcidApp`) is not modified — `AcidGame` is a
  separate, new base class in its own file.
- No changes to the audio phase's Ruby API — `acid_play_note`/
  `acid_stop_note` used exactly as they already exist.
- New Ruby-facing gfx bindings are exactly two: `acid_fill_circle`,
  `acid_draw_text`. Nothing else added to the binding surface — in
  particular, no "ask my own window size" binding; the game hardcodes its
  own window dimensions as constants instead (see Task 3).
- `v2/hw` gets the same source changes added to its build
  (`v2/hw/main/CMakeLists.txt` needs no new *files* added, since every
  file touched by this plan already exists in both builds — only
  `v2/hw/main/app_main.c` needs a new boot-sequence spawn call, Task 5) and
  the same boot-sequence spawn call as `v2/sim`, matching every prior
  phase's pattern. `idf.py build` is not runnable on this machine (no
  ESP-IDF installed) — same pre-existing gap as every prior phase; changes
  here are statically verified only.
- Fixed-tick game logic uses integer arithmetic for all per-tick movement,
  hit-testing, and difficulty math (hit-testing and the "reached center"
  check both use squared-distance comparison, no `sqrt`). One narrow
  exception: a newly spawned enemy's direction vector is normalized once,
  at spawn time only, via `Math.sqrt` (`mruby`'s `math` gembox is confirmed
  present in this project's actual build, pulled in by `default.gembox`).
- This project's mruby build has **no `require`/`require_relative`** gem
  (confirmed in `vm_host.c`'s own comment) — shared library files are
  loaded into every app's VM via sequential `load_file_into_vm` calls in
  `vm_host.c`, not by Ruby code. **Correction from this plan's original
  text**: `puts`/`print`/`p` ARE available — they're provided by
  `mruby-io` (not `mruby-print`, which genuinely doesn't exist in this
  vendored source, but `mruby-io` does and is pulled in by
  `stdlib-io.gembox` via `default.gembox`), confirmed by printing real
  output from inside a live app VM during this branch's final review.
  Earlier tasks in this plan verified everything visually (screenshot +
  pixel inspection) because that error sent them looking for it in the
  wrong place — future work can use `puts` for debug output directly.
- Real verification only, matching every prior phase's discipline: builds
  that succeed and processes that don't crash are never sufficient evidence
  on their own. This plan's tasks use real screenshots (Xvfb + `xdotool` +
  ImageMagick's `import`/`convert`) and, for Task 4, the same SDL2 `disk`
  audio driver PCM-capture technique the audio phase (phase 3) already
  proved out. The screenshot pipeline below was independently verified
  working on this exact machine before this plan was written:
  `Xvfb :N -screen 0 320x240x24`, then run `acidos_sim` with
  `DISPLAY=:N`, then `xdotool search --name "LGFX Simulator"` reliably
  finds the one SDL window (confirmed by its real window title, not
  guessed), then `import -window "$WIN" shot.png` captures it, then
  `convert shot.png -crop 1x1+X+Y txt:-` reads any one pixel's color.
  **Correction from this plan's original text**: `kernel_spawn_app`'s
  script paths (e.g. `"v2/apps/lib/acid_app.rb"`) resolve relative to the
  process's own current working directory, not the binary's location —
  the binary (`v2/sim/build/acidos_sim`) must be launched from the repo
  root (e.g. `v2/sim/build/acidos_sim &`, not `cd v2/sim/build &&
  ./acidos_sim &`), or every app fails to load silently and the window
  stays blank. Confirmed during this branch's final review.
- Every task must leave `git status` clean of anything but its own intended
  changes — no temporary test apps, no leftover `sim_main.c`/`app_main.c`
  edits, no stray screenshot files, committed by accident.

---

### Task 1: New gfx bindings — `acid_fill_circle`, `acid_draw_text`

**Files:**
- Modify: `v2/core/gfx/gfx.h`
- Modify: `v2/core/gfx/gfx.c`
- Modify: `v2/core/hal/hal_display.h`
- Modify: `v2/sim/hal_display_sim.cpp`
- Modify: `v2/hw/main/hal_display_hw.c`
- Modify: `v2/core/bindings/gfx_binding.c`

**Interfaces:**
- Consumes: `gfx_fill_circle(int,int,int,unsigned int)` (already exists,
  from the windowing phase, in `v2/core/gfx/gfx.h`/`.c` and
  `v2/core/hal/hal_display.h`).
- Produces: `void gfx_draw_text(int x, int y, const char *str, unsigned int
  fg, unsigned int bg)` and `void hal_display_draw_text(int x, int y, const
  char *str, unsigned int fg, unsigned int bg)`; Ruby-callable
  `acid_fill_circle(x, y, r, color)` and `acid_draw_text(str, x, y,
  fg_color, bg_color)`, both registered as `mrb_define_module_function` on
  `mrb->kernel_module`, matching every existing binding's exact pattern.

- [ ] **Step 1: Add `gfx_draw_text` to the gfx layer**

Append to `v2/core/gfx/gfx.h` (after the existing `gfx_fill_circle`
declaration):

```c
void gfx_draw_text( int x, int y, const char * str, unsigned int fg, unsigned int bg );
```

Append to `v2/core/gfx/gfx.c` (after the existing `gfx_fill_circle`
function):

```c
void
gfx_draw_text( int x, int y, const char * str, unsigned int fg, unsigned int bg )
{
    xSemaphoreTake( g_gfx_lock, portMAX_DELAY );
    hal_display_draw_text( x, y, str, fg, bg );
    xSemaphoreGive( g_gfx_lock );
}
```

- [ ] **Step 2: Add `hal_display_draw_text` to the HAL interface and both targets**

Append to `v2/core/hal/hal_display.h`:

```c
void hal_display_draw_text( int x, int y, const char * str, unsigned int fg, unsigned int bg );
```

Append to `v2/sim/hal_display_sim.cpp` (after the existing
`hal_display_fill_circle` function). LovyanGFX's real API, verified against
`v2/components/lovyangfx/src/lgfx/v1/LGFXBase.hpp` (`setTextColor(fg, bg)`,
`drawString(str, x, y)`) — no `setFont` call needed, this uses the library's
built-in default font:

```cpp
extern "C" void hal_display_draw_text( int x, int y, const char * str, unsigned int fg, unsigned int bg )
{
    lcd.setTextColor( fg, bg );
    lcd.drawString( str, x, y );
}
```

Append to `v2/hw/main/hal_display_hw.c` (after the existing
`hal_display_fill_circle` function), matching that file's exact
`ESP_LOGI`-stub pattern:

```c
void
hal_display_draw_text( int x, int y, const char * str, unsigned int fg, unsigned int bg )
{
    ESP_LOGI( TAG, "hal_display_draw_text(%d, %d, \"%s\", 0x%06x, 0x%06x): stub, not drawn", x, y, str, fg, bg );
}
```

- [ ] **Step 3: Add both Ruby bindings**

In `v2/core/bindings/gfx_binding.c`, add two new static functions (after
the existing `acid_fill_rect`) and register both in
`acid_bindings_register`:

```c
static mrb_value
acid_fill_circle( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    mrb_int x, y, r, color;
    mrb_get_args( mrb, "iiii", &x, &y, &r, &color );
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    gfx_fill_circle( ctx->window_x + ( int ) x, ctx->window_y + ( int ) y,
                      ( int ) r, ( unsigned int ) color );
    return mrb_nil_value();
}

static mrb_value
acid_draw_text( mrb_state * mrb, mrb_value self )
{
    ( void ) self;
    const char * str;
    mrb_int x, y, fg, bg;
    /* 'z' = NUL-terminated const char* (verified against mruby's own
     * mrb_get_args format-specifier doc comment in src/class.c). */
    mrb_get_args( mrb, "ziiii", &str, &x, &y, &fg, &bg );
    struct kernel_app_context * ctx = ( struct kernel_app_context * ) mrb->ud;
    gfx_draw_text( ctx->window_x + ( int ) x, ctx->window_y + ( int ) y,
                   str, ( unsigned int ) fg, ( unsigned int ) bg );
    return mrb_nil_value();
}

void
acid_bindings_register( mrb_state * mrb )
{
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_fill_rect",
                                 acid_fill_rect, MRB_ARGS_REQ( 5 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_fill_circle",
                                 acid_fill_circle, MRB_ARGS_REQ( 4 ) );
    mrb_define_module_function( mrb, mrb->kernel_module, "acid_draw_text",
                                 acid_draw_text, MRB_ARGS_REQ( 5 ) );
}
```

- [ ] **Step 4: Build**

```bash
cd v2/sim/build && cmake --build . -j4
```
Expected: builds clean, no warnings/errors.

- [ ] **Step 5: Real verification — screenshot + pixel check**

Create a temporary throwaway app (NOT committed) at
`v2/apps/_test_gfx_bindings.rb`:

```ruby
class TestGfxBindings < AcidApp
  def on_create
    acid_clear_user_area
    acid_fill_circle(40, 60, 15, 0x00FF66)
    acid_draw_text("HI", 10, 90, 0xD4E6DB, 0x050607)
  end
end
TestGfxBindings.new.start
```

Temporarily add one line to `v2/sim/sim_main.c` right after the existing
`kernel_spawn_app` calls (do not remove or reorder the existing ones):

```c
    kernel_spawn_app( "v2/apps/_test_gfx_bindings.rb", 20, 40, 120, 120, 1 );
```

Build, then run under Xvfb and capture a real screenshot:

```bash
cd v2/sim/build && cmake --build . -j4
Xvfb :91 -screen 0 320x240x24 &
sleep 1
DISPLAY=:91 ./acidos_sim &
sleep 1.5
WIN=$(DISPLAY=:91 xdotool search --name "LGFX Simulator")
DISPLAY=:91 import -window "$WIN" /tmp/gfx_test_shot.png
# Circle center is at local (40,60) inside a window spawned at (20,40) ->
# absolute (60,100). Expect acid green (0x00FF66 = RGB 0,255,102).
convert /tmp/gfx_test_shot.png -crop 1x1+60+100 txt:-
# Text baseline is at local (10,90) inside a window spawned at (20,40) ->
# absolute (30,130)-ish; check a pixel a few px right/down of that origin
# lands on either the text glyph color or its background fill (either
# confirms drawString/setTextColor actually painted something there,
# since an untouched window would show THEME_BG (0x050607) unchanged).
convert /tmp/gfx_test_shot.png -crop 1x1+34+134 txt:-
kill %1 %2
```

Expected: the circle pixel reads `#00ff66` (or very close — anti-aliasing
is not expected from `fillCircle`, so an exact match is reasonable to
require). The text-area pixel should NOT read the untouched background
color `#050607`, confirming `drawString` painted something (exact glyph
pixel color depends on font rendering, so "not still background" is the
right assertion, not an exact color match).

If the window isn't found by that exact `xdotool` command, adjust (e.g.
`xwininfo -root -tree` to inspect what's actually there) — the goal is a
real screenshot of the real running process, not this exact command
sequence.

- [ ] **Step 6: Clean up the temporary test**

Revert the temporary line in `v2/sim/sim_main.c` and delete
`v2/apps/_test_gfx_bindings.rb`. Confirm `git status` shows only the six
files listed under **Files** above as modified — nothing else.

```bash
git status
git diff --stat
```

- [ ] **Step 7: Commit**

```bash
git add v2/core/gfx/gfx.h v2/core/gfx/gfx.c v2/core/hal/hal_display.h \
        v2/sim/hal_display_sim.cpp v2/hw/main/hal_display_hw.c \
        v2/core/bindings/gfx_binding.c
git commit -m "v2: acid_fill_circle/acid_draw_text gfx bindings"
```

---

### Task 2: `AcidGame` base class and `vm_host.c` wiring

**Files:**
- Create: `v2/apps/lib/acid_game.rb`
- Modify: `v2/core/vm_host/vm_host.c`

**Interfaces:**
- Consumes: `AcidApp` (`v2/apps/lib/acid_app.rb`, already exists —
  `on_create`/`on_touch`/`on_destroy` hooks, `acid_poll_event`).
- Produces: `AcidGame < AcidApp` with `TICK_MS = 50` and an `on_tick` hook,
  loaded into every app VM (used by Task 3 onward).

- [ ] **Step 1: Write `acid_game.rb`**

Create `v2/apps/lib/acid_game.rb`:

```ruby
class AcidGame < AcidApp
  TICK_MS = 50

  def on_tick
  end

  def start
    on_create
    running = true
    while running
      ev = acid_poll_event(self.class::TICK_MS)
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
      on_tick if running
    end
    on_destroy
  end
end
```

- [ ] **Step 2: Wire it into `vm_host.c`**

In `v2/core/vm_host/vm_host.c`, add a second path constant next to the
existing one:

```c
#define ACID_APP_LIB_PATH "v2/apps/lib/acid_app.rb"
#define ACID_GAME_LIB_PATH "v2/apps/lib/acid_game.rb"
```

And add a second `load_file_into_vm` call, right after the existing one,
before the app's own script is loaded:

```c
    load_file_into_vm( mrb, cxt, ACID_APP_LIB_PATH );
    load_file_into_vm( mrb, cxt, ACID_GAME_LIB_PATH );
    load_file_into_vm( mrb, cxt, params->script_path );
```

(Every app's VM now has both `AcidApp` and `AcidGame` defined, whether or
not that particular app uses `AcidGame` — matches the existing convention:
simple and unconditional, not per-app selective loading.)

- [ ] **Step 3: Build**

```bash
cd v2/sim/build && cmake --build . -j4
```

- [ ] **Step 4: Real verification — confirm `on_tick` actually fires repeatedly at the right cadence**

Create a temporary throwaway app (NOT committed) at
`v2/apps/_test_tick.rb`. It draws a marker whose x-position advances every
tick, so two screenshots taken a known time apart can prove ticking is
actually happening on roughly the right cadence (not just once, not
stalled):

```ruby
class TestTick < AcidGame
  def on_create
    @n = 0
  end

  def on_tick
    @n += 1
    acid_clear_user_area
    x = (@n * 2) % 100
    acid_fill_rect(x, 40, 6, 6, 0x00FF66)
  end
end
TestTick.new.start
```

Temporarily add to `v2/sim/sim_main.c` (same convention as Task 1's Step
5 — after the existing spawn calls, not replacing them):

```c
    kernel_spawn_app( "v2/apps/_test_tick.rb", 20, 40, 120, 100, 1 );
```

Build, run, and take two screenshots one second apart:

```bash
cd v2/sim/build && cmake --build . -j4
Xvfb :92 -screen 0 320x240x24 &
sleep 1
DISPLAY=:92 ./acidos_sim &
sleep 1.5
WIN=$(DISPLAY=:92 xdotool search --name "LGFX Simulator")
DISPLAY=:92 import -window "$WIN" /tmp/tick_shot_a.png
sleep 1.0
DISPLAY=:92 import -window "$WIN" /tmp/tick_shot_b.png
kill %1 %2
```

Then, for each screenshot, scan the row at absolute y=80 (window spawned
at y=40, marker drawn at local y=40, so absolute y=80) across the window's
x-range (absolute x=20..120) for the acid-green marker pixel, to find its
x position in each shot:

```bash
for f in /tmp/tick_shot_a.png /tmp/tick_shot_b.png; do
  echo "=== $f ==="
  convert "$f" -crop 100x1+20+80 txt:- | grep -i "00ff66\|00FF66"
done
```

Expected: the marker is found at a different x position in each
screenshot (proving `on_tick` fired and redrew between the two captures),
and the two x positions are consistent with somewhere around 15-25 ticks
having elapsed in ~1 real second (`(@n * 2) % 100` — a wide tolerance is
correct here, this is timing under a real scheduler, not a hard
real-time guarantee; the goal is confirming "ticking at roughly 50ms,"
not an exact count). If the marker isn't found in the expected row (e.g.
antialiasing or coordinate mismatch), search a slightly wider row range
before concluding on_tick isn't working.

- [ ] **Step 5: Clean up the temporary test**

Revert the temporary `sim_main.c` line, delete `v2/apps/_test_tick.rb`.

```bash
git status
git diff --stat
```

Confirm only `v2/apps/lib/acid_game.rb` (new) and
`v2/core/vm_host/vm_host.c` (modified) show up.

- [ ] **Step 6: Commit**

```bash
git add v2/apps/lib/acid_game.rb v2/core/vm_host/vm_host.c
git commit -m "v2: AcidGame base class -- fixed-tick game loop"
```

---

### Task 3: `acid_blaster.rb` — game mechanics (no audio yet)

**Files:**
- Create: `v2/apps/acid_blaster.rb`

**Interfaces:**
- Consumes: `AcidGame` (Task 2), `acid_fill_circle`/`acid_draw_text` (Task
  1), `acid_clear_user_area`/`acid_draw_window_frame` (existing, from
  windowing phase).
- Produces: a playable (silent) game — Task 4 adds sound on top of this
  file, Task 5 wires it into boot.

- [ ] **Step 1: Write the game**

Create `v2/apps/acid_blaster.rb`:

```ruby
class AcidBlaster < AcidGame
  # Must match the kernel_spawn_app(...) call that spawns this app (Task
  # 5) and kernel_layout.h's KERNEL_TITLE_BAR_H -- no generic "ask my own
  # window size" binding exists (see this plan's Global Constraints).
  WINDOW_W = 250
  WINDOW_H = 180
  TITLE_BAR_H = 16
  PLAY_H = WINDOW_H - TITLE_BAR_H
  CENTER_X = WINDOW_W / 2
  CENTER_Y = TITLE_BAR_H + PLAY_H / 2

  ENEMY_R = 8
  TAP_TOLERANCE = 6

  BG_COLOR = 0x050607     # THEME_BG
  ENEMY_COLOR = 0x00FF66  # THEME_HARD
  TEXT_COLOR = 0xD4E6DB   # THEME_TEXT

  def on_create
    reset_game
  end

  def reset_game
    @score = 0
    @enemies = []
    @spawn_timer = 0
    @game_over = false
  end

  def spawn_interval
    v = 24 - @score / 2
    v < 6 ? 6 : v
  end

  def enemy_speed
    v = 2 + @score / 5
    v > 8 ? 8 : v
  end

  def spawn_enemy
    edge = rand(4)
    if edge == 0
      x = rand(WINDOW_W)
      y = TITLE_BAR_H
    elsif edge == 1
      x = WINDOW_W
      y = TITLE_BAR_H + rand(PLAY_H)
    elsif edge == 2
      x = rand(WINDOW_W)
      y = TITLE_BAR_H + PLAY_H
    else
      x = 0
      y = TITLE_BAR_H + rand(PLAY_H)
    end

    dx = CENTER_X - x
    dy = CENTER_Y - y
    dist = Math.sqrt((dx * dx + dy * dy).to_f)
    dist = 1.0 if dist < 1.0
    speed = enemy_speed
    vx = (dx * speed / dist).round
    vy = (dy * speed / dist).round
    # Belt-and-braces: with speed >= 2 this cannot actually happen (see
    # the design spec's note), but a stuck enemy would be a silent, very
    # confusing bug if it ever did, so guard it anyway.
    if vx == 0 && vy == 0
      vx = dx <=> 0
      vy = dy <=> 0
    end

    @enemies << { x: x, y: y, dx: vx, dy: vy }
  end

  # Returns true if any enemy reached the center this tick.
  def update_enemies
    hit_center = false
    @enemies.each do |e|
      e[:x] += e[:dx]
      e[:y] += e[:dy]
      ddx = e[:x] - CENTER_X
      ddy = e[:y] - CENTER_Y
      hit_center = true if (ddx * ddx + ddy * ddy) <= (ENEMY_R * ENEMY_R)
    end
    hit_center
  end

  # Returns true if a tap at (x, y) destroyed an enemy.
  def check_tap(x, y)
    hit_index = nil
    i = 0
    while i < @enemies.length
      e = @enemies[i]
      ddx = x - e[:x]
      ddy = y - e[:y]
      limit = ENEMY_R + TAP_TOLERANCE
      if (ddx * ddx + ddy * ddy) <= (limit * limit)
        hit_index = i
        break
      end
      i += 1
    end
    return false unless hit_index
    @enemies.delete_at(hit_index)
    @score += 1
    true
  end

  def on_touch(x, y, pressed)
    return unless pressed
    if @game_over
      reset_game
      return
    end
    check_tap(x, y)
  end

  def on_tick
    unless @game_over
      @spawn_timer -= 1
      if @spawn_timer <= 0
        spawn_enemy
        @spawn_timer = spawn_interval
      end
      @game_over = true if update_enemies
    end
    draw
  end

  def draw
    acid_clear_user_area
    acid_draw_window_frame
    if @game_over
      draw_game_over
    else
      @enemies.each { |e| acid_fill_circle(e[:x], e[:y], ENEMY_R, ENEMY_COLOR) }
      acid_draw_text("SCORE: #{@score}", 4, TITLE_BAR_H + 2, TEXT_COLOR, BG_COLOR)
    end
  end

  def draw_game_over
    acid_draw_text("GAME OVER", CENTER_X - 36, CENTER_Y - 10, TEXT_COLOR, BG_COLOR)
    acid_draw_text("SCORE: #{@score}", CENTER_X - 30, CENTER_Y + 6, TEXT_COLOR, BG_COLOR)
  end
end

AcidBlaster.new.start
```

- [ ] **Step 2: Real verification — spawn temporarily, play it for real via xdotool, screenshot at each stage**

Temporarily add to `v2/sim/sim_main.c` (after the existing spawn calls):

```c
    kernel_spawn_app( "v2/apps/acid_blaster.rb", 30, 40, 250, 180, 1 );
```

Build and run under Xvfb:

```bash
cd v2/sim/build && cmake --build . -j4
Xvfb :93 -screen 0 320x240x24 &
sleep 1
DISPLAY=:93 ./acidos_sim &
sleep 1.5
WIN=$(DISPLAY=:93 xdotool search --name "LGFX Simulator")
```

**Verify an enemy spawns and is visible:**

```bash
sleep 1.5
DISPLAY=:93 import -window "$WIN" /tmp/blaster_shot1.png
convert /tmp/blaster_shot1.png -crop 250x164+30+56 txt:- | grep -ci "00ff66"
```
Expected: at least one pixel of acid green found in the play area (an
enemy is on screen — exact count varies since spawn timing/position is
randomized, any nonzero count is a pass).

**Verify tap-to-shoot destroys an enemy and increments score.** Find an
enemy's approximate screen position from the screenshot just taken (the
`convert ... txt:-` output includes each matching pixel's coordinates),
then tap there with `xdotool`:

```bash
DISPLAY=:93 xdotool mousemove --window "$WIN" <enemy_x> <enemy_y> click 1
sleep 0.2
DISPLAY=:93 import -window "$WIN" /tmp/blaster_shot2.png
# Score text area: local (4, 18) in a window at (30,40) -> absolute
# (34,58). Confirm it's no longer the untouched background color (proves
# acid_draw_text painted "SCORE: 1" there; exact glyph pixels aren't
# asserted, same reasoning as Task 1's text check).
convert /tmp/blaster_shot2.png -crop 1x1+34+58 txt:-
```
Expected: that pixel is not the raw background color `#050607` — text was
drawn (a score changed from 0 to a nonzero value).

**Verify game-over triggers when an enemy is left alone.** Wait long
enough for an unhandled enemy to reach the center (spawn interval starts
at 24 ticks = 1.2s, and it then needs to travel from an edge to the
center at 2px/tick minimum — budget at least 8-10 real seconds of
untouched wait to be safe), then screenshot:

```bash
sleep 10
DISPLAY=:93 import -window "$WIN" /tmp/blaster_shot3.png
convert /tmp/blaster_shot3.png -crop 1x1+119+128 txt:-
```
(That pixel is roughly where "GAME OVER" text starts, local
`(CENTER_X - 36, CENTER_Y - 10)` = `(89,88)` inside a window at (30,40) ->
absolute (119,128) — adjust the exact coordinate if the first read still
shows background, this is an approximate landing spot for an 8px default
font, not a guaranteed exact glyph pixel.) Confirm it is not `#050607`.

**Verify restart works:** tap anywhere in the play area while on the
game-over screen, then confirm a new enemy-spawn cycle has restarted
(same green-pixel check as the first verification above, after another
short wait).

```bash
DISPLAY=:93 xdotool mousemove --window "$WIN" 120 150 click 1
sleep 1.5
DISPLAY=:93 import -window "$WIN" /tmp/blaster_shot4.png
convert /tmp/blaster_shot4.png -crop 250x164+30+56 txt:- | grep -ci "00ff66"
```

```bash
kill %1 %2
```

- [ ] **Step 3: Clean up the temporary spawn line**

Revert the temporary line in `v2/sim/sim_main.c`.

```bash
git status
git diff --stat
```
Confirm only `v2/apps/acid_blaster.rb` (new) shows up.

- [ ] **Step 4: Commit**

```bash
git add v2/apps/acid_blaster.rb
git commit -m "v2: acid_blaster game mechanics (no audio yet)"
```

---

### Task 4: `acid_blaster.rb` — audio integration

**Files:**
- Modify: `v2/apps/acid_blaster.rb`

**Interfaces:**
- Consumes: `acid_play_note(voice, ona, volume)`/`acid_stop_note(voice)`
  (already exist, from the audio phase — no changes to their signatures or
  behavior).

- [ ] **Step 1: Add sound effect constants and the pending-timer list**

In `v2/apps/acid_blaster.rb`, add near the top of the class body (after
the existing color constants):

```ruby
  HIT_VOICE = 0
  HIT_ONA = 55
  HIT_TICKS = 3
  OVER_VOICE = 1
  OVER_ONA = 25
  OVER_TICKS = 8
```

Update `reset_game` to also reset the pending-effects list:

```ruby
  def reset_game
    @score = 0
    @enemies = []
    @spawn_timer = 0
    @game_over = false
    @sfx = []
  end
```

Add the trigger/tick helpers (mirrors family-mruby-os's real
`sfx_pending`/`tick_sfx` pattern from `shooter.app.rb`, scaled down to this
project's play_note/stop_note-only API — see the design spec's Reference
Model section):

```ruby
  def trigger_sfx(voice, ona, volume, ticks)
    acid_play_note(voice, ona, volume)
    @sfx << { voice: voice, ticks: ticks }
  end

  def tick_sfx
    i = @sfx.length - 1
    while i >= 0
      s = @sfx[i]
      s[:ticks] -= 1
      if s[:ticks] <= 0
        acid_stop_note(s[:voice])
        @sfx.delete_at(i)
      end
      i -= 1
    end
  end
```

- [ ] **Step 2: Trigger the hit sound on a successful tap**

In `check_tap`, change:

```ruby
    return false unless hit_index
    @enemies.delete_at(hit_index)
    @score += 1
    true
```

to:

```ruby
    return false unless hit_index
    @enemies.delete_at(hit_index)
    @score += 1
    trigger_sfx(HIT_VOICE, HIT_ONA, 90, HIT_TICKS)
    true
```

- [ ] **Step 3: Trigger the game-over sound, and tick the sfx list every frame**

Change `on_tick` from:

```ruby
  def on_tick
    unless @game_over
      @spawn_timer -= 1
      if @spawn_timer <= 0
        spawn_enemy
        @spawn_timer = spawn_interval
      end
      @game_over = true if update_enemies
    end
    draw
  end
```

to:

```ruby
  def on_tick
    unless @game_over
      @spawn_timer -= 1
      if @spawn_timer <= 0
        spawn_enemy
        @spawn_timer = spawn_interval
      end
      if update_enemies
        @game_over = true
        trigger_sfx(OVER_VOICE, OVER_ONA, 90, OVER_TICKS)
      end
    end
    tick_sfx
    draw
  end
```

No `on_destroy` override is added: `vm_host.c`'s already-unconditional
`kernel_audio_release_owner` cleanup (from the audio phase) already
silences any voice this app was still holding on any exit path, including
a close mid-effect — this app doesn't need to duplicate that.

- [ ] **Step 4: Build**

```bash
cd v2/sim/build && cmake --build . -j4
```

- [ ] **Step 5: Real verification — real PCM capture, same technique the audio phase (phase 3) already proved out**

Temporarily add the same spawn line as Task 3's Step 2 to
`v2/sim/sim_main.c`. Build, then run with SDL2's `disk` audio driver so
the actual generated audio is captured to a real file:

```bash
cd v2/sim/build && cmake --build . -j4
Xvfb :94 -screen 0 320x240x24 &
sleep 1
DISPLAY=:94 SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=/tmp/blaster_audio.raw \
  ./acidos_sim &
sleep 1.5
WIN=$(DISPLAY=:94 xdotool search --name "LGFX Simulator")
```

Locate a visible enemy the same way Task 3's Step 2 did (screenshot +
grep for `00ff66`), then tap it:

```bash
DISPLAY=:94 import -window "$WIN" /tmp/blaster_audio_shot.png
# (find an enemy's coordinates from this screenshot, same as Task 3)
DISPLAY=:94 xdotool mousemove --window "$WIN" <enemy_x> <enemy_y> click 1
sleep 1
kill %1 %2
```

Inspect the captured raw PCM (8-bit unsigned, `AUDIO_U8`, per the audio
phase's HAL — 128 = silence) for a real non-silent stretch corresponding
to the ~150ms hit sound:

```bash
python3 -c "
data = open('/tmp/blaster_audio.raw', 'rb').read()
non_silent = sum(1 for b in data if abs(b - 128) > 2)
print('total bytes:', len(data), 'non-silent bytes:', non_silent)
"
```
Expected: a nonzero, plausible count of non-silent bytes (at 22050 Hz, a
~150ms note is roughly 3300 bytes — order-of-magnitude match is the right
bar here, not an exact count, matching the audio phase's own established
tolerance).

**Repeat for the game-over sound:** don't tap anything, let the game run
long enough for an enemy to reach the center (same ~10s budget as Task
3's game-over check) with the disk driver capturing the whole session,
then confirm the tail of the capture also shows a non-silent stretch
around the point the enemy reached center.

- [ ] **Step 6: Clean up the temporary spawn line**

Revert the temporary line in `v2/sim/sim_main.c`.

```bash
git status
git diff --stat
```
Confirm only `v2/apps/acid_blaster.rb` shows up as modified.

- [ ] **Step 7: Commit**

```bash
git add v2/apps/acid_blaster.rb
git commit -m "v2: acid_blaster sound effects -- hit and game-over notes"
```

---

### Task 5: Wire into boot sequence (both targets)

**Files:**
- Modify: `v2/sim/sim_main.c`
- Modify: `v2/hw/main/app_main.c`

**Interfaces:**
- Consumes: `v2/apps/acid_blaster.rb` (Tasks 3-4), `kernel_spawn_app`
  (already exists).

- [ ] **Step 1: Wire `sim_main.c`**

In `v2/sim/sim_main.c`, add this line permanently, after the existing
`kernel_spawn_app( "v2/apps/demo_swatch.rb", ...)` call and before the
`xTaskCreate( kernel_router_task, ...)` call:

```c
    kernel_spawn_app( "v2/apps/acid_blaster.rb", 30, 40, 250, 180, 1 );
```

- [ ] **Step 2: Wire `app_main.c`**

In `v2/hw/main/app_main.c`, add the identical line in the identical
position (after the existing `demo_swatch.rb` spawn call, before
`xTaskCreate`):

```c
    kernel_spawn_app( "v2/apps/acid_blaster.rb", 30, 40, 250, 180, 1 );
```

- [ ] **Step 3: Build `v2/sim`**

```bash
cd v2/sim/build && cmake --build . -j4
```

- [ ] **Step 4: Real verification — full boot sequence includes the game, real play session**

```bash
Xvfb :95 -screen 0 320x240x24 &
sleep 1
DISPLAY=:95 ./acidos_sim &
sleep 1.5
WIN=$(DISPLAY=:95 xdotool search --name "LGFX Simulator")
DISPLAY=:95 import -window "$WIN" /tmp/boot_shot.png
convert /tmp/boot_shot.png -crop 250x164+30+56 txt:- | grep -ci "00ff66"
```
Expected: nonzero (the game window is present and has spawned at least one
enemy), AND confirm the other existing apps (`demo_touch`, `demo_swatch`,
the desktop strip) are still present and undisturbed in the same
screenshot — spot-check one pixel from each of their known regions against
their expected colors, same technique used throughout this plan.

Play a short real session (tap an enemy, confirm score updates) exactly as
in Task 3's Step 2, to confirm the game works end-to-end from a cold real
boot, not just when spawned in isolation.

```bash
kill %1 %2
```

- [ ] **Step 5: Statically verify `v2/hw`**

`idf.py build` cannot run on this machine (no ESP-IDF installed — same
gap as every prior phase). Instead: re-read the final `app_main.c` and
confirm the new line's syntax matches `kernel_spawn_app`'s real signature
(`const char*, int, int, int, int, int`) exactly, and that
`v2/hw/main/CMakeLists.txt` already includes every source file this
change touches (it does — no new files were created by this plan, only
existing files were modified, and `hal_display_hw.c`/`gfx_binding.c`/
`gfx.c`/`vm_host.c` are all already listed there from prior phases).
State this explicitly in your report rather than silently skipping it.

- [ ] **Step 6: Commit**

```bash
git add v2/sim/sim_main.c v2/hw/main/app_main.c
git commit -m "v2: wire acid_blaster into boot sequence (sim + hw)"
```
