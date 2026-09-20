# acid OS v2: terminal easter eggs and the screen overlay — design

**Goal:** three hidden commands in `v2/apps/terminal.rb`. Typing `dave` flies a pixel
superman across the screen; `joe` flies a pixel teapot; `maximbady` bounces a small
figure (blue trousers, red top, green head and arms) around the screen a few times and
then flashes **SOOOOOOOOO** across the middle for a couple of seconds. All three are
video-game sprites, drawn over the whole 640x360 screen — over the wallpaper, over the
taskbar, over every open window — and all three are silent to the scrollback: the
animation *is* the response. Everything runs unmodified on `v2/sim`; `v2/hw` gets the
same source changes, statically verified only, per every prior phase's documented
ESP-IDF gap.

## The constraint that shapes the whole design

Nothing in acid OS v2 can draw outside its own window. Every app owns a private
offscreen canvas (`kernel_window.h`'s `canvas`), draws into it in window-relative
coordinates, and `kernel_router_composite_frame` (`core/kernel/kernel_router.c:107`) is
the only code that ever puts those pixels on the real screen: wallpaper first, then
every window's canvas blitted back-to-front in z-order, then one `gfx_present`.

Those blits are opaque. `gfx_blit_canvas` is a plain copy (`gfx.h`), which is why
`wallpaper_draw_into` exists at all — `acid_repaint_region` fakes transparency for
desktop.rb's closed dropdown by painting real wallpaper pixels into the desktop's own
canvas, because there is no alpha compositing to do it properly.

So "across the screen" needs something that does not exist yet. The whole design is
about adding the smallest honest version of it.

## Rejected approaches

**Animate inside the terminal window.** Pure Ruby, one file, no kernel changes — and
the sprite is trapped in a 260x160 box. Rejected: it isn't what was asked for.

**A full-screen window sent to the back, faking transparency with
`acid_repaint_region`.** No compositor change needed. Rejected on two verified facts:
the desktop's own window is 640x204, not 640x24 (`sim/sim_main.c:34` — it is registered
tall so its Menu dropdown has somewhere to draw), and it is opaque like every other
window, so anything behind it is invisible across the top 204 of 360 rows. Second,
`acid_spawn_app` takes only `(path, w, h, arg)` and cascades x/y itself
(`core/bindings/window_binding.c:392`), so a full-screen window at 0,0 needs a C change
regardless — the "no C work" option was never actually on the table.

**A transparent overlay *window*, spawned as its own app.** This was the first design,
and it worked, but it billed an easter egg like an application. Every spawn is
`xTaskCreate(vm_host_task, ..., 8192, ...)` (`core/kernel/kernel_spawn.c:75`) plus a
fresh `mrb_open()` and six Ruby files compiled from source at runtime — `acid_keys.rb`,
`acid_palette.rb`, `acid_waveform.rb`, `acid_app.rb`, `acid_game.rb`, then the app
(`core/vm_host/vm_host.c:260-276`). One FreeRTOS task, one isolated mruby VM and six
parses, to fly a teapot for three seconds. It also dragged an `overlay` flag through
`kernel_window`, `kernel_spawn_app` and both boot call sites, plus click-through in
`kernel_window_find_at` and an exclusion in `acid_window_info` so no taskbar button
appeared for it.

Dropping the window drops all of that. What follows costs *less* C than the version it
replaces.

## The design

### 1. A kernel-owned overlay canvas (C)

One canvas, owned by the kernel, composited last with colour-key transparency. It is not
a window: nothing hit-tests it, nothing lists it in the taskbar, nothing can drag or
focus it, and no task or VM exists for it.

`core/kernel/kernel_theme.h` gains the key colour:

```c
/* Every pixel an overlay leaves this colour shows whatever is underneath.
 * Magenta because nothing in the theme palette or the wallpaper's own neon
 * palette (wallpaper_data.h) quantises onto it in RGB565. */
#define ACID_OVERLAY_KEY 0xFF00FF
```

