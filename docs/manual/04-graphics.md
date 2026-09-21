# 4. Graphics

[← The app lifecycle](03-app-lifecycle.md) · [Contents](README.md) · [Next: Sound →](05-sound.md)

There are three drawing primitives — rectangle, circle, text — plus the window
chrome and a separate full-screen overlay. That is the whole graphics API. There
is no line, no arc, no rounded rectangle, no alpha, no image loading. Everything
in this OS, from Tetris pieces to the rounded window corners to the sprites that
fly across the screen, is built from filled rectangles.

Lean into it. The look is deliberate.

## 4.1 Colours

Colours are plain **24-bit RGB integers**: `0xRRGGBB`.

```ruby
acid_fill_rect(10, 20, 50, 30, 0xFF00AA)
```

> Not RGB565. Early bring-up code used RGB565-style literals like `0xF800` and
> they rendered as near-black. If a colour comes out wrong and dark, check that
> it is a full 24-bit value.

### The theme palette

Six colours define the OS's identity. They are C defines (`core/kernel/kernel_theme.h`),
not exposed to Ruby, so apps declare the ones they use as their own constants —
which is why you see the same literals across the tree:

| Constant | Value | Used for |
|---|---|---|
| `THEME_BG` | `0x050607` | Window body background, desktop |
| `THEME_HARD` | `0x00FF66` | Acid green — borders, accents, pressed states |
| `THEME_PANEL` | `0x0B1712` | Title bars, panels |
| `THEME_TEXT` | `0xD4E6DB` | Body text |
| `THEME_MUTED` | `0x9DAAA3` | Secondary text |
| `THEME_VIOLET` | `0xB026FF` | Launchable/executable accent |

```ruby
BG_COLOR = 0x050607      # THEME_BG
TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
ACCENT_COLOR = 0x00FF66  # THEME_HARD
```

Use the theme for **UI** — anything that should look like part of the system.

### `AcidPalette` — the hue wheel

For **content**, use the 256-colour hue wheel. `AcidPalette` is always loaded;
you never list it in `libs`.

```ruby
AcidPalette.hue(step)            # 256 steps around the wheel
AcidPalette.hue(step, steps)     # a wheel of `steps` divisions
```

A full HSV hue wheel at maximum saturation and value, walked in even increments
— a real continuous rainbow, integer-only, no `Math` needed. `step` wraps, so
you can feed it a counter that grows forever.

```ruby
# a distinct, vivid colour per item
@items.each_with_index do |item, i|
  acid_fill_rect(0, i * ROW_H, WINDOW_W, ROW_H, AcidPalette.hue(i * 20))
end

# colour cycling over time
def on_idle
  @step += 3
  redraw
end
# ... acid_fill_rect(x, y, w, h, AcidPalette.hue(@step + row * 12))
```

This exists because apps used to draw *content* from the five theme colours too,
and everything ended up looking like the same few shades of green.

## 4.2 Coordinates and clipping

All window drawing is in **window-relative** coordinates. `(0, 0)` is your
window's own top-left corner. Your window is `w × h` as declared in your
manifest; the title bar is the top 16 pixels and the border is 1 pixel all
round.

### `acid_fill_rect(x, y, w, h, color)`

The workhorse. **Clipped against your window's bounds** — a rectangle that runs
off an edge is trimmed, and one entirely outside draws nothing. You cannot paint
onto the desktop or another app's window by miscalculating a coordinate.

```ruby
acid_fill_rect(0, 16, WINDOW_W, WINDOW_H - 16, BG_COLOR)  # fill the body
acid_fill_rect(x, y, 1, h, LINE_COLOR)                    # a 1px vertical line
acid_fill_rect(x, y, w, 1, LINE_COLOR)                    # a 1px horizontal line
```

Clipping is genuinely useful, not just a safety net. Piano draws a divider line
at `x = 0` for every white key and lets the first one clip harmlessly onto the
border, instead of special-casing index 0.

### `acid_fill_circle(x, y, r, color)`

`x, y` is the **centre**, `r` the radius.

```ruby
acid_fill_circle(ball_x, ball_y, 2, 0x00FF66)
```

