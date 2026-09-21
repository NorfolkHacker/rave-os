# 2. Apps and manifests

[← Getting started](01-getting-started.md) · [Contents](README.md) · [Next: The app lifecycle →](03-app-lifecycle.md)

## 2.1 The two files

Every app in `v2/apps/` is a pair sharing a base name:

```
v2/apps/tetris.rb          the Ruby source
v2/apps/tetris.app.toml    the manifest
```

The manifest is what makes the app *exist* as far as the OS is concerned. A
`.rb` with no manifest beside it is never registered and never launchable. A
manifest with no matching `.rb` registers a path that fails to load when picked.

## 2.2 The manifest format

Despite the extension, this is **not** real TOML. `desktop.rb` splits each line
on the first `=`, strips whitespace from both sides, and keeps the rest as a
raw string. There are no quotes, no arrays, no tables, no comments.

```toml
name = System Monitor
w = 200
h = 160
desc = Live kernel/task/audio stats
menu = true
multi = false
libs = lib/acid_sprite.rb, lib/acid_eggs.rb
```

| Key | Required | Meaning |
|---|---|---|
| `name` | **yes** | Display name in the Menu, the taskbar and window lists. Keep it short — the Menu truncates at 22 characters and window titles at 16. |
| `w` | **yes** | Window width in pixels, including the 1px border. |
| `h` | **yes** | Window height in pixels, including the 16px title bar. |
| `desc` | no | One line shown alongside the app. Has no effect on behaviour. |
| `menu` | no | `menu = false` hides the app from the Menu dropdown. It stays launchable by path (File Manager, `acid_spawn_app`). Anything other than the exact string `false` — including omitting the key — means visible. |
| `multi` | no | `multi = true` allows several windows of this app at once. Default is **singleton**: launching an already-open app raises and focuses the existing window instead of spawning a second. |
| `libs` | no | Comma-separated module paths to load into this app's VM before its own script. Relative to `v2/apps/`. |
| `source` | no | `source = cart` marks an app installed by Load Cart. Only Load Cart writes this; see [§2.5](#25-carts). |

If `name`, `w` or `h` is missing, the manifest is **silently skipped** and the
app never appears. A manifest that fails to parse does not stop the scan — the
other apps still register — so a missing app usually means a typo in its own
manifest, not a broken system.

### Sizing a window

The screen is **640×360**. `w`/`h` are the whole window:

```
+--------------------------------------+  <- y = 0, 1px THEME_HARD border
| Title                             ●  |  <- title bar, 16px tall
+--------------------------------------+  <- y = 16, your area starts here
|                                      |
|          your drawing area           |     w - 2 usable width
|                                      |     h - 17 usable height
+--------------------------------------+
```

Usable content therefore runs from `(1, 16)` to `(w - 2, h - 2)`. In practice
apps draw from `(0, 16)` to `(w, h)` and let the clipping and the border
overdraw sort out the edges — see [§4.2](04-graphics.md#42-coordinates-and-clipping).

New windows cascade from the top left as more open, and the kernel clamps the
cascade so a window always lands fully on screen. A window bigger than the
screen pins to the top-left of the usable area.

### Keeping the constants in sync

The kernel gets `w`/`h` from the manifest; your Ruby needs them too, for layout.
Nothing links the two, so every app in the tree declares them again and keeps
them matched by convention:

```ruby
class CounterApp < AcidApp
  WINDOW_W = 180   # must match `w =` in counter.app.toml
  WINDOW_H = 120   # must match `h =` in counter.app.toml
```

Get them out of step and your app draws to the wrong size; the binding clips
whatever falls outside the real window, so the symptom is content mysteriously
cut off or a stripe of unpainted background, not a crash.

## 2.3 Loading modules

mruby here has **no `require`**. The VM host loads files for you, in this exact
order, before your script runs:

1. `v2/apps/lib/acid_keys.rb` — `AcidKeys` key codes
2. `v2/apps/lib/acid_palette.rb` — `AcidPalette.hue`
3. `v2/apps/lib/acid_waveform.rb` — `AcidWaveform` constants
4. `v2/apps/lib/acid_app.rb` — `AcidApp`
5. `v2/apps/lib/acid_game.rb` — `AcidGame`
6. everything in your manifest's `libs`, left to right
7. your own `<name>.rb`

**Those first five are always available.** You never list them in `libs`, and
listing one anyway is harmless but redundant. `libs` is for *extra* modules:

```toml
libs = lib/acid_sprite.rb, lib/acid_eggs.rb
```

Everything is loaded into one flat namespace — there are no file-scoped
constants. A module you write for your own app goes in `v2/apps/lib/` (or a
subdirectory of `v2/apps/`, as Editor does with `editor/buffer.rb`) and gets
named in `libs`.

### What `libs` will not accept

Each entry is validated before loading. Rejected entries are skipped with a
message on stderr and the app still starts:

- must end in `.rb`
- must be relative — a leading `/` is rejected
- no `\` anywhere
- no empty path components (`a//b.rb`, or a trailing `/`)
- no `..` as a whole component (`a/../../x.rb` is rejected; a file genuinely
  named `weird..name.rb` is fine)

## 2.4 How the launcher finds your app

At boot, `desktop.rb`:

1. lists `v2/apps/`, keeps every entry ending in `.app.toml`, and **sorts them**;
2. parses each one, and for each valid manifest calls
   `acid_launcher_register(rb_path, name, w, h, multi, libs)`;
3. records whether `menu != "false"`, which is what the dropdown filters on.

The registry is a fixed-size C table. Registration is refused once it is full,
and it refuses the manifests that sort *last* — so if apps start disappearing
from the Menu, check the count before assuming your manifest is wrong. There is
generous headroom over the number of apps that ship.

`v2/fsroot/App` is a symlink to `v2/apps`. The registry matches launch paths by
exact string, so a path that arrives through the symlink matches nothing and
spawns a VM with none of its modules. `AcidApp#canonical_app_path` converts one
form to the other; use it whenever you take a path from the filesystem and pass
it to `acid_spawn_app`.

## 2.5 Carts

A **cart** is a whole app in one file, written outside the OS and carried in.
It is ordinary app Ruby with a header comment block on top:

```ruby
# name: Hello Acid
# w: 200
# h: 150
# desc: Example cart -- colour-cycling bars
# libs: lib/acid_palette.rb

class HelloAcidApp < AcidApp
  # ... ordinary app code ...
end

HelloAcidApp.new.start
```

The **Load Cart** app (Menu → Load Cart) browses `v2/carts/`, `~/carts`, and the
host's `/media`, `/mnt` and `/run/media` mount points — so a cart on a USB stick
or an SD card appears in the same list. On hardware, the memory card's mount
point joins that list. Installing writes `v2/apps/<slug>.rb` plus a generated
`v2/apps/<slug>.app.toml` carrying `source = cart`.

### The header

All five keys are optional. Only the **first comment block** in the file is
scanned — a `# name:` further down, inside a comment among real code, is
ignored.

| Key | Default if absent |
|---|---|
| `name` | derived from the filename (`acid_snake.cart` → "Acid Snake") |
| `w` | 220, clamped to the screen |
| `h` | 160, clamped to the screen |
| `desc` | empty |
| `libs` | none. Entries naming modules that do not exist in `v2/apps/lib` are dropped. |

The slug comes from the filename and can never contain a path separator or a
traversal component.

### What a cart may and may not overwrite

A cart can replace an app that was *itself* installed from a cart — that is what
`source = cart` in the generated manifest marks. It can never overwrite a
hand-written app in `v2/apps/`.

An installed cart joins the Menu at the **next boot**; Load Cart's own **RUN**
button starts it immediately in the meantime.

### Carts are not a sandbox

Once installed, a cart's code is exactly as privileged as any other app in
`v2/apps/` — same bindings, same filesystem reach, same everything. The header
validation stops a *malformed* cart, not a hostile one. Read a cart before you
install it.

An installed cart also shows up as a new untracked file in `git status`. Commit
it or delete it like any other file.

---

[← Getting started](01-getting-started.md) · [Contents](README.md) · [Next: The app lifecycle →](03-app-lifecycle.md)
