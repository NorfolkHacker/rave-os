# 6. Games

[← Sound](05-sound.md) · [Contents](README.md) · [Next: System APIs →](07-system-apis.md)

`AcidApp` is event-driven: it sits blocked on its queue and wakes when something
happens. That is right for a text editor and wrong for anything with a ball in
it. `AcidGame` (`v2/apps/lib/acid_game.rb`, always loaded) replaces the event
loop with a **fixed-tick** one.

## 6.1 `AcidGame`

```ruby
class SnakeApp < AcidGame
  TICK_MS = 50          # 20 ticks per second

  def on_create; end
  def on_tick; end      # ← the new one
  def on_touch(x, y, pressed); end
  def on_key(code, pressed); end
  def on_destroy; end
end

SnakeApp.new.start
```

`on_tick` fires every `TICK_MS` milliseconds regardless of whether anything
happened. Everything else works as in `AcidApp`, with three differences:

1. **`on_idle` is never called.** `on_tick` replaces it.
2. **`redraw` is never called automatically.** Not at startup, not on `:moved`.
   A game repaints its whole scene from `on_tick`; that is the contract.
3. **`quit!` does nothing.** `AcidGame` runs its own loop with a local flag and
   deliberately ignores `AcidApp`'s `@running`. If you want a self-close, add
   your own flag and check it in a loop of your own.

`TICK_MS` is read as `self.class::TICK_MS`, so it is a **constant** on your
class — unlike `poll_timeout_ms`, which is a method because it changes at
runtime.

## 6.2 The tick loop

```ruby
def start
  on_create
  running = true
  next_tick_at = Time.now.to_f + (self.class::TICK_MS / 1000.0)
  while running
    remaining_ms = ((next_tick_at - Time.now.to_f) * 1000).to_i
    remaining_ms = 0 if remaining_ms < 0
    ev = acid_poll_event(remaining_ms)
    if ev == :close
      running = false
    elsif ev == :moved
      acid_notify_redraw_done
    elsif ev.is_a?(Array) && ev[0] == :key
      on_key(ev[1], ev[2])
    elsif ev
      on_touch(ev[0], ev[1], ev[2])
    end
    if running && Time.now.to_f >= next_tick_at
      on_tick
      next_tick_at += (self.class::TICK_MS / 1000.0)
      next_tick_at = Time.now.to_f + (self.class::TICK_MS / 1000.0) if next_tick_at < Time.now.to_f
    end
  end
  on_destroy
end
```

Two details worth understanding:

**The poll timeout is whatever is left of this tick.** Events are still handled
promptly — a touch in the middle of a tick is dispatched immediately — but the
tick itself stays on schedule.

**Missed ticks resync rather than burst.** If something put the loop more than a
whole tick behind, `next_tick_at` is reset to *now plus one tick* instead of
catching up. A catch-up burst would look like the game briefly speeding up,
which is worse than a dropped frame.

**`:moved` is acknowledged but not repainted.** A game redraws its whole scene
next tick anyway, so a stale chrome position self-corrects within `TICK_MS`. The
acknowledgement still has to be sent immediately, or the compositor blocks
waiting for a repaint that was never going to come.

### Picking `TICK_MS`

| `TICK_MS` | Rate | Suits |
|---|---|---|
| 33 | 30 Hz | Fast action, smooth motion |
| 50 | 20 Hz | The default across this OS's games — arcade action |
| 100 | 10 Hz | Puzzle games, turn-based movement |
| 500 | 2 Hz | Tetris-style gravity (or use a counter on a faster tick) |

Prefer a fast tick with a counter over a slow tick, when different things in
your game move at different rates:

```ruby
TICK_MS = 50

def on_tick
  @frame += 1
  move_player                        # every tick
  drop_piece if @frame % 10 == 0     # every 500ms
  spawn_enemy if @frame % 40 == 0    # every 2s
end
```

## 6.3 Focus and the z-order trap

This is the one thing that *must* be right in a game.

