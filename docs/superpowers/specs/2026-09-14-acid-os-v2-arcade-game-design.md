# acid OS v2: first real app/game (roadmap phase 4) — design

**Goal:** ship the first real, complete Ruby app for acid OS v2 — a small tap-to-shoot
survival arcade game — exercising the windowing (phase 2) and audio (phase 3)
subsystems together from an actual app, not a demo. Runs unmodified on both
`v2/sim` and `v2/hw`, like every existing app.

**Working title:** "Acid Blaster" (`v2/apps/acid_blaster.rb`, `class AcidBlaster`).
Not precious about the name.

## Reference model

Researched directly from family-mruby-os's real source (`fmruby-core`, cloned to
scratchpad — this is the same OS acid OS v2 is modeled on, not a clone of it).
Specifically read `flash/app/game/shooter.app.rb`, their real shoot-'em-up. Its
architecture is adopted here, scaled down to acid OS v2's much smaller surface:

- **Fixed-tick loop, not delta-time physics.** Their `on_update` runs on a fixed
  cadence (`TICK_MS = 50`, ~20fps) and all movement is expressed in plain
  integer "pixels per tick," not real elapsed time. No floats needed for
  motion. acid OS v2 adopts the same: `TICK_MS = 50`.
- **Sound effects via a pending-timer list.** Their `sfx_pending`/`tick_sfx`
  pattern: triggering a sound pushes `{ticks_left, ...}`, decremented every
  tick, firing the note-off when it reaches zero. Adopted here in a form
  scaled to acid OS v2's much smaller audio API (`acid_play_note`/
  `acid_stop_note` only — no frequency/duty/sweep control).
  See "Audio" below.
- **Text drawing takes both colors.** Their `@gfx.draw_text(x, y, string, fg,
  bg)` always supplies a background color (opaque, not transparent) so text
  stays legible over an animated scene. acid OS v2's new `acid_draw_text`
  binding matches this shape.

Not adopted (out of scope for this phase, noted for the future roadmap phase
below): sprites/BMP loading, keyboard/gamepad input, the formation/boss
structure, multi-channel APU-style sound.

## Roadmap note (not part of this phase)

Verified against the real family-mruby-os source that it has substantially
more than acid OS v2 does today: a USB keyboard driver, a real shell/terminal
app (`shell.app.rb` + `shell_irb.rb` for interactive Ruby, `shell_commands.rb`
for filesystem/process commands), and even a second language runtime
alongside Ruby (a real "Family BASIC"-compatible interpreter,
`components/basic/`, `.bas` app files), plus a large app catalog (editor,
file manager, presentation tool, music tools, several games).

Decision: **keyboard input + a shell/terminal app is added as an explicit
future roadmap phase**, not part of this one. This phase stays scoped to a
single touch-only game. Roadmap (updated):
1. bring-up — DONE
2. windowing/GUI — DONE
3. audio — DONE
4. first real app/game (this spec)
5. keyboard input + terminal/shell app
6. real Tab5 hardware bring-up (flash+run)
7. custom cyberdeck hardware

## Architecture

### New base class: `AcidGame < AcidApp`

New file `v2/apps/lib/acid_game.rb`. Reuses `AcidApp`'s `on_create`/
`on_destroy`/`on_touch` hooks and its `:close` handling verbatim (require
`acid_app.rb` first, same as any future file that needs it). Overrides
`start` with a fixed-tick loop instead of `AcidApp#start`'s 200ms
blocking-poll-then-idle loop:

```ruby
require "v2/apps/lib/acid_app"

class AcidGame < AcidApp
  TICK_MS = 50

  def on_tick; end

  def start
    on_create
    running = true
    while running
      ev = acid_poll_event(self.class::TICK_MS)
      if ev == :close
        running = false
      elsif ev == :moved
        # no-op here: this app redraws its whole scene every tick anyway
        # (see on_tick contract below), so a stale chrome position self-
        # corrects on the very next tick without a special case.
      elsif ev
        on_touch(ev[0], ev[1], ev[2])
      end
      on_tick if running
    end
    on_destroy
  end
end
```

Contract for subclasses: `on_tick` is called roughly every `TICK_MS`
milliseconds (whether or not a touch event arrived that iteration) and is
responsible for updating game state AND drawing the entire scene for that
frame, including chrome (`acid_draw_window_frame`) — unlike `AcidApp`'s
redraw-on-demand model, a 20fps game simply repaints everything every tick,
so there's no separate dirty-tracking to get wrong.

`AcidApp` itself is **not modified** by this phase — `demo_touch.rb`,
`demo_swatch.rb`, and `desktop.rb` are unaffected.

### New gfx bindings

Two additions to the Ruby-facing drawing API, both following the existing
binding pattern exactly (`mrb_define_module_function` on `mrb->kernel_module`,
reading `struct kernel_app_context` off `mrb->ud` for the window offset, same
as `acid_fill_rect`).

