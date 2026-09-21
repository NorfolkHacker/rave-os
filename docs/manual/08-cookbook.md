# 8. Cookbook

[← System APIs](07-system-apis.md) · [Contents](README.md) · [Next: API reference →](09-api-reference.md)

Recipes, conventions, and the mistakes that cost the most time.

## 8.1 Testing an app without the simulator

The simulator links the **same `libmruby.a`** that the standalone `mruby` binary
is built from, with the same gem set. So you can exercise your app's logic on
the host, with the `acid_*` calls stubbed — which is fast, scriptable, and the
only practical way to check that your sound bookkeeping really releases every
voice.

Save this as `harness.rb` outside the repo:

```ruby
$calls = Hash.new(0)
$gated = {}

module Kernel
  def acid_fill_rect(x, y, w, h, c); $calls[:fill_rect] += 1; nil; end
  def acid_fill_circle(x, y, r, c); $calls[:fill_circle] += 1; nil; end
  def acid_draw_text(s, x, y, f, b); $calls[:draw_text] += 1; nil; end
  def acid_clear_user_area; $calls[:clear] += 1; nil; end
  def acid_draw_window_frame(t); $calls[:frame] += 1; nil; end
  def acid_draw_window_border; $calls[:border] += 1; nil; end
  def acid_am_i_focused; true; end
  def acid_notify_redraw_done; nil; end
  def acid_launch_arg; ""; end

  # The audio stubs validate every argument range, so an out-of-range
  # note or voice is a loud failure on the host instead of silence on
  # the device.
  def acid_configure_filter(c, r, m)
    raise "cutoff #{c}" if c < 0 || c > 255
    raise "resonance #{r}" if r < 0 || r > 15
    $calls[:filter] += 1; nil
  end

  def acid_configure_voice(v, fr, a, d, s, r)
    raise "voice #{v}" if v < 0 || v > 7
    raise "filter_route #{fr}" unless fr == 0 || fr == 1
    raise "sustain #{s}" if s < 0 || s > 100
    $calls[:cfg_voice] += 1; nil
  end

  def acid_configure_osc(v, w, d)
    raise "voice #{v}" if v < 0 || v > 7
    raise "waveform #{w}" if w < 0 || w > 3
    $calls[:cfg_osc] += 1; nil
  end

  def acid_play_note(v, o, vol)
    raise "voice #{v}" if v < 0 || v > 7
    raise "ona #{o} outside 1..88" if o < 1 || o > 88
    raise "volume #{vol}" if vol < 0 || vol > 100
    $gated[v] = true
    $calls[:play] += 1; nil
  end

  def acid_stop_note(v); $gated[v] = false; $calls[:stop] += 1; nil; end

  def acid_trigger_arp(v, n0, n1, n2, n3, count, rate)
    [n0, n1, n2, n3].each { |n| raise "arp ona #{n}" if n < 1 || n > 88 }
    raise "arp count #{count}" if count < 1 || count > 4
    $calls[:arp] += 1; nil
  end

  def acid_set_ring_partner(v, p); $calls[:ring] += 1; nil; end
  def acid_set_volume(p); nil; end
  def acid_get_volume; 100; end
  def acid_active_voice_count; $gated.values.select { |x| x }.length; end

  # Feeds scripted events, honours the timeout so Time-based tick loops
  # really tick, and ends the run after $run_seconds.
  def acid_poll_event(timeout_ms)
    $deadline_at ||= Time.now.to_f + $run_seconds
    return :close if Time.now.to_f > $deadline_at
    ev = $events.shift
    return ev if ev
    deadline = Time.now.to_f + (timeout_ms / 1000.0)
    while Time.now.to_f < deadline; end
    nil
  end
end
```

Then drive your app:

```sh
cd /path/to/rave-os
MRB=v2/components/mruby/build/host/bin/mruby

cat > /tmp/drive.rb <<'RB'
$run_seconds = 4.0
$events = []
$events << [60, 150, true] << [60, 150, true] << [60, 150, false]
$events << [:key, 259, true]
RB

cat /tmp/harness.rb /tmp/drive.rb \
    v2/apps/lib/acid_keys.rb v2/apps/lib/acid_palette.rb \
    v2/apps/lib/acid_waveform.rb v2/apps/lib/acid_app.rb \
    v2/apps/lib/acid_game.rb v2/apps/mygame.rb > /tmp/run.rb

echo 'puts "calls: #{$calls}"
puts "still gated: #{$gated.select { |k, v| v }.keys.inspect}"' >> /tmp/run.rb

$MRB /tmp/run.rb
```

