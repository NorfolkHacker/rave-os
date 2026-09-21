# 3. The app lifecycle

[← Apps and manifests](02-apps-and-manifests.md) · [Contents](README.md) · [Next: Graphics →](04-graphics.md)

Your app is a subclass of `AcidApp` (`v2/apps/lib/acid_app.rb`), and the last
line of your file starts it:

```ruby
class MyApp < AcidApp
  # override what you need
end

MyApp.new.start
```

`start` calls `on_create`, paints once with `redraw`, then loops until the
window closes, dispatching events to your callbacks. When the loop ends it calls
`on_destroy`.

## 3.1 The callbacks

Every one of these has an empty default. Override only what you need.

### `on_create`

Called once, before the first paint. Set up instance variables, configure synth
voices, read `acid_launch_arg`.

```ruby
def on_create
  @items = []
  @selected = 0
  acid_configure_voice(5, 1, 5, 80, 60, 120)
end
```

### `on_touch(x, y, pressed)`

`x` and `y` are **window-relative** — `(0, 0)` is your window's own top-left
corner, not the screen's. `pressed` is `true` for a press or a drag, `false` for
a release.

Three things the kernel does before you see a touch:

- **The title bar is never yours.** A press at `y < 16` is consumed by the
  router as a drag or a close. You never receive a touch above `y = 16`.
- **A gesture belongs to the window it started in.** Once a press lands in your
  window, every drag event and the final release come to you — *even after the
  finger leaves your window*. So `x` and `y` can be **negative or larger than
  your window**. Clamp before you use them for a hit test.
- **A gesture that started elsewhere never leaks in.** A drag that began on a
  close button, in the desktop strip, or over nothing delivers nothing to
  whichever window happens to be under the finger now.

```ruby
def on_touch(x, y, pressed)
  return unless pressed
  @selected = (y - TITLE_BAR_H) / ROW_H
  redraw
end
```

