# 7. System APIs

[← Games](06-games.md) · [Contents](README.md) · [Next: Cookbook →](08-cookbook.md)

Beyond drawing and sound, an app can inspect and manipulate the running system:
the window list, the launcher registry, task and memory statistics, the network
and the master volume. These are what the Config, System Monitor, Network, File
Manager, Terminal and desktop apps are built out of, and they are available to
any app.

## 7.1 Launching other apps

### By registry index

The launcher registry is built at boot from `v2/apps/*.app.toml` (see
[§2.4](02-apps-and-manifests.md#24-how-the-launcher-finds-your-app)). You can
walk it:

```ruby
count = acid_launcher_count           # => number of registered apps
name  = acid_launcher_name(index)     # => "System Monitor", or nil
path  = acid_launcher_path(index)     # => "v2/apps/sysmon.rb", or nil
ok    = acid_launcher_spawn(index)    # => true/false
```

`acid_launcher_spawn` uses the width, height, `multi` flag and `libs` recorded
in the manifest, so the app opens exactly as it would from the Menu. It returns
`true` on success — including when the app was a singleton and already open, in
which case the existing window is raised and focused instead.

This is how the Terminal's `run` command works:

```ruby
def cmd_run(args)
  query = args[0].downcase
  count = acid_launcher_count
  i = 0
  while i < count
    if acid_launcher_name(i).downcase == query
      acid_launcher_spawn(i)
      return
    end
    i += 1
  end
  @lines << "run: no app named #{args[0]}"
end
```

The registry includes apps hidden from the Menu with `menu = false`, so a
launcher of your own can reach every app on the system, not just the visible
ones.

### By path

```ruby
acid_spawn_app(path, w, h, arg)    # => true/false
```

Launch by script path instead of registry index. The `multi` flag and `libs`
still come from the registry (looked up by exact path), so an app launched this
way gets its modules exactly as the Menu would give them. A path with no
registry entry defaults to singleton and no modules.

`arg` is an optional startup string; pass `""` for none.

```ruby
# Open the Editor on a specific file
acid_spawn_app("v2/apps/editor.rb", 420, 280, "v2/apps/mygame.rb")
```

> **Canonicalise paths that came from the filesystem.** `v2/fsroot/App` is a
> symlink to `v2/apps`, and the registry matches by exact string — a path that
> arrived through the symlink matches nothing and spawns a VM with none of its
> modules. Use the helper on `AcidApp`:
>
> ```ruby
> acid_spawn_app(canonical_app_path(path), w, h, "")
> ```

### Reading your own launch argument

```ruby
def on_create
  target = acid_launch_arg          # "" if launched without one
  load_file(target) unless target.empty?
end
```

Safe to read more than once; it is plain context state, not consumed.

### Singletons

By default an app is a **singleton**: launching one that is already open raises
and focuses the existing window instead of spawning a second. `multi = true` in
the manifest opts out. Editor, File Manager and Terminal are the multi-window
apps in the tree.

Remember that two windows of a `multi` app are **two separate mruby VMs**. They
share no module state, no globals and no variables, and neither can see the
other. Anything that must be true system-wide — "only one of these animations at
a time" — has to be enforced by the kernel, not by your Ruby. See
[§4.5](04-graphics.md#45-the-overlay).

## 7.2 The window list

```ruby
acid_window_max                  # => capacity of the window table
acid_window_info(index)          # => [name, x, y, w, h, focused] or nil
acid_activate_window(index)      # raise and focus that window
acid_close_window(index)         # => true/false
```

`acid_window_info` returns `nil` for an empty slot, so walk the whole range and
skip the gaps:

```ruby
def active_windows
  out = []
  i = 0
  max = acid_window_max
  while i < max
    info = acid_window_info(i)
    out << [i, info] if info
    i += 1
  end
  out
end
```

The tuple is `[name, x, y, w, h, focused]` — the name as registered by the
manifest, screen coordinates, size, and whether it currently holds focus.

`acid_close_window` **refuses to close the calling app's own window** and
returns `false`. A monitor ending itself from its own window list is a confusing
way to quit; the title-bar close button is the normal route.

`acid_send_self_to_back` drops your own window to the back of the z-order. It
always targets the caller — there is no way to send someone else's window back.
The desktop uses it when closing its dropdown, having temporarily raised itself
to show it.

## 7.3 Focus

```ruby
acid_am_i_focused        # => true/false
```

`AcidApp#focused?` wraps this. In this OS, focus and being the topmost window
always change together, so it doubles as "am I the window actually visible on
top". See [§6.3](06-games.md#63-focus-and-the-z-order-trap) for why a game must
check it.

## 7.4 Tasks and memory

```ruby
acid_refresh_tasks       # sample the task table; => number of tasks
acid_task_count          # => number of tasks in the last sample
acid_task_info(index)    # => [name, state, cpu_percent] or nil
acid_mem_used_kb         # => kilobytes in use
```

`acid_refresh_tasks` takes a snapshot; `acid_task_info` reads from it. Call
refresh first, then walk:

```ruby
def sample
  count = acid_refresh_tasks
  rows = []
  i = 0
  while i < count
    info = acid_task_info(i)
    rows << info if info
    i += 1
  end
  rows
end
```

`state` is a string — `"running"`, `"ready"`, `"blocked"`, `"suspended"`, `"deleted"`, or `"?"` — and
`cpu_percent` is an integer share of CPU time.

## 7.5 Compositor and audio statistics

```ruby
acid_composited_frames    # frames the compositor has painted
acid_skipped_frames       # frames it skipped because nothing was dirty
acid_active_voice_count   # voices with a sounding envelope, 0-8
```

Exposed for a system monitor to show what is actually distinctive about this
build — its own compositor, its own synthesiser. They are also the fastest way
to answer "is my app repainting far more than it needs to?" (watch
`composited_frames` climb while nothing moves) and "did I leave a note gated on?"
(watch `active_voice_count` fail to return to zero).

## 7.6 Network

```ruby
hostname, ip, connected = acid_network_info
```

Returns `[hostname, ip, connected]`.

`connected` means **an address was found**, not that anything is reachable.
There is no live reachability check, no sockets, no HTTP client. This is
information for a status display, not a networking API.

## 7.7 Master volume and the wallpaper

```ruby
acid_set_volume(percent)           # 0-100
acid_get_volume                    # => 0-100
acid_set_wallpaper_enabled(bool)
acid_get_wallpaper_enabled         # => true/false
```

Both are **system-wide settings that the Config app owns**. Read them freely;
think twice before writing them. An app that turns the user's volume down
because it is loud, or switches their wallpaper off because it wants a plain
background, is misbehaving — scale your own note volumes and draw your own
background instead.

## 7.8 The filesystem

There is no `acid_*` filesystem API. Apps use **ordinary mruby `File` and `Dir`**
against the host filesystem:

```ruby
Dir.entries(path)                 # => array of names, including "." and ".."
File.exist?(path)  File.directory?(path)  File.size(path)  File.symlink?(path)
File.basename(path)  File.dirname(path)  File.join(a, b)  File.rename(a, b)

f = File.open(path, "r")
text = f.read
f.close

f = File.open(path, "w")
f.write(text)
f.close
```

The apps in the tree mostly use the streaming form instead, which avoids
building a whole array for a large directory:

```ruby
d = Dir.open(path)
names = []
while (ent = d.read)
  names << ent unless ent == "." || ent == ".."
end
d.close
names.sort!
```

Always close what you open, and always rescue — a missing file raises:

```ruby
def read_file(path)
  f = File.open(path, "r")
  text = f.read
  f.close
  text
rescue => e
  @lines << "cat: #{e.message}"
  nil
end
```

### The layout

`v2/fsroot/` is the OS's own filesystem root, browsable from File Manager:

| Path | Holds |
|---|---|
| `v2/fsroot/Home` | The user's files |
| `v2/fsroot/App` | Symlink to `v2/apps` |
| `v2/fsroot/Help` | Help text |
| `v2/fsroot/Etc` | Configuration |
| `v2/fsroot/Data`, `Cache`, `Tmp`, `var` | As the names suggest |
| `v2/fsroot/Bin`, `Lib`, `Usr`, `Boot` | System directories |

On the simulator these are real directories relative to the repository root, so
paths are relative and the process's working directory has to be the repo root.

There is **no sandbox**. An app can read and write anything the simulator process
can. That is a development convenience, not a security model — see
[§2.5](02-apps-and-manifests.md#carts-are-not-a-sandbox).

---

[← Games](06-games.md) · [Contents](README.md) · [Next: Cookbook →](08-cookbook.md)