What to look for:

- **It exits 0.** Any Ruby error in your app surfaces here with a line number.
- **`still gated: []`.** Anything else is a note you never stopped.
- **The call counts are plausible.** `fill_rect` in the tens of thousands for a
  four-second run means you are repainting far more than you need to.

Concatenating the files is exactly what the VM host does, in the same order, so
the load semantics match. The stubs are not the real kernel — they will not tell
you that your layout is ugly — but they catch the whole class of bugs that are
tedious to find by clicking.

## 8.2 Conventions worth following

**Declare `WINDOW_W`/`WINDOW_H` and keep them matched to the manifest.** Nothing
enforces it. See [§2.2](02-apps-and-manifests.md#sizing-a-window).

**Name your theme colours.** `BG_COLOR = 0x050607 # THEME_BG` reads better than a
literal in twenty places, and it is what every app in the tree does.

**Use `while` loops over `each` in hot paths.** The existing code does, and
consistently — it is a microcontroller, and an allocation-free loop in a 20 Hz
tick is a reasonable default habit. `each` is fine everywhere else.

**Configure every synth voice you use in `on_create`.** The defaults are a harsh
unfiltered pulse with instant envelopes. See
[§5.4](05-sound.md#54-shaping-the-voice-acid_configure_voice).

**Pick voice numbers nobody else uses, and say so in a comment.** See the table
in [§5.1](05-sound.md#voice-ownership-and-voice-stealing).

**Comment the *why*, not the *what*.** This codebase's comments explain
decisions — why the border is drawn last, why the envelope is fixed-point, why
`tick_sfx` is unguarded. Match that and the next person (probably you) will
thank you.

## 8.3 Common mistakes

### Nothing appears

- No `.app.toml`, or it is missing `name`, `w` or `h` → silently never
  registered. Check the manifest first, always.
- Simulator not restarted after adding a *new* app. Existing apps reload from
  disk on every launch; the registry is built once at boot.
- Not running from the repository root, so `v2/apps/...` does not resolve.
- You forgot `MyApp.new.start` on the last line, so the class was defined and
  nothing ran.

### The window is there but empty

- `redraw` never called. `AcidApp` calls it once at startup; `AcidGame` **never**
  calls it — a game must paint from `on_tick`.
- You drew before `acid_clear_user_area`, which then wiped it.
- Drawing at `y < 16`, underneath the title bar.

### Content is clipped or missing at an edge

- `WINDOW_W`/`WINDOW_H` disagree with the manifest.
- `acid_draw_text` whose origin is outside the window draws **nothing at all** —
  it is an origin check, not per-glyph clipping.
- `acid_fill_circle` is **not** clipped to the window, unlike `acid_fill_rect`.

### The border vanishes

`acid_draw_window_border` must be the **last** call in `redraw`. Your content is
drawn over exactly those pixels.

### A note never stops

You did not call `acid_stop_note`. Nothing else ever will. See
[§6.4](06-games.md#64-the-sound-effect-lifecycle) — and check
`acid_active_voice_count` returns to zero.

### An arpeggio loops forever

Same cause. An arp cycles until the voice is gated off; stop it after
`rate_ms × count` milliseconds if you meant one pass.

### An action fires many times from one tap

Missing touch debounce. A held press repeats every poll. See
[§3.5](03-app-lifecycle.md#35-touch-debouncing).

### A hit test misfires during a drag

A gesture belongs to the window the press started in, so `on_touch` keeps
receiving events after the finger leaves your window — with `x`/`y` negative or
past your width. Clamp before hit-testing. See
[§3.1](03-app-lifecycle.md#on_touchx-y-pressed).

### A game paints over other windows

Missing `focused?` guard around the draw. See
[§6.3](06-games.md#63-focus-and-the-z-order-trap).

### Dragging a window is sluggish

You are not calling `acid_notify_redraw_done` after handling `:moved`, so the
compositor waits the full poll timeout on your window for every drag. `AcidApp`
and `AcidGame` both do it for you — this only bites in a hand-written loop.

### Colours come out black

RGB565 literals. Use full 24-bit `0xRRGGBB`.

### A `multi` app's two windows disagree

They are two separate VMs with no shared state. That is by design. Anything that
must be true system-wide has to be enforced by the kernel.

### An app crashes with no message

Look at the terminal you launched the simulator from. That is where mruby
exception traces go.

## 8.4 Recipes

### Centring text

Glyphs are 6×8 pixels.

```ruby
def draw_centred(text, y, fg, bg)
  x = (WINDOW_W - text.length * 6) / 2
  acid_draw_text(text, x, y, fg, bg)
end
```

### A scrolling list

```ruby
ROW_H = 12
VISIBLE = (WINDOW_H - TITLE_BAR_H - 4) / ROW_H

def draw_list
  i = 0
  while i < VISIBLE && @scroll + i < @items.length
    item = @items[@scroll + i]
    y = TITLE_BAR_H + 2 + i * ROW_H
    selected = (@scroll + i) == @selected
    acid_fill_rect(1, y, WINDOW_W - 2, ROW_H, selected ? ACCENT : BG)
    acid_draw_text(item[0, 30], 4, y + 2, selected ? BG : TEXT,
                   selected ? ACCENT : BG)
    i += 1
  end
end

def scroll_to(index)
  @selected = index
  @scroll = index if index < @scroll
  @scroll = index - VISIBLE + 1 if index >= @scroll + VISIBLE
  @scroll = 0 if @scroll < 0
end
```

### A progress bar

```ruby
def draw_bar(x, y, w, h, fraction, fg, bg)
  acid_fill_rect(x, y, w, h, bg)
  filled = (w * fraction).to_i
  filled = 0 if filled < 0
  filled = w if filled > w
  acid_fill_rect(x, y, filled, h, fg)
end
```

### A repainting-only-when-changed loop

```ruby
def state_signature
  [@score, @lives, @selected, @mode]
end

def redraw_if_changed
  sig = state_signature
  return if sig == @drawn_sig
  @drawn_sig = sig
  redraw
end
```

### A button that responds to press and release

```ruby
def on_touch(x, y, pressed)
  hit = in_button?(x, y)
  if pressed
    return if @pressed
    @pressed = hit
    draw_button
  else
    do_action if @pressed && hit
    @pressed = false
    draw_button
  end
end
```

### A confirm-before-quit

```ruby
def on_key(code, pressed)
  return unless pressed
  if @confirming
    quit! if code == "y".ord
    @confirming = false
    redraw
  elsif code == AcidKeys::ESCAPE
    @confirming = true
    redraw
  end
end
```

Remember `quit!` has no effect in an `AcidGame`.

### Colour-cycling a whole window

```ruby
def poll_timeout_ms
  40
end

def on_idle
  return unless focused?
  @step += 3
  redraw
end

def redraw
  acid_clear_user_area
  acid_draw_window_frame(window_title)
  y = TITLE_BAR_H
  row = 0
  while y < WINDOW_H
    h = (y + 8 > WINDOW_H) ? WINDOW_H - y : 8
    acid_fill_rect(0, y, WINDOW_W, h, AcidPalette.hue(@step + row * 12))
    y += 8
    row += 1
  end
  acid_draw_window_border
end
```

### Playing a short melody

```ruby
MELODY = [ [40, 2], [44, 2], [47, 2], [52, 4], [47, 2], [52, 6] ]  # [ona, ticks]

def start_melody
  @melody_index = 0
  @melody_ticks = 0
end

def tick_melody
  return unless @melody_index
  if @melody_ticks <= 0
    if @melody_index >= MELODY.length
      acid_stop_note(LEAD_VOICE)
      @melody_index = nil
      return
    end
    ona, ticks = MELODY[@melody_index]
    acid_play_note(LEAD_VOICE, ona, 55)
    @melody_ticks = ticks
    @melody_index += 1
  end
  @melody_ticks -= 1
end
```

---

[← System APIs](07-system-apis.md) · [Contents](README.md) · [Next: API reference →](09-api-reference.md)