A normal app's `redraw` only ever runs inside the compositor's z-order-aware
repaint. A game repaints itself **directly, every tick, with no z-order
awareness at all**. If it keeps doing that while another window is genuinely on
top, it paints over that window's visible content on every single tick.

So every game's tick ends with a focus check:

```ruby
def on_tick
  update_world           # simulation always runs
  tick_sfx               # sound bookkeeping always runs
  draw if focused?       # drawing only when we are actually on top
end
```

Note what is *inside* the guard and what is not. The world keeps turning while
you are covered; only the painting stops.

You also need to force a full repaint when focus comes *back*, because the
partial-redraw state you were tracking is stale:

```ruby
def on_tick
  update_world
  tick_sfx
  is_focused = focused?
  @needs_frame = true if is_focused && !@was_focused   # just regained focus
  @was_focused = is_focused
  draw if is_focused
end
```

## 6.4 The sound-effect lifecycle

**Nothing stops a note but an explicit `acid_stop_note`.** Not a short envelope,
not a finished arpeggio, not the end of a tick. A gated-on voice sustains, and a
triggered arpeggio cycles, until you gate it off.

That makes sound effects a *bookkeeping* problem, and this OS's games all solve
it the same way. Learn the pattern; it is the source of the two most annoying
bugs you can ship.

### The pattern

Register every sound you start, count it down, and stop it in exactly one place.

```ruby
# 1. Starting a sound registers it with a tick countdown.
def trigger_sfx(voice, notes, count, rate_ms, volume, ticks)
  acid_play_note(voice, notes[0], volume)
  acid_trigger_arp(voice, notes[0], notes[1], notes[2], notes[3], count, rate_ms)
  @sfx << { voice: voice, ticks: ticks }
end

# 2. tick_sfx is the ONLY place a note is ever stopped.
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

# 3. Called from every tick, unconditionally — never behind `focused?`.
def on_tick
  update_world unless @game_over
  tick_sfx
  draw if focused?
end
```

Iterate **backwards** when deleting during a walk, as above.

### Rule 1 — a reset must stop voices before it clears the bookkeeping

```ruby
def reset_game
  stop_all_sfx        # ← first
  @sfx = []
  @score = 0
  # ...
end

def stop_all_sfx
  return unless @sfx  # safe before the first reset_game
  @sfx.each { |s| acid_stop_note(s[:voice]) }
  @sfx = []
end
```

A restart tap arrives while the game-over sting is usually still playing.
`tick_sfx` is the only thing that ever sends a note-off — so clearing `@sfx`
without first stopping its voices leaves them with **no note-off ever coming**.
The arpeggio cycles forever and the envelope sustains forever, for as long as
the app is open.

Both shipped arcade games had exactly this bug. A short envelope only made it
rarer to hit, not impossible.

### Rule 2 — an arpeggio's gate must not outlast one pass

A four-note arp at 110 ms takes 440 ms for one pass and then starts over. If you
meant it as a one-shot run, compute the tick count from the arp itself rather
than guessing:

```ruby
OVER_NOTES = [ 30, 27, 23, 18 ]
OVER_RATE_MS = 110
OVER_COUNT = 4
OVER_TICKS = (OVER_RATE_MS * OVER_COUNT + TICK_MS - 1) / TICK_MS   # round up

trigger_sfx(OVER_VOICE, OVER_NOTES, OVER_COUNT, OVER_RATE_MS, 50, OVER_TICKS)
```

Derive it and the sound stays correct when you retune the effect. Hard-code it
and it drifts the first time you change `rate_ms`.

### Rule 3 — `tick_sfx` runs even when nothing else does

Put it *outside* every guard. A game that stops calling `tick_sfx` while paused,
while game-over, or while unfocused has just invented a stuck note.

```ruby
def on_tick
  unless @game_over
    update_ball          # guarded
  end
  tick_sfx               # never guarded
  draw if focused?
end
```

### Rule 4 — `on_destroy` stops everything

```ruby
def on_destroy
  stop_all_sfx
end
```

The kernel releases your voices when your task exits either way, so this is not
about leaks — it is about the difference between a clean fade and an abrupt cut,
and about keeping the habit while the app is still running.

