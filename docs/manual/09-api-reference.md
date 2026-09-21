# 9. API reference

[← Cookbook](08-cookbook.md) · [Contents](README.md)

Every `acid_*` function is a **module function on `Kernel`**, so it is callable
from anywhere with no receiver. All of them take and return plain integers,
strings, booleans, arrays, symbols or `nil`.

Out-of-range arguments are **clamped or ignored**, never raised. Nothing in this
API throws.

## Index by area

**Graphics** — [`acid_fill_rect`](#acid_fill_rect) · [`acid_fill_circle`](#acid_fill_circle) · [`acid_draw_text`](#acid_draw_text)

**Chrome** — [`acid_clear_user_area`](#acid_clear_user_area) · [`acid_draw_window_frame`](#acid_draw_window_frame) · [`acid_draw_window_border`](#acid_draw_window_border) · [`acid_repaint_region`](#acid_repaint_region)

**Overlay** — [`acid_overlay_open`](#acid_overlay_open) · [`acid_overlay_clear`](#acid_overlay_clear) · [`acid_overlay_fill_rect`](#acid_overlay_fill_rect) · [`acid_overlay_close`](#acid_overlay_close)

**Events** — [`acid_poll_event`](#acid_poll_event) · [`acid_notify_redraw_done`](#acid_notify_redraw_done)

**Audio** — [`acid_play_note`](#acid_play_note) · [`acid_stop_note`](#acid_stop_note) · [`acid_configure_voice`](#acid_configure_voice) · [`acid_configure_osc`](#acid_configure_osc) · [`acid_configure_filter`](#acid_configure_filter) · [`acid_set_ring_partner`](#acid_set_ring_partner) · [`acid_trigger_arp`](#acid_trigger_arp) · [`acid_set_volume`](#acid_set_volume) · [`acid_get_volume`](#acid_get_volume) · [`acid_active_voice_count`](#acid_active_voice_count)

**Windows** — [`acid_window_max`](#acid_window_max) · [`acid_window_info`](#acid_window_info) · [`acid_activate_window`](#acid_activate_window) · [`acid_close_window`](#acid_close_window) · [`acid_send_self_to_back`](#acid_send_self_to_back) · [`acid_am_i_focused`](#acid_am_i_focused)

**Launching** — [`acid_launcher_register`](#acid_launcher_register) · [`acid_launcher_count`](#acid_launcher_count) · [`acid_launcher_name`](#acid_launcher_name) · [`acid_launcher_path`](#acid_launcher_path) · [`acid_launcher_spawn`](#acid_launcher_spawn) · [`acid_spawn_app`](#acid_spawn_app) · [`acid_launch_arg`](#acid_launch_arg)

**System** — [`acid_refresh_tasks`](#acid_refresh_tasks) · [`acid_task_count`](#acid_task_count) · [`acid_task_info`](#acid_task_info) · [`acid_mem_used_kb`](#acid_mem_used_kb) · [`acid_composited_frames`](#acid_composited_frames) · [`acid_skipped_frames`](#acid_skipped_frames) · [`acid_network_info`](#acid_network_info) · [`acid_set_wallpaper_enabled`](#acid_set_wallpaper_enabled) · [`acid_get_wallpaper_enabled`](#acid_get_wallpaper_enabled)

---

## Functions, alphabetically

### `acid_activate_window`

```ruby
acid_activate_window(index) # => nil
```

Raises the window at `index` (an [`acid_window_info`](#acid_window_info) index)
to the front and gives it focus. Out-of-range or empty index: no-op.

### `acid_active_voice_count`

```ruby
acid_active_voice_count # => 0..8
```

How many synth voices have a sounding envelope, as of the most recent audio
callback. Diagnostic. See [§5.9](05-sound.md#59-master-volume-and-metering).

### `acid_am_i_focused`

```ruby
acid_am_i_focused # => true | false
```

True if the calling app's own window holds keyboard focus — which in this OS
also means it is the topmost visible window. Wrapped by `AcidApp#focused?`.

### `acid_clear_user_area`

```ruby
acid_clear_user_area # => nil
```

Fills everything below the 16px title bar with `THEME_BG` (`0x050607`). Does not
touch the title bar.

### `acid_close_window`

```ruby
acid_close_window(index) # => true | false
```

Ends the window at `index`. Returns `false` for an out-of-range or empty index,
and **refuses to close the calling app's own window**.

### `acid_composited_frames`

```ruby
acid_composited_frames # => Integer
```

Frames the compositor has painted since boot.

### `acid_configure_filter`

```ruby
acid_configure_filter(cutoff, resonance, mode) # => nil
```

Sets the **one shared** filter. Global — affects every routed voice in every app.

| Argument | Range | |
|---|---|---|
| `cutoff` | 0–255 | Corner frequency. Clamped. |
| `resonance` | 0–15 | Q from ≈0.707 to 8.0. Clamped. |
| `mode` | bitmask | `1` low-pass, `2` band-pass, `4` high-pass. Combinable; `0` mutes routed voices. |

Only affects voices whose `filter_route` is 1 (see
[`acid_configure_voice`](#acid_configure_voice)).
See [§5.6](05-sound.md#56-the-filter-acid_configure_filter).

### `acid_configure_osc`

```ruby
acid_configure_osc(voice, waveform, duty_percent) # => nil
```

| Argument | Range | |
|---|---|---|
| `voice` | 0–7 | Out of range ignored. |
| `waveform` | 0–3 | `AcidWaveform::PULSE`/`SAW`/`TRIANGLE`/`NOISE`. |
| `duty_percent` | 1–99 | Clamped. Audible only on `PULSE`. Default 50. |

See [§5.5](05-sound.md#55-the-oscillator-acid_configure_osc).

### `acid_configure_voice`

```ruby
acid_configure_voice(voice, filter_route, attack_ms, decay_ms,
                     sustain_percent, release_ms) # => nil
```

| Argument | Range | |
|---|---|---|
| `voice` | 0–7 | |
| `filter_route` | 0 or 1 | 1 routes this voice through the shared filter. |
| `attack_ms` | ms | Silence → full. |
| `decay_ms` | ms | Full → sustain. |
| `sustain_percent` | 0–100 | Clamped. **Overwritten by every `acid_play_note`'s `volume`.** |
| `release_ms` | ms | Current level → silence, after gate-off. |

Defaults on a fresh voice: pulse wave, 50% duty, instant attack/decay/release,
unfiltered. See [§5.4](05-sound.md#54-shaping-the-voice-acid_configure_voice).

### `acid_draw_text`

```ruby
acid_draw_text(str, x, y, fg, bg) # => nil
```

Draws `str` with its top-left corner at `(x, y)` in window-relative coordinates.
Fixed-width bitmap font, **6×8 pixels per glyph**, opaque background — `bg` must
match what is already behind the text.

**Coarse origin guard:** if `(x, y)` is outside the window, nothing is drawn at
all. There is **no per-glyph clipping**, so a long string starting inside runs
past the right edge. Truncate with `str[0, n]`.

### `acid_draw_window_border`

```ruby
acid_draw_window_border # => nil
```

Draws a 1px `THEME_HARD` (`0x00FF66`) outline around the whole window, then cuts
the four rounded corners. **Must be the last call in your redraw** — your content
is drawn over exactly these pixels.

### `acid_draw_window_frame`

```ruby
acid_draw_window_frame(title) # => nil
```

Fills the 16px title bar with `THEME_PANEL`, draws `title` at its left in
`THEME_TEXT`, and the `THEME_HARD` close dot at its right. The title is **not**
clipped against the close button — keep it to 16 characters.

### `acid_fill_circle`

```ruby
acid_fill_circle(x, y, r, color) # => nil
```

Filled circle centred at `(x, y)`, radius `r`, in window-relative coordinates.
**Not clipped** to the window — clamp your own coordinates.

### `acid_fill_rect`

```ruby
acid_fill_rect(x, y, w, h, color) # => nil
```

Filled rectangle in window-relative coordinates. `color` is 24-bit `0xRRGGBB`.

**Clipped against the window's bounds**: a rectangle running off an edge is
trimmed, one entirely outside draws nothing. You cannot paint outside your own
window.

### `acid_get_volume`

```ruby
acid_get_volume # => 0..100
```

System-wide output gain.

### `acid_get_wallpaper_enabled`

```ruby
acid_get_wallpaper_enabled # => true | false
```

### `acid_launch_arg`

```ruby
acid_launch_arg # => String
```

The optional startup string this app was spawned with, or `""`. Safe to read
more than once.

### `acid_launcher_count`

```ruby
acid_launcher_count # => Integer
```

Number of apps in the launcher registry, including those hidden from the Menu
with `menu = false`.

### `acid_launcher_name`

```ruby
acid_launcher_name(index) # => String | nil
```

The registered display name, or `nil` for an out-of-range index.

### `acid_launcher_path`

```ruby
acid_launcher_path(index) # => String | nil
```

The registered script path (`"v2/apps/tetris.rb"`), or `nil`.

### `acid_launcher_register`

```ruby
acid_launcher_register(path, name, w, h, multi, libs) # => true | false
```

Adds an app to the launcher registry. `multi` is a boolean, `libs` a
comma-separated string (`""` for none). Returns `false` if the registry is full
or an argument is unusable.

Called by `desktop.rb`'s boot-time manifest scan. You would only call it
yourself when writing a replacement desktop.

### `acid_launcher_spawn`

```ruby
acid_launcher_spawn(index) # => true | false
```

Launches the registered app at `index` with its recorded size, `multi` flag and
`libs`. Returns `true` on success — **including** when the app was a singleton
and already open, in which case its existing window is raised and focused.

### `acid_mem_used_kb`

```ruby
acid_mem_used_kb # => Integer
```

Kilobytes of memory in use.

### `acid_network_info`

```ruby
acid_network_info # => [hostname, ip, connected]
```

`connected` means an address was found, **not** that anything is reachable.
There is no live check and no socket API.

### `acid_notify_redraw_done`

```ruby
acid_notify_redraw_done # => nil
```

Signals that this window has finished the repaint triggered by a `:moved` event.
The compositor waits for it before repainting the next window in z-order.
`AcidApp` and `AcidGame` both call it for you; a hand-written loop must.

### `acid_overlay_clear`

```ruby
acid_overlay_clear # => nil
```

Fills the whole overlay canvas with the transparency key (`0xFF00FF`). No-op if
you do not own the overlay.

### `acid_overlay_close`

```ruby
acid_overlay_close # => nil
```

Hides the overlay and releases your claim. No-op unless you are the current
owner, so you can never close someone else's animation. Released automatically
if your app exits.

### `acid_overlay_fill_rect`

```ruby
acid_overlay_fill_rect(x, y, w, h, color) # => nil
```

Filled rectangle on the overlay, in **screen-absolute** coordinates on a 640×360
screen. Clipped against the screen, so negative coordinates are normal.
Silently draws nothing if you do not own the overlay.

### `acid_overlay_open`

```ruby
acid_overlay_open # => true | false
```

Claims the kernel's single full-screen overlay canvas and clears it to the
transparency key. `false` means another task already holds it — **not an error**;
an effect that cannot start should simply do nothing. Re-opening from the owning
task succeeds and re-clears. See [§4.5](04-graphics.md#45-the-overlay).

### `acid_play_note`

```ruby
acid_play_note(voice, ona, volume) # => nil
```

| Argument | Range | |
|---|---|---|
| `voice` | 0–7 | Out of range ignored. |
| `ona` | 1–88 | 88-key piano numbering; 49 = A4 = 440 Hz. Out of range leaves pitch unchanged. |
| `volume` | 0–100 | Clamped. **Becomes the voice's sustain level.** |

Sets pitch, sets sustain level from `volume`, and gates the envelope on —
restarting it from zero, so retriggering replays the attack. A voice that has
never had a pitch and is not arpeggiating silently does nothing (this avoids a
DC thump).

**The note sounds until `acid_stop_note`.** There is no duration argument.

### `acid_poll_event`

```ruby
acid_poll_event(timeout_ms) # => nil | :close | :moved | Array
```

Blocks on this window's event queue for up to `timeout_ms`.

| Return | Meaning |
|---|---|
| `nil` | Timed out, nothing waiting |
| `:close` | Close button, or the kernel ending this app |
| `:moved` | Window was dragged — repaint, then `acid_notify_redraw_done` |
| `[:key, code, pressed]` | Key event; `code` is ASCII or an `AcidKeys` constant |
| `[x, y, pressed]` | Touch event, window-relative — may fall outside the window during a drag |

`timeout_ms` reaches FreeRTOS as an unsigned tick count. `0` busy-spins;
negative becomes an effectively infinite block. `AcidApp` clamps to a minimum of
1.

### `acid_refresh_tasks`

```ruby
acid_refresh_tasks # => Integer
```

Samples the FreeRTOS task table and returns how many tasks it found. Call before
[`acid_task_info`](#acid_task_info).

### `acid_repaint_region`

```ruby
acid_repaint_region(x, y, w, h) # => nil
```

Fills that region **of your own canvas** with the wallpaper, erasing your claim
on it. The next composite then shows whatever is really underneath. Despite the
name it asks nothing of any other window.

### `acid_send_self_to_back`

```ruby
acid_send_self_to_back # => nil
```

Drops the calling app's own window to the back of the z-order. Always targets
the caller.

### `acid_set_ring_partner`

```ruby
acid_set_ring_partner(voice, partner) # => nil
```

Pairs `voice` with `partner` (0–7) for ring modulation, or clears it if
`partner` is negative. **Audible only on a `TRIANGLE` voice** — this matches the
real SID, whose ring modulator is wired into the triangle generator. The partner
contributes only its oscillator phase and need not be gated on.

### `acid_set_volume`

```ruby
acid_set_volume(percent) # => nil
```

System-wide output gain, 0–100, clamped. A **system setting** — the Config app
owns it. Scale your own note volumes instead.

### `acid_set_wallpaper_enabled`

```ruby
acid_set_wallpaper_enabled(enabled) # => nil
```

System-wide. Flips the flag and asks the compositor to recomposite.

### `acid_skipped_frames`

```ruby
acid_skipped_frames # => Integer
```

Frames the compositor skipped because nothing was dirty.

### `acid_spawn_app`

```ruby
acid_spawn_app(path, w, h, arg) # => true | false
```

Launches an app by script path. The `multi` flag and `libs` come from the
registry, looked up by **exact path** — an unregistered path gets singleton
behaviour and no modules. `arg` is the startup string; `""` for none.

Canonicalise paths that came from the filesystem with
`AcidApp#canonical_app_path` — `v2/fsroot/App` is a symlink to `v2/apps` and the
registry does not follow it.

### `acid_stop_note`

```ruby
acid_stop_note(voice) # => nil
```

Gates the voice off (starting its release) and stops any arpeggio it was
running. **Unconditional** — it silences whichever voice is there regardless of
which app started it.

### `acid_task_count`

```ruby
acid_task_count # => Integer
```

Tasks in the most recent [`acid_refresh_tasks`](#acid_refresh_tasks) sample.

### `acid_task_info`

```ruby
acid_task_info(index) # => [name, state, cpu_percent] | nil
```

`state` is a string (`"running"`, `"ready"`, `"blocked"`, `"suspended"`, `"deleted"`, `"?"`).
`nil` for an out-of-range index.

### `acid_trigger_arp`

```ruby
acid_trigger_arp(voice, note0, note1, note2, note3, count, rate_ms) # => nil
```

Steps `voice` through `count` of the four **absolute `ona` values**, cycling
upward with wraparound, `rate_ms` per step.

| Argument | Range | |
|---|---|---|
| `voice` | 0–7 | |
| `note0`–`note3` | 1–88 | Absolute pitches, not offsets. Unused slots must still be valid — pass `1`. |
| `count` | 1–4 | How many slots to use. |
| `rate_ms` | ms | Per step. |

Call **immediately after `acid_play_note`** on the same voice: `play_note` sets
the volume, envelope and gate; this drives pitch-stepping on top. The pattern
restarts at slot 0 on gate-on.

**It cycles forever until `acid_stop_note`.** See
[§6.4](06-games.md#64-the-sound-effect-lifecycle).

### `acid_window_info`

```ruby
acid_window_info(index) # => [name, x, y, w, h, focused] | nil
```

Screen coordinates. `nil` for an empty or out-of-range slot — walk `0` to
[`acid_window_max`](#acid_window_max) and skip the gaps.

### `acid_window_max`

```ruby
acid_window_max # => Integer
```

Capacity of the window table; the exclusive upper bound for
[`acid_window_info`](#acid_window_info) indices.

---

## Library modules

All in `v2/apps/lib/`.

### `AcidApp` — always loaded

Base class for event-driven apps. [Chapter 3](03-app-lifecycle.md).

| Member | |
|---|---|
| `on_create` | Once, before the first paint |
| `on_touch(x, y, pressed)` | Window-relative touch |
| `on_key(code, pressed)` | Key event; focused window only |
| `on_idle` | Every `poll_timeout_ms` with no event |
| `on_destroy` | Once, after the loop ends |
| `redraw` | Full repaint; default draws bare chrome |
| `poll_timeout_ms` | Method, default `200`; clamped to ≥1 |
| `window_title` | Derived from the class name, capped at 16 chars |
| `focused?` | Wraps `acid_am_i_focused` |
| `quit!` | Ends the loop (**no effect in `AcidGame`**) |
| `canonical_app_path(path)` | Maps `v2/fsroot/App/…` → `v2/apps/…` |
| `start` | Runs the event loop |

### `AcidGame` — always loaded

`AcidApp` subclass with a fixed-tick loop. [Chapter 6](06-games.md).

| Member | |
|---|---|
| `TICK_MS` | **Constant** on your class; tick interval |
| `on_tick` | Every `TICK_MS`, regardless of events |
| `start` | Own loop; never calls `redraw`, ignores `quit!`, acknowledges `:moved` without repainting |

### `AcidKeys` — always loaded

```ruby
ENTER = 257   BACKSPACE = 258   ESCAPE = 259   TAB = 260   DELETE = 261
UP = 262      DOWN = 263        LEFT = 264     RIGHT = 265
```

Printable keys arrive as their ASCII byte.

### `AcidPalette` — always loaded

```ruby
AcidPalette.hue(step)          # 256-step HSV hue wheel => 0xRRGGBB
AcidPalette.hue(step, steps)   # a wheel of `steps` divisions
```

Integer-only, full saturation and value. `step` wraps. For **content**; use the
theme colours for UI. [§4.1](04-graphics.md#41-colours).

### `AcidWaveform` — always loaded

```ruby
PULSE = 0   SAW = 1   TRIANGLE = 2   NOISE = 3
```

### `AcidSprite` — add `lib/acid_sprite.rb` to `libs`

Draws character-grid pictures onto the **overlay** (screen-absolute).

```ruby
AcidSprite.draw(rows, x, y, scale, palette, flip = false)
AcidSprite.width(rows)    # in characters
AcidSprite.height(rows)   # in characters
```

`rows` is an array of equal-length strings, one character per pixel; `.` is
transparent; `palette` maps character → `0xRRGGBB`. Runs of identical colour are
merged into single rectangles. [§4.6](04-graphics.md#46-sprites).

## Theme colours

C defines in `core/kernel/kernel_theme.h`, not exposed to Ruby. Declare the ones
you use as your own constants.

| Name | Value |
|---|---|
| `THEME_BG` | `0x050607` |
| `THEME_HARD` | `0x00FF66` |
| `THEME_PANEL` | `0x0B1712` |
| `THEME_TEXT` | `0xD4E6DB` |
| `THEME_MUTED` | `0x9DAAA3` |
| `THEME_VIOLET` | `0xB026FF` |
| overlay transparency key | `0xFF00FF` |

## Fixed geometry

| | |
|---|---|
| Screen | 640 × 360 |
| Title bar height | 16 |
| Window border | 1px, all round |
| Desktop top strip | 24 |
| Font glyph | 6 × 8 |
| Synth voices | 8 (0–7) |
| Synth sample rate | 22,050 Hz |
| Arpeggio slots | 4 |
| `ona` range | 1–88 (49 = A4 = 440 Hz) |

---

[← Cookbook](08-cookbook.md) · [Contents](README.md)