> **Presses repeat while held.** A finger or mouse button held down delivers
> `pressed == true` repeatedly, not once. Anything that *acts* on a press —
> firing a shot, flipping a mode, advancing a counter — needs a guard, or it
> fires on every frame the touch is held. See [§3.5](#35-touch-debouncing).

### `on_key(code, pressed)`

Only the focused window receives key events. `code` is the character's byte for
printable ASCII, or one of the `AcidKeys` constants:

```ruby
module AcidKeys
  ENTER = 257; BACKSPACE = 258; ESCAPE = 259; TAB = 260
  DELETE = 261; UP = 262; DOWN = 263; LEFT = 264; RIGHT = 265
end
```

```ruby
def on_key(code, pressed)
  return unless pressed
  case code
  when AcidKeys::UP    then move(-1)
  when AcidKeys::DOWN  then move(1)
  when AcidKeys::ESCAPE then quit!
  else
    @line << code.chr if code >= 32 && code < 127
  end
  redraw
end
```

### `on_idle`

Fires whenever a poll times out with no event waiting — so its rate is set by
`poll_timeout_ms`, and it fires *at most* that often. This is where an
animating app advances a frame.

```ruby
def on_idle
  return unless focused?
  @phase += 1
  redraw
end
```

### `on_destroy`

Called once, after the loop ends, whichever way it ended. Use it to stop
sounding notes, close an overlay, or flush state to disk.

```ruby
def on_destroy
  acid_stop_note(MY_VOICE)
  acid_overlay_close
end
```

> Voices your app gated on are released by the kernel when your task exits, on
> every exit path — so a forgotten `acid_stop_note` here will not leave a note
> droning after your app is gone. Do it anyway: it is the difference between a
> clean fade and an abrupt cut, and it is the habit that keeps your *running*
> app's sound correct.

### `redraw`

Repaints the whole window. The default implementation draws bare chrome:

```ruby
def redraw
  acid_clear_user_area
  acid_draw_window_frame(window_title)
  acid_draw_window_border
end
```

Yours should follow the same shape — **clear, frame, your content, border** —
and the border genuinely must come last. See
[§4.3](04-graphics.md#43-window-chrome).

## 3.2 The event loop

This is the real loop, from `acid_app.rb`:

```ruby
def start
  on_create
  redraw
  @running = true
  while @running
    ev = acid_poll_event([poll_timeout_ms, 1].max)
    if ev == :close
      @running = false
    elsif ev == :moved
      redraw
      acid_notify_redraw_done
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
```

`acid_poll_event(timeout_ms)` blocks on your window's event queue and returns:

| Return | Meaning |
|---|---|
| `nil` | The timeout expired with nothing waiting → `on_idle` |
| `:close` | The close button, or the kernel ending your app → loop ends |
| `:moved` | Your window was dragged; you must repaint and then acknowledge |
| `[:key, code, pressed]` | A key event |
| `[x, y, pressed]` | A touch event |

You normally never call `acid_poll_event` yourself — `AcidApp` does it. You only
reach for it when writing a loop of your own, as `AcidGame` does.

### `:moved` and `acid_notify_redraw_done`

When windows move, the compositor repaints them **back to front**, and it waits
for each window to confirm it has finished drawing before moving to the next.
`acid_notify_redraw_done` is that confirmation. Without it the compositor blocks
for the full timeout on your window, every drag, and z-order comes out as send
order rather than completion order.

If you write your own loop, you must handle `:moved` and you must acknowledge
it — even if you choose not to repaint. `AcidGame` acknowledges without
repainting, because it repaints everything on its next tick anyway.

## 3.3 `poll_timeout_ms`

How long `acid_poll_event` blocks when nothing is waiting, and therefore how
often `on_idle` fires. The default is **200 ms**.

```ruby
def poll_timeout_ms
  40      # ~25 fps
end
```

It is a **method, not a constant**, because the right answer changes while the
app runs. The Terminal returns a frame interval while an animation is in flight
and drops back to 200 afterwards:

```ruby
def poll_timeout_ms
  @animating ? 33 : 200
end
```

The loop clamps to a minimum of 1 ms. Returning `0` would busy-spin at 100% CPU;
returning a negative value converts to an enormous unsigned tick count — an
effectively infinite block, so `on_idle` would never fire again. The clamp
prevents both; don't rely on it.

**Pick the largest number that still looks right.** Every app is a task on a
small device, and a 200 ms poll that wakes five times a second costs almost
nothing.

## 3.4 Focus, titles and quitting

### `focused?`

True when your window holds keyboard focus — which in this OS also means it is
the topmost visible window, because focus and z-order always change together.

Use it to skip work you cannot see:

```ruby
def on_idle
  return unless focused?
  advance_animation
  redraw
end
```

This matters most for apps that paint **outside** the compositor's z-order-aware
repaint — see [§6.3](06-games.md#63-focus-and-the-z-order-trap).

### `window_title`

Derived from your class name automatically: `DemoTouchApp` → `"Demo Touch"`
(trailing `App` dropped, camel case split, capped at 16 characters). Override
for something custom:

```ruby
def window_title
  "Counter #{@count}"
end
```

16 characters is a real limit, not a style note: narrow windows are around 140px
and the title is **not clipped against the close button**, so an overlong title
runs straight into it.

### `quit!`

Ends the loop from inside your app:

```ruby
def on_key(code, pressed)
  quit! if pressed && code == AcidKeys::ESCAPE
end
```

The other two ways the loop ends — the title-bar close button, and the kernel
ending your app — both arrive as the `:close` event, not through this method.

> `quit!` only sets `@running`. `AcidGame` runs its own loop with its own local
> flag and deliberately ignores that ivar, so **`quit!` does nothing in a
> game**. See [§6.1](06-games.md#61-acidgame).

## 3.5 Touch debouncing

The single most common bug in an acid OS app.

A held touch delivers `pressed == true` on **every poll**, not once per press.
Anything that acts on a press therefore needs to fire once per *hold*, not once
per event:

```ruby
def on_create
  @touch_down = false
end

def on_touch(x, y, pressed)
  unless pressed
    @touch_down = false      # release: re-arm
    return
  end
  return if @touch_down      # still the same hold: ignore
  @touch_down = true
  fire!                      # runs exactly once per press
end
```

Some interactions genuinely *want* the repeat — dragging a paddle, painting,
scrubbing a slider. Those read `x`/`y` on every event and hold no state:

```ruby
def on_touch(x, y, pressed)
  return unless pressed
  @paddle_x = clamp(x - PADDLE_W / 2)
end
```

The rule is about **acting**, not about moving. Ask: "if the user rests their
finger here for a second, should this happen sixty times?" If not, guard it.

A second pattern does the same job without a flag, when the action is a state
change: compare against what is already true.

```ruby
def on_touch(x, y, pressed)
  offset = hit_test(x)
  return if offset == @active_offset   # same key still held — nothing new
  @active_offset = offset
  acid_play_note(VOICE, ROOT_ONA + offset, 45)
end
```

That is how `piano.rb` gets one note per key press while a finger slides across
the keyboard retriggering correctly at each boundary.

---

[← Apps and manifests](02-apps-and-manifests.md) · [Contents](README.md) · [Next: Graphics →](04-graphics.md)