**`acid_fill_circle(x, y, r, color)`** — new file `v2/core/bindings/
gfx_binding.c` addition (same file `acid_fill_rect` already lives in). Wraps
`gfx_fill_circle`, which already exists in the C layer
(`v2/core/gfx/gfx.c`/`.h`) from the windowing phase but was never exposed to
Ruby. Trivial — same shape as `acid_fill_rect` minus one parameter.

**`acid_draw_text(str, x, y, fg_color, bg_color)`** — new. Requires:
- `v2/core/gfx/gfx.h`/`.c`: new `gfx_draw_text(int x, int y, const char *str,
  unsigned int fg, unsigned int bg)`, taking `gfx_get_lock()` around the call
  exactly like `gfx_fill_rect`/`gfx_fill_circle` already do (serializes
  against the shared LGFX object, per the windowing phase's own final-review
  fix).
- `v2/core/hal/hal_display.h`: new `void hal_display_draw_text(int x, int y,
  const char *str, unsigned int fg, unsigned int bg);` declaration.
- `v2/sim/hal_display_sim.cpp`: implementation using LovyanGFX's real API
  (verified against `v2/components/lovyangfx/src/lgfx/v1/LGFXBase.hpp`):
  `lcd.setTextColor(fg, bg); lcd.drawString(str, x, y);` — the default font
  (no `setFont` call) is LovyanGFX's built-in bitmap font, no font-loading
  infra needed.
- `v2/hw/main/hal_display_hw.c`: `ESP_LOGI`-only stub, matching every other
  hw HAL function's existing pattern exactly (`hal_display_fill_rect`,
  `hal_display_fill_circle`, `hal_audio_hw.c`'s functions).
- `v2/core/bindings/gfx_binding.c`: the `acid_draw_text` binding itself,
  reading a `const char*` string arg (`mrb_get_args(mrb, "siiii", ...)`) plus
  four ints, translating x/y by `ctx->window_x`/`ctx->window_y` same as
  every other binding.

## Game state and mechanics

**Window:** spawned in `sim_main.c` (and `hw/main/app_main.c`, identically)
alongside the existing three apps: `kernel_spawn_app("v2/apps/
acid_blaster.rb", 30, 40, 250, 180, 1)`. Screen is 320x240
(`v2/sim/hal_display_sim.cpp`'s `LGFX lcd(320, 240)`), desktop strip occupies
y0-20. This default position overlaps `demo_touch`/`demo_swatch`'s own
default positions — acceptable, windows are already draggable (phase 2), no
layout logic needed for this phase.

**Play area:** the window's user area (below the `KERNEL_TITLE_BAR_H`-tall
title bar, i.e. local coordinates `y >= 16`, per the existing
`acid_clear_user_area` convention). Center point for "enemies converge here"
is the play area's own center.

**Entities:** enemies only (no player avatar/sprite — tapping an enemy
directly destroys it, there is nothing else to draw for the player). Each
enemy: `{x, y, dx, dy, r}` — position, per-tick velocity (integer pixels/tick,
matching the fixed-tick model), radius (`ENEMY_R = 8`).

**Spawn:** every `spawn_interval` ticks, create one enemy at a random point
on the play area's edge (pick edge 0-3 via `rand(4)`, then a random
coordinate along it via `rand(w)`/`rand(h)` — `mruby-random` is confirmed
present in this project's actual mruby build, pulled in by `stdlib-ext.gembox`
via `default.gembox`, so `Kernel#rand` is available with no new gem work).
Velocity is the unit vector from spawn point to play-area center, scaled by
`enemy_speed` (integer pixels/tick — computed as a plain integer ratio, not
float division, to stay consistent with the fixed-tick model's no-floats
approach — e.g. `dx = (cx - x) * enemy_speed / distance`, distance via
integer approximation is acceptable since exact diagonal speed doesn't need
to be pixel-perfect for a first pass).

**Difficulty curve, both driven by `@score`:**
- `spawn_interval_ticks = [24 - @score / 2, 6].max` (starts at 24 ticks =
  1.2s between spawns at 50ms/tick, floors at 6 ticks = 0.3s)
- `enemy_speed = [2 + @score / 5, 8].min` (starts at 2px/tick, caps at
  8px/tick)