New `core/kernel/kernel_overlay.{h,c}`:

```c
int    kernel_overlay_open( void * owner_task );   /* allocate-on-first-use, clear, mark dirty */
void   kernel_overlay_close( void * owner_task );  /* no-op unless owner -- see lifetime note */
void   kernel_overlay_release_owner( void * owner_task );  /* vm_host cleanup */
int    kernel_overlay_is_open( void );
void * kernel_overlay_canvas( void );              /* the compositor's read; NULL when closed */
void * kernel_overlay_canvas_for( void * owner_task ); /* a drawer's read; NULL unless owner */
```

**One owner at a time, enforced here rather than in Ruby.** There is exactly one overlay
canvas, so exactly one thing may animate on it: a second egg starting mid-flight would
clear the first one's frames and the two would fight, which is the "not doubled up, not
multiple sprites" requirement. A guard in `AcidEggs` alone cannot deliver that — the
terminal is `multi = true`, so two terminal windows are two separate mruby VMs with two
separate copies of the module's state, each believing it is the only one. `open` claims
the overlay for a task handle and refuses any other task until it is released; `close`
and `canvas_for` ignore a caller that is not the owner.

`kernel_overlay_release_owner` exists because a claim must not outlive the task holding
it — an app that crashes mid-egg would otherwise wedge the overlay closed to everyone
forever. It is called from `vm_host_task`'s unconditional per-app cleanup, directly
beside the `kernel_audio_release_owner` call already there, which is this codebase's
established pattern for exactly this problem (`kernel_audio.h:60`).

**Lifetime, deliberately.** The canvas is allocated lazily on the first `open` and then
kept for the life of the OS; `close` only clears the open flag. Freeing it would mean
one task (whichever app ran the egg) destroying memory that the router task may be
mid-blit on — exactly the use-after-free `kernel_window_unregister`'s own comment
documents having already been hit once with window canvases, where the fix was to move
the free onto the owning task after its VM had stopped. There is no equivalent "owning
task has stopped" moment here, so the design does not create the hazard in the first
place. The cost is 640x360x2 = ~450KB retained after the first egg — and only after the
first egg, so a user who never types `dave` pays nothing. On the sim that comes from
LovyanGFX's own allocator (`hal_display_create_canvas` → `new LGFX_Sprite`), not the
4MB FreeRTOS heap in `sim/FreeRTOSConfig.h`; on hw the display layer is a stub
(`hw/main/hal_display_hw.c:47`).

An app drawing into the canvas while the router blits it is the same race every window
canvas already has, and is accepted for the same reason: the blit is a plain memory
copy, so the worst case is one frame showing a sprite mid-move.

**Keyed blit**, unchanged from the earlier design:

- `core/hal/hal_display.h`: declare `hal_display_blit_canvas_keyed(target, canvas, x, y, key)`.
- `sim/hal_display_sim.cpp`: `as_canvas(canvas)->pushSprite(as_canvas(target), x, y, key)`.
  LovyanGFX already supports this — `LGFX_Sprite.hpp:336` takes a transparent colour and
  runs it through the same `_write_conv.convert` path that every existing `fillRect`
  colour in this project already goes through, so a key written as `0xFF00FF` matches
  pixels filled as `0xFF00FF`.
- `hw/main/hal_display_hw.c`: matching `ESP_LOGI` stub, like its neighbours.
- `core/gfx/gfx.{h,c}`: `gfx_blit_canvas_keyed`, wrapping the HAL call exactly as
  `gfx_blit_canvas` does.

**Compositor**, one branch at the end of `kernel_router_composite_frame`, after the
window loop and before `gfx_present`:

```c
if( kernel_overlay_is_open() )
{
    gfx_blit_canvas_keyed( kernel_overlay_canvas(), 0, 0, ACID_OVERLAY_KEY );
}
```