## 6.5 A minimal game

```ruby
class DodgeApp < AcidGame
  WINDOW_W = 200
  WINDOW_H = 160
  TITLE_BAR_H = 16
  TICK_MS = 50

  BG = 0x050607
  TEXT = 0xD4E6DB

  PLAYER_W = 24
  PLAYER_H = 6
  PLAYER_Y = WINDOW_H - 16
  ROCK_R = 3
  HIT_VOICE = 7
  HIT_RATE_MS = 30
  HIT_COUNT = 3
  HIT_TICKS = (HIT_RATE_MS * HIT_COUNT + TICK_MS - 1) / TICK_MS

  def on_create
    acid_configure_filter(180, 3, 1)
    acid_configure_osc(HIT_VOICE, AcidWaveform::SAW, 50)
    acid_configure_voice(HIT_VOICE, 1, 2, 40, 50, 60)
    reset_game
  end

  def reset_game
    stop_all_sfx
    @sfx = []
    @player_x = (WINDOW_W - PLAYER_W) / 2
    @rocks = []
    @score = 0
    @over = false
    @frame = 0
    @needs_full = true
    @was_focused = false
  end

  def stop_all_sfx
    return unless @sfx
    @sfx.each { |s| acid_stop_note(s[:voice]) }
    @sfx = []
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

  def hit_sound
    acid_play_note(HIT_VOICE, 55, 60)
    acid_trigger_arp(HIT_VOICE, 55, 48, 41, 1, HIT_COUNT, HIT_RATE_MS)
    @sfx << { voice: HIT_VOICE, ticks: HIT_TICKS }
  end

  def on_touch(x, y, pressed)
    return unless pressed
    if @over
      reset_game
      return
    end
    @player_x = x - PLAYER_W / 2
    @player_x = 0 if @player_x < 0
    @player_x = WINDOW_W - PLAYER_W if @player_x > WINDOW_W - PLAYER_W
  end

  def on_tick
    unless @over
      @frame += 1
      @rocks << { x: 8 + rand(WINDOW_W - 16), y: TITLE_BAR_H } if @frame % 8 == 0
      i = @rocks.length - 1
      while i >= 0
        r = @rocks[i]
        r[:y] += 4
        if r[:y] >= PLAYER_Y && r[:x] > @player_x && r[:x] < @player_x + PLAYER_W
          hit_sound
          @over = true
        end
        if r[:y] > WINDOW_H
          @rocks.delete_at(i)
          @score += 1
        end
        i -= 1
      end
    end

    tick_sfx

    is_focused = focused?
    @needs_full = true if is_focused && !@was_focused
    @was_focused = is_focused
    draw if is_focused
  end

  def on_destroy
    stop_all_sfx
  end

  def draw
    if @needs_full
      acid_draw_window_frame(window_title)
      @needs_full = false
    end
    acid_fill_rect(0, TITLE_BAR_H, WINDOW_W, WINDOW_H - TITLE_BAR_H, BG)
    @rocks.each_with_index do |r, i|
      acid_fill_circle(r[:x], r[:y], ROCK_R, AcidPalette.hue(i * 30 + @score * 7))
    end
    acid_fill_rect(@player_x, PLAYER_Y, PLAYER_W, PLAYER_H, 0x00FF66)
    acid_draw_text("SCORE #{@score}", 6, TITLE_BAR_H + 4, TEXT, BG)
    acid_draw_text("TAP TO RESTART", 46, WINDOW_H / 2, TEXT, BG) if @over
    acid_draw_window_border
  end
end

DodgeApp.new.start
```

With `v2/apps/dodge.app.toml`:

```toml
name = Dodge
w = 200
h = 160
desc = Dodge the falling rocks
menu = false
```

`menu = false` keeps a game out of the Menu dropdown — the convention every
shipped game follows. They are launched instead from the Terminal (`run tetris`)
or by clicking their `.app.toml` in File Manager. Drop the line if you want
yours in the Menu.

---

[← Sound](05-sound.md) · [Contents](README.md) · [Next: System APIs →](07-system-apis.md)
