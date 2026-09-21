# The acid OS v2 Manual

acid OS v2 is a small graphical operating system for the
[M5Stack Tab5](https://docs.m5stack.com/en/core/M5Tab5) (ESP32-P4), built on
FreeRTOS. Every application is a **Ruby** script running in its own mruby VM,
on its own FreeRTOS task, in its own window.

This manual is about writing those applications.

```ruby
class HelloApp < AcidApp
  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    acid_fill_rect(10, 30, 100, 40, AcidPalette.hue(90))
    acid_draw_text("HELLO ACID", 14, 45, 0x050607, AcidPalette.hue(90))
    acid_draw_window_border
  end

  def on_touch(x, y, pressed)
    # A note sounds until you stop it — there is no duration argument.
    pressed ? acid_play_note(5, 40, 60) : acid_stop_note(5)
  end
end

HelloApp.new.start
```

That is a complete, installable app. Drop it in `v2/apps/hello.rb` with a
five-line manifest beside it and it appears in the Menu at the next boot — no
rebuild, no C, no toolchain.

## Contents

| | |
|---|---|
| **[1. Getting started](01-getting-started.md)** | Build the simulator, run it, write and install your first app. |
| **[2. Apps and manifests](02-apps-and-manifests.md)** | `.app.toml`, how the launcher finds apps, and the `.cart` format for apps written outside the OS. |
| **[3. The app lifecycle](03-app-lifecycle.md)** | `AcidApp`, the event loop, touch, keys, idle, redraw, focus. |
| **[4. Graphics](04-graphics.md)** | Drawing inside your window, the theme palette, the 256-colour hue wheel, the full-screen overlay and sprites. |
| **[5. Sound](05-sound.md)** | The 8-voice synthesiser: notes, envelopes, waveforms, the filter, ring modulation and the arpeggiator. |
| **[6. Games](06-games.md)** | `AcidGame`, fixed-tick loops, and the sound-effect lifecycle. |
| **[7. System APIs](07-system-apis.md)** | Windows, launching other apps, task and memory stats, network, master volume. |
| **[8. Cookbook](08-cookbook.md)** | Recipes, conventions and the mistakes that bite. |
| **[9. API reference](09-api-reference.md)** | Every `acid_*` function and every library module, alphabetically. |

## What kind of Ruby is this?

[mruby](https://mruby.org) — the embeddable Ruby. Classes, modules, blocks,
`Array`, `Hash`, `String`, `Symbol`, `Struct` and exceptions all work. What is
**not** there:

- **No `require` / `require_relative`.** The kernel loads your files for you;
  see [Loading modules](02-apps-and-manifests.md#23-loading-modules).
- **No gems, and no Ruby stdlib beyond mruby's own core.** What you *do* get
  includes `File`, `Dir`, `Time`, `Math`, `Struct`, `rand` and `sprintf`. The
  OS's own code sticks to integer arithmetic anyway — it has to run on a
  microcontroller, and the kernel below it contains no floating point at all.
- **No threads in Ruby.** Concurrency is FreeRTOS tasks, one per app, handed to
  you by the kernel.
- **Every OS call is a global function** named `acid_*`, defined on `Kernel`, so
  you can call it from anywhere without a receiver.

## Where things live

```
v2/
  apps/            your apps: <name>.rb + <name>.app.toml
    lib/           shared Ruby modules (AcidApp, AcidPalette, ...)
  carts/           example .cart programs
  core/
    audio/         the synthesiser (C)
    bindings/      the acid_* functions (C)
    kernel/        windowing, compositor, audio command queue (C)
  sim/             native Linux build — what you develop against
  hw/              the ESP-IDF build for real hardware
```

You only ever need `v2/apps/` to write an app.