**Bindings**, added to `core/bindings/gfx_binding.c` beside the existing drawing calls:

| Binding | Behaviour |
|---|---|
| `acid_overlay_open` | claims the overlay for the calling task; false if another task holds it or the canvas could not be allocated |
| `acid_overlay_clear` | fills the whole canvas with the key colour (one frame's erase); a no-op for a non-owner |
| `acid_overlay_fill_rect(x, y, w, h, color)` | screen-absolute; a no-op when closed or for a non-owner |
| `acid_overlay_close` | releases it; the next composite tick drops it |

Each passes `xTaskGetCurrentTaskHandle()` as the owner, the same self-identification
pattern `acid_send_self_to_back` and the audio bindings already use.

`acid_overlay_fill_rect` is the only drawing primitive the sprites need, so that is the
entire surface. `gfx_fill_rect` already calls `gfx_mark_dirty` for any non-NULL target,
so frames recomposite on their own; `open` and `close` mark dirty explicitly, since
neither draws anything itself.

### 2. `AcidSprite` — sprites as pictures (`v2/apps/lib/acid_sprite.rb`)

A sprite is a list of strings, one character per pixel, and a palette — a sprite sheet
you can read and edit in place:

```ruby
DAVE = [ "...KKK....",
         "..KSSSK...",
         "..SSSS.RR.",
         ".BBBBBBRR.", ... ]
PALETTE = { "K" => 0x101010, "S" => 0xE8B48A, "B" => 0x2050E0, "R" => 0xE01020 }
```

- `AcidSprite.draw(rows, x, y, scale, palette, flip = false)` — one
  `acid_overlay_fill_rect` per pixel, `scale` px square. `"."` is skipped, so sprites are
  transparent without any per-sprite key handling. `flip` mirrors horizontally, so Dave
  and the teapot face the direction they are flying.
- `AcidSprite.width(rows)` / `AcidSprite.height(rows)` — for bounds and centring.

Sprites are ~16x12 pixels at scale 3, so ~48x36 on screen. No sprite colour may quantise
onto `ACID_OVERLAY_KEY` in RGB565; the palettes above respect that.

### 3. `AcidEggs` — the three animations (`v2/apps/lib/acid_eggs.rb`)

A module, not an app. It holds the sprite data, the block font, and a frame stepper:

```ruby
AcidEggs.start(name)   # "dave" | "joe" | "maximbady"; opens the overlay
AcidEggs.active?
AcidEggs.step          # advance one frame if TICK_MS has elapsed, else no-op
AcidEggs.abort         # clear and close the overlay
```

`step` is time-guarded internally so callers can call it as often as they like. Each
frame is `acid_overlay_clear` then one `AcidSprite.draw` — a full 640x360 clear per frame
is a single fill and far simpler than tracking dirty rectangles for one sprite.

**dave** — pixel superman: blue body, red cape streaming behind him, black hair, skin
head, a yellow chest pixel. Random height in y 30..300, random left-to-right or
right-to-left, 7px per frame, with a slight sine bob. Ends when he leaves the screen
(~95 frames, ~3s at 33ms).

**joe** — pixel teapot: grey-white body, spout, handle, lid knob. Same flight logic, a
gentle wobble instead of a bob.

**maximbady** — blue trousers, red top, green head and green arms, exactly as specified.
Bounces off all four screen edges, 5px per frame diagonally, for 6 bounces. Then the
figure vanishes and **SOOOOOOOOO** appears centred for 60 frames (~2s), then the overlay
closes. The word is drawn through `AcidSprite` from a 5x7 block font containing only the
two glyphs it needs, `S` and `O`, at scale 6: ten glyphs of 30x42 with 6px gaps is 354px
wide, centred at x=143, y=159. The built-in 6px font would render the whole word 60px
wide, far too small for a screen-centre gag.

**Sound** — voice 6. Voices 0-4 belong to the games (`acid_blaster.rb`, `breakout.rb`)
and 5 to the piano (`piano.rb:13`), of `SYNTH_NUM_VOICES` 8, so 6 and 7 are free. Dave
gets a rising whoosh via `acid_trigger_arp`, Joe a short blip, maximbady a boing per
bounce and a descending slide under the text.

### 4. Driving the animation without a task (`v2/apps/lib/acid_app.rb`)

`AcidApp#start` hardcodes `acid_poll_event(200)`. It gains one overridable method:

```ruby
def poll_timeout_ms
  200
end
```

used in place of the literal. The terminal returns 33 while an egg is in flight and 200
otherwise. `on_idle` fires on each timeout and steps a frame.

One wrinkle: `on_idle` only runs when `acid_poll_event` times out, so a burst of
keystrokes would otherwise stall the animation. The terminal therefore also calls
`AcidEggs.step` at the end of `on_key`; the time guard inside `step` makes the extra
calls free. The animation stays smooth whether or not you are typing, which is the point
of not putting it in its own task in the first place.

### 5. Terminal triggers (`v2/apps/terminal.rb`)

`terminal.app.toml` gains `libs = lib/acid_sprite.rb, lib/acid_eggs.rb` — the mechanism
the Editor already uses for its five modules.

In `run_command`, ahead of the ordinary dispatch:

```ruby
EGGS = [ "dave", "joe", "maximbady" ]
# ...
if args.empty? && EGGS.include?(cmd.downcase)
  AcidEggs.start(cmd.downcase)
  return
end
```

Case-insensitive, bare word only, no arguments. Nothing is printed, `cmd_help` is not
touched, and a failed `acid_overlay_open` stays just as silent. The line still echoes as
`$ dave` because `submit` echoes every line — that is the shell behaving normally, not
the egg answering.

`on_destroy` calls `AcidEggs.abort`, so closing the terminal mid-flight takes the overlay
with it rather than leaving a sprite frozen on screen.

## Testing

**Unit.** `core/kernel/` gains `test_kernel_overlay.c` in the style of the existing
standalone-`main` tests: closed by default; `open` reports open and returns a non-NULL
canvas; a second `open` by the same owner does not reallocate; `close` reports closed
while the canvas pointer stays valid (the documented no-free lifetime); reopen after
close works. Ownership gets its own cases: a second task's `open` is refused while the
first holds it, a non-owner's `close` and `canvas_for` do nothing, the overlay becomes
claimable again after the owner closes, and `release_owner` frees a claim for the owning
task only.

**Manual, on the sim,** each of the three eggs:

1. over bare wallpaper, confirming the key colour is invisible and the sprite is not;
2. over an open window and over the taskbar, confirming the sprite draws *on top* of
   both and that neither is damaged after the egg ends;
3. clicking where the sprite is, mid-flight, confirming the click lands on whatever is
   underneath — the overlay is not a window and cannot take it;
4. typing continuously during the flight, confirming characters appear and the animation
   does not stall;
5. closing the terminal mid-flight, confirming the overlay goes with it;
6. for `maximbady` specifically, six bounces and a centred, legible **SOOOOOOOOO**;
7. typing a second egg while one is in flight, and typing an egg in a *second terminal
   window* while the first terminal's egg is flying — in both cases exactly one sprite
   is ever on screen, and the second request is silently ignored rather than fighting
   the first for the canvas.

**hw.** Source changes compile-checked only, as in every previous phase.

## Risks

- **~450KB retained** after the first egg, by design, to avoid a cross-task free. Only
  charged to users who trigger one.
- **RGB565 quantisation** — no sprite or font colour may land on `ACID_OVERLAY_KEY`.
  Respected by the palettes here; a constraint for any future overlay user.
- **The eggs live in the terminal's VM**, so ~150 extra lines are parsed at every
  terminal launch. Measurable in principle, small in practice, and the price of not
  spawning a task and a VM per teapot.