> Unlike `acid_fill_rect`, this is **not** clipped to your window. Clamp your own
> coordinates for anything that can move off an edge.

### `acid_draw_text(str, x, y, fg, bg)`

Fixed-width bitmap font, **6×8 pixels per glyph**, drawn with an opaque background —
`bg` must be the colour that is already behind the text, or you get a box.

```ruby
acid_draw_text("SCORE #{@score}", 6, 20, TEXT_COLOR, BG_COLOR)
```

`x, y` is the **top-left** of the first glyph, not a baseline.

Guarded by a **coarse origin check**: if the starting point is outside your
window, nothing is drawn at all. There is no per-glyph clipping, so a string
that starts inside and runs long will draw past your window's right edge. Cut
strings to length yourself:

```ruby
acid_draw_text(name[0, 22], 6, y, TEXT_COLOR, BG_COLOR)
```

So a string of `n` characters is `6 * n` pixels wide and 8 tall — the arithmetic
you need for centring and for deciding where to truncate.

## 4.3 Window chrome

Three calls draw the parts of the window that belong to the system.

```ruby
def redraw
  acid_clear_user_area                    # 1. clear the body to THEME_BG
  acid_draw_window_frame(window_title)    # 2. title bar, title, close button
  # ... your content ...                  # 3. everything you draw
  acid_draw_window_border                 # 4. outline and rounded corners
end
```

| Call | Does |
|---|---|
| `acid_clear_user_area` | Fills everything below the title bar with `THEME_BG`. Does not touch the title bar. |
| `acid_draw_window_frame(title)` | Fills the 16px title bar with `THEME_PANEL`, draws `title` at its left, and the green close dot at its right. |
| `acid_draw_window_border` | 1px `THEME_HARD` outline around the whole window, then cuts the four rounded corners. |

### The border must come last

Your content is drawn in coordinates running from `(0, 0)` to the window's full
width and height — exactly where the border's own pixels sit. Draw the border
first and your content paints straight over it. This is why it is a separate
call rather than part of `acid_draw_window_frame`.

The rounded corners are a 3-pixel staircase, the technique classic low-resolution
GUIs used before anti-aliased curves were affordable. They must be cut *after*
the straight border lines, which `acid_draw_window_border` handles internally.

### Titles

`acid_draw_window_frame` does **not** clip the title against the close button.
Keep titles short — `AcidApp#window_title` caps at 16 characters for exactly
this reason.

## 4.4 Partial redraws and flicker

`acid_clear_user_area` + full repaint is correct and simple, and it is what you
want for the initial paint and for `:moved`. It is **not** what you want on
every keystroke or button press: clearing the whole body to blank and redrawing
it flashes visibly.

The fix is to repaint only what changed. `piano.rb` is the model:

```ruby
# Full repaint: initial paint, and after a window drag.
def redraw
  acid_clear_user_area
  acid_draw_window_frame(window_title)
  draw_white_keys
  draw_black_keys
  acid_draw_window_border
end

# Per-press: repaint only the one key whose state changed.
def redraw_offset(offset)
  # ...redraw that key, plus anything drawn on top of it...
end
```

Two things to watch when repainting a region:

- **Overlap.** If something is drawn on top of your region, repainting the
  region alone erases it. Piano repaints a white key *and* any black keys that
  straddle its edges.
- **The border.** If your region touches the window edge, call
  `acid_draw_window_border` again afterwards, or redraw a stripe shy of the edge.

A third technique, for apps that redraw on a timer: keep a **state signature** —
a plain comparable value of everything that affects the picture — and skip the
repaint entirely when it has not changed.

```ruby
def state_signature
  [@score, @lives, @level, @selected]
end

def redraw_if_changed
  sig = state_signature
  return if sig == @drawn_sig
  @drawn_sig = sig
  redraw
end
```

## 4.5 The overlay

The overlay is **one screen-sized canvas owned by the kernel**, composited last,
with magenta `0xFF00FF` treated as transparent. It is the only way to draw
across the *whole* screen — over the wallpaper, the desktop strip and every open
window, including your own.

It is not a window. It owns no task, is never hit-tested (clicks land on whatever
is really underneath), never takes focus, and never appears in the taskbar.

