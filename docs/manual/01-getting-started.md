# 1. Getting started

[← Contents](README.md) · [Next: Apps and manifests →](02-apps-and-manifests.md)

You develop against the **simulator** — a native Linux build of the whole OS
that runs in an SDL2 window. It is the same kernel, the same compositor, the
same synthesiser and the same mruby VMs as the hardware build; only the display
and input backends differ. Nothing in this manual requires a Tab5 on your desk.

## 1.1 Build the simulator

### Step zero: submodules

mruby carries a *nested* submodule (`mrbgems/mruby-compiler/lib/prism`). A
non-recursive `git submodule update --init` leaves it empty and the build fails
in a confusing place, because mruby's own bytecode compiler needs it.

```sh
git submodule update --init --recursive
```

Do this once, from the repository root. If you have already initialised
submodules non-recursively, run it again with `--recursive`; it is idempotent.

### Dependencies

The simulator needs a C/C++ toolchain, CMake and SDL2 development headers.

```sh
# Debian / Ubuntu / Parrot / Kali
sudo apt install build-essential cmake libsdl2-dev
```

### Build and run

```sh
cmake -S v2/sim -B v2/sim/build
cmake --build v2/sim/build
./v2/sim/build/acidos_sim
```

A 640×360 window opens onto the desktop. **Menu** is at the top left.

> **Run it from the repository root.** The kernel loads app scripts by relative
> path (`v2/apps/<name>.rb`), so the working directory has to be the repo root
> or nothing launches.

### Driving the simulator

| Input | Effect |
|---|---|
| Left mouse button | Touch — press, drag, release |
| Dragging a title bar | Moves a window |
| The green dot at a title bar's right | Closes that window |
| Keyboard | Delivered to the focused window as key events |
| Closing the SDL window | Shuts the OS down |

## 1.2 The hardware build

The ESP-IDF project lives in `v2/hw/` and targets `esp32p4`. It needs
ESP-IDF ≥ 5.3 — the first release with ESP32-P4 support — installed and sourced.

```sh
cd v2/hw
idf.py set-target esp32p4
idf.py build
```

Hardware bring-up is not finished: the HAL layer for the real panel, touch
controller and I²S audio path is still being written. **Write and test your apps
on the simulator.** Because the mruby side of the OS is identical on both
targets, an app that works on the simulator is not expected to need changes.

## 1.3 Your first app

An app is **two files** in `v2/apps/`, sharing a base name.

### The script

`v2/apps/counter.rb`:

```ruby
class CounterApp < AcidApp
  WINDOW_W = 180
  WINDOW_H = 120
  TITLE_BAR_H = 16

  BG = 0x050607
  TEXT = 0xD4E6DB
  ACCENT = 0x00FF66

  def on_create
    @count = 0
  end

  def on_touch(x, y, pressed)
    return unless pressed
    @count += 1
    redraw
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    acid_fill_rect(20, 40, WINDOW_W - 40, 30, ACCENT)
    acid_draw_text("TAPS: #{@count}", 34, 52, BG, ACCENT)
    acid_draw_text("tap anywhere", 40, 88, TEXT, BG)
    acid_draw_window_border
  end
end

CounterApp.new.start
```

### The manifest

`v2/apps/counter.app.toml`:

```toml
name = Counter
w = 180
h = 120
desc = Counts taps
```

### Run it

Restart the simulator. **Menu → Counter.** The launcher rescans `v2/apps/` for
`*.app.toml` files at every boot, so a new app needs a restart but never a
rebuild.

## 1.4 What just happened

1. At boot, `desktop.rb` scanned `v2/apps/` for `*.app.toml` manifests and
   registered each one it could parse as launchable.
2. Picking **Counter** made the kernel spawn a **new FreeRTOS task** with its
   **own mruby VM** and its own 180×120 drawing canvas.
3. That VM loaded the framework libraries (`acid_keys.rb`, `acid_palette.rb`,
   `acid_waveform.rb`, `acid_app.rb`, `acid_game.rb`), then any modules your
   manifest asked for, then `counter.rb`.
4. The last line, `CounterApp.new.start`, entered `AcidApp`'s event loop, which
   blocks on the window's event queue and dispatches to your callbacks.
5. Your drawing went to a private canvas. The compositor blits every window's
   canvas to the screen in z-order.

Because each app is its own VM and its own task, a Ruby exception in your app
takes down your app and nothing else. The stack trace goes to the terminal you
launched the simulator from — **keep that terminal visible while developing**;
it is your only error console.

## 1.5 The development loop

Each spawn loads your script **from disk**, so:

- **Changing an existing app's Ruby** — just close its window and launch it
  again from Menu. No restart, no rebuild.
- **Adding a new app, or changing a `.app.toml`** — restart the simulator. The
  launcher registry is built once, at boot, by `desktop.rb`'s manifest scan.
- **Changing C** — rebuild:
  `cmake --build v2/sim/build && ./v2/sim/build/acidos_sim`

There is also an **Editor** app inside the OS (Menu → Editor) that opens app
source from the running system, and a **File Manager** that launches an app by
clicking its `.app.toml`. Editing an app from inside the OS it runs in is a
perfectly good way to work.

---

[← Contents](README.md) · [Next: Apps and manifests →](02-apps-and-manifests.md)