**Hit detection (tap-to-shoot):** on `on_touch(x, y, pressed)`, only act on
`pressed == true` (ignore release — a tap is a single-shot action here,
unlike `demo_touch`'s hold-to-sustain-note model). Translate to play-area-
local coordinates already handled by the existing `on_touch` contract (same
as `demo_touch.rb`'s existing `x`/`y` usage). Iterate `@enemies`, find the
first whose center is within `r + TAP_TOLERANCE` pixels of the tap
(`TAP_TOLERANCE = 6`, generous for a finger on a small screen); if found,
remove it, `@score += 1`, trigger the hit sound (see Audio). No explicit
"which enemy is on top" ordering concern — first match in array order,
consistent with this project's established "simplicity over cleverness for a
first pass" precedent (voice-stealing in the audio phase took the same
approach).

**Enemy reaching center = game over:** each tick, after moving every enemy,
check if any enemy's distance to the play-area center is `<= r`. If so:
`@game_over = true`, trigger the game-over sound, stop updating/spawning
enemies (but keep ticking the sound-effect timer list and keep drawing).

**Game-over screen:** clear the play area, draw "GAME OVER" and "SCORE: N"
via `acid_draw_text`, centered-ish in the play area. Any `pressed` touch
while `@game_over` is true resets: `@score = 0`, `@enemies = []`,
`@spawn_timer = 0`, `@game_over = false`.

**dt/tick safety:** none needed — this is a fixed-tick design (no measured
elapsed time anywhere), so there's no "stalled tick causes a teleport" risk
the way a dt-based design would have.

## Audio

Two sound effects, using the existing minimal `acid_play_note(voice, ona,
volume)`/`acid_stop_note(voice)` API exactly as-is (no changes to the audio
phase's Ruby surface). Mirrors family-mruby-os's `sfx_pending`/`tick_sfx`
pattern, scaled down: a plain array of `{voice:, ticks_left:}`, decremented
once per `on_tick`, calling `acid_stop_note(voice)` when a timer reaches
zero.

- **Hit** (enemy destroyed): `acid_play_note(0, 55, 90)` (ona 55 ≈ a
  mid-high pitch — arbitrary but reasonable, per the ona 1-88 table
  documented in `v2/core/audio/synth.h`), held for 3 ticks (~150ms) before
  `acid_stop_note(0)`.
- **Game over** (enemy reaches center): `acid_play_note(1, 25, 90)` (ona 25 —
  distinctly lower than the hit sound), held for 8 ticks (~400ms) before
  `acid_stop_note(1)`.

Voices 0 and 1 are arbitrary, unshared with anything else running audio in
this demo boot sequence (nothing else currently plays notes except
`demo_touch.rb`, which uses voice 0 too — a real but harmless collision
covered by the audio phase's own explicit "voice-stealing is unconditional,
last write wins" design: if the user is holding a note on `demo_touch` and
also playing the game, the game's hit sound steals voice 0 momentarily. This
is the same accepted tradeoff the audio phase already designed for, not a
new problem — not fixed here).

## Visual style

Palette follows RaveOS's existing documented colors (`docs/BUILD_LOG.md`),
same as every prior v2 phase — no new palette:
- Enemies: acid green (`#00ff66`, `THEME_HARD` — already defined in
  `v2/core/kernel/kernel_theme.h`)
- Background: existing `THEME_BG`
- Score/game-over text: existing `THEME_TEXT` foreground on `THEME_BG`
  background (via the new `acid_draw_text` bg-color parameter)

## Testing plan

Same real-verification discipline as every prior phase:
- Build `v2/sim` clean.
- Real `xdotool` taps against the live sim window (not synthetic/mocked
  events) confirming: a tap on a visible enemy destroys it and increments
  score; a tap on empty space does nothing; an enemy left alone reaches the
  center and triggers game over; a tap during game-over resets the game.
- SDL2 `disk` audio driver capture confirming the hit and game-over sounds
  produce real non-silent PCM of roughly the expected duration (~150ms /
  ~400ms), not just "the call didn't crash."
- `v2/hw` (ESP-IDF): statically verified only, same pre-existing gap as
  every prior phase (no ESP-IDF installed on this machine).

## Global constraints

- No RaveOS v1 files (`kernel/`, `boot/`, `programs/`, `demos/`) touched.
- `AcidApp` (`v2/apps/lib/acid_app.rb`) is not modified — `AcidGame` is a
  separate, new base class.
- No changes to the audio phase's Ruby API (`acid_play_note`/
  `acid_stop_note` used exactly as they are).
- New Ruby-facing gfx bindings are exactly two: `acid_fill_circle`,
  `acid_draw_text`. Nothing else added to the binding surface.
- `hw` gets the same source files added to its build (`v2/hw/main/
  CMakeLists.txt`) and the same boot-sequence spawn call
  (`v2/hw/main/app_main.c`), matching `sim_main.c` — same pattern every prior
  phase has followed.
- Fixed-tick game logic uses integer arithmetic only (no floats in movement/
  spawn/difficulty math), matching the family-mruby-os reference pattern and
  this project's general no-floating-point-where-avoidable convention.

## Ambiguities closed explicitly

- **Multiple enemies overlapping a tap:** first match in array iteration
  order is destroyed, not the nearest/topmost. Simplicity over precision for
  a first pass.
- **Lives:** none — single miss ends the game. Matches the "reflexes, high
  stakes" feel of a simple arcade survival loop; a lives system can be added
  later if it turns out to feel too punishing.
- **Voice collision with `demo_touch.rb`:** accepted, not fixed — covered by
  the audio phase's own existing unconditional voice-stealing design.
- **Restart control:** any tap while on the game-over screen, not a specific
  button/region — smallest possible surface for a touch-only restart.