```ruby
acid_overlay_open        # claim it; true on success, false if someone else has it
acid_overlay_clear       # fill the whole screen with the transparency key
acid_overlay_fill_rect(x, y, w, h, color)   # screen-absolute coordinates
acid_overlay_close       # release the claim
```

### One owner at a time

`acid_overlay_open` returns `false` if another task already holds the overlay.
There is exactly one canvas, so a second animation starting mid-flight would
clear the first one's frames and the two would fight.

**A refusal is not an error.** An effect that declines to start because another
one is already running should simply do nothing:

```ruby
def start_effect
  return unless acid_overlay_open
  @frame = 0
  @running = true
end
```

Re-opening from the task that already owns it succeeds and re-clears — handy for
restarting your own animation.

The claim is released automatically if your app exits mid-animation, normally or
on an unhandled exception. `acid_overlay_close` is a no-op unless you are the
current owner, so you can never close someone else's animation.

### Coordinates are screen-absolute

`acid_overlay_fill_rect` takes screen coordinates on a 640×360 screen, not
window coordinates. It clips against the screen, so negative coordinates are
normal — that is how a sprite flies in from off-screen.

Drawing while you do not own the overlay silently does nothing. A sprite whose
animation ended a frame ago, or an effect that never got a canvas, must not be
an error anyone sees.

### An animation loop

```ruby
FRAME_MS = 33

def poll_timeout_ms
  @effect_running ? FRAME_MS : 200
end

def on_idle
  return unless @effect_running
  @x += 6
  if @x > 640
    acid_overlay_close
    @effect_running = false
    return
  end
  acid_overlay_clear
  AcidSprite.draw(SHIP, @x, 120, 3, SHIP_PALETTE)
end
```

Clear-then-draw means there is a brief window where the canvas is all-key with
nothing drawn yet. If a composite lands inside it, the worst case is one frame
with the sprite missing — tens of microseconds against a ~16 ms compositor tick.
Every window canvas in this OS already draws this way.

## 4.6 Sprites

`AcidSprite` (`lib/acid_sprite.rb`, add it to your manifest's `libs`) draws
pictures onto the overlay. A sprite is written **as a picture**: an array of
equal-length strings, one character per pixel, plus a palette. `.` is
transparent.

```ruby
TEAPOT = [ "..WWW..",
           ".WWWWW.",
           "WWWWWWW" ]
PALETTE = { "W" => 0xE8E8F0 }

AcidSprite.draw(TEAPOT, x, y, scale, PALETTE)
AcidSprite.draw(TEAPOT, x, y, scale, PALETTE, true)   # mirrored horizontally
AcidSprite.width(TEAPOT)    # in characters, not pixels — multiply by scale
AcidSprite.height(TEAPOT)
```

You edit a sprite by **redrawing it in the source**, not by recomputing
coordinates. `scale` multiplies each character into a `scale × scale` block, so
a 7×3 drawing at `scale = 4` is 28×12 on screen.

`flip` mirrors horizontally, so one drawing of a character can face either way —
what makes a sprite flying right-to-left look like it is facing where it is
going rather than flying backwards.

Runs of identical colour are merged into single rectangles. A 16×12 sprite is
about 190 pixels; at 30 fps that would be ~5,700 binding calls a second, each
taking the graphics lock. Merged, the same sprite is a few dozen.

## 4.7 The wallpaper

```ruby
acid_set_wallpaper_enabled(true)   # or false
acid_get_wallpaper_enabled         # => true/false
```

A system-wide setting; Config owns it. Toggling it only flips the flag and asks
the compositor to recomposite.

`acid_repaint_region(x, y, w, h)` fills that region **of your own canvas** with
the wallpaper. Despite the name it does not ask anyone else to repaint: under
the compositor the screen is recomputed fresh every frame from each window's
canvas, so erasing your own claim on a region is all that is needed — the next
composite shows whatever is really underneath. The desktop's dropdown-close is
the one caller.

---

[← The app lifecycle](03-app-lifecycle.md) · [Contents](README.md) · [Next: Sound →](05-sound.md)
