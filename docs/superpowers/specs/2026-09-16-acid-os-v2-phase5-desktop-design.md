# acid OS v2: desktop shell, keyboard, file manager, editor (roadmap phase 5) — design

**Goal:** give acid OS v2 real keyboard input, a desktop shell that actually shows and
lets you switch between running apps (not the current 20px placeholder strip), a file
manager, and a text editor — all working, unmodified, on `v2/sim` (the only target that
can be actually run; `v2/hw` gets the same source changes, statically verified only, per
every prior phase's documented ESP-IDF gap).

## Reference model

Researched directly from family-mruby-os's real source, re-cloned for this phase to
`/tmp/claude-1000/.../scratchpad/phase5-ref/fmruby-core` (the prior clone from the audio
phase no longer existed on disk — session scratchpads don't persist across sessions;
re-cloning and re-reading real source, not reusing memory of it, is this project's own
established discipline). Read directly:

- `main/prebuild_scripts/kernel/system_desktop.app.rb` — the real desktop shell: two
  canvas layers (menu bar/dropdown/launcher on top, wallpaper/stats behind), a menu bar
  with a dropdown launcher, a taskbar, a clock, network/BLE/kana status icons, a boot
  animation, and a dozen dialog mixins (`FileManagerMixin`, `ConfirmDialogMixin`,
  `ConfigDialogMixin`, etc.) all `include`d into one `SystemDesktopApp < FmrbApp`.
- `main/prebuild_scripts/kernel/system_desktop/file_manager.rb` (`FileManagerMixin`) —
  real file browsing: `Dir.open`/`File.size`/`File.unlink` backed listing with `..`
  navigation, single-click-selects/double-click-activates, a right-click context menu
  (Run/Edit/Copy/Delete/Paste), full keyboard nav (arrows, PgUp/PgDn, Home/End, Enter,
  Tab-opens-context-menu, Delete key), a scrollbar widget.
- `main/prebuild_scripts/default_app/editor.app.rb` + its `editor/` directory
  (`clipboard.rb`, `const.rb`, `debug_pane.rb`, `file_run.rb`, `i18n.rb`, `input.rb`,
  `keys.rb`, `menu.rb`, `palette_ui.rb`, `render.rb`, `search.rb`, `ti_ui.rb`) — a real,
  large MS-DOS-style text editor: menu bar, syntax highlighting, a type-inference
  completion/hover/diagnostics engine, Find/Replace, clipboard, an on-device debugger
  pane, F5 "Run", kana input composition, key-repeat. `EditorApp < FmrbApp` alone is 838
  lines before any of its dozen included mixins.

**Not adopted — this project's established minimal-vertical-slice pattern, same as every
prior phase** (the audio phase shipped exactly 2 of a much richer reference audio API;
the game phase shipped one small arcade game, not the reference's boss-and-sprite
shoot-'em-up). This phase similarly does **not** adopt: dual-canvas
compositing/wallpaper images, the boot animation, dropdown menus, a launcher, any of the
half-dozen system dialogs (About, Config, Storage, Network, Clock-setting, Shortcuts),
right-click/context menus (no right button on a touchscreen anyway), scrollbar widgets,
Copy/Paste/Delete file operations, cross-app spawn-with-argument (file manager launching
the editor on a specific file), syntax highlighting, completion/diagnostics, Find/Replace,
clipboard, key-repeat, kana/IME composition, or any modifier key beyond Shift.

What **is** adopted, scaled down: a real taskbar (not a menu bar/dropdown — touch-first,
matches this project's existing single-pointer input model, no keyboard-only-reachable
menu concept needed); real `Dir`/`File`-backed browsing with `..` navigation and
directory/file distinction; a real load-edit-save text editor with cursor movement,
insert, backspace/join-line, and a save action — the same "prove the shape, not the
feature catalog" pattern this project has followed through every phase so far.

## Scope decisions

- **Desktop shell = taskbar, not menu bar + launcher + dropdown.** Family-mruby-os's
  desktop is keyboard-and-mouse-first (a real PC-like menu bar you can drive with
  F10/arrows/Enter). acid OS v2's own windowing-phase spec already chose single-pointer
  touch as "this phase's entire input vocabulary" and this phase's own keyboard
  addition (below) is scoped to *text entry in an already-focused window*, not menu
  navigation. A row of tap-to-switch app buttons is the touch-native equivalent of a
  taskbar/alt-tab, and is the smallest thing that satisfies "shows and lets you switch
  between running apps" from the user's own instruction.
- **File manager and editor are independent, both spawned at boot — no cross-app
  launch.** The reference's file manager "Edit" action calls `spawn_app(path, file)`,
  passing a runtime argument to a freshly spawned app. `kernel_spawn_app` (`v2/core/
  kernel/kernel_spawn.c`) has no argument-passing mechanism today, and it is not
  exposed to Ruby at all — every app spawn in this project happens from C boot code
  (`sim_main.c`/`app_main.c`), never dynamically from a running app. Building dynamic
  inter-app spawn-with-argument (task creation from inside another task's context,
  argument plumbing through `vm_host_params`, resource-limit questions) is real scope
  beyond "ship a file manager and an editor that each work" — explicitly cut, same
  category of cut as the windowing phase's resize/keyboard cuts. The editor instead
  always opens one fixed file (`v2/home/notes.txt`, a real seed file this phase
  creates); the file manager's "open" action on a text file shows an in-window preview
  (read-only, via `File.read`) rather than launching the editor.
- **No file manager Delete/Copy/Paste.** `File.unlink` exists and works (`mruby-io`),
  but a destructive action plus a confirmation-dialog concept is more surface than a
  first-pass browsing/preview tool needs, and risks the demo deleting its own seed
  files during testing. Browsing + preview only.
- **Keyboard vocabulary: printable ASCII (resolved for Shift) + Enter/Backspace/
  Escape/Tab/Delete/arrows.** No Ctrl/Alt/function keys, no key-repeat-while-held, no
  IME/kana composition. This is exactly enough for the editor to be a real, if basic,
  text editor and for the file manager to be keyboard-navigable, without building a
  general modifier-key/repeat-timer subsystem this phase doesn't otherwise need.
- **`SDL_GetKeyboardState`, not a second `SDL_PollEvent` consumer.** Read
  `v2/components/lovyangfx/src/lgfx/v1/platforms/sdl/Panel_sdl.cpp`: `Panel_sdl::
  _event_proc()` already calls `SDL_PollEvent` in a loop (for its own L/R
  rotate and 1-6 zoom hotkeys), running continuously on the main thread via
  `Panel_sdl::loop()`. `SDL_PollEvent` drains the queue — a second independent
  `SDL_PollEvent` loop anywhere else would race it, each call silently stealing some
  events from the other (confirmed by reading the function: it has no dispatch-to-
  multiple-consumers concept, it's a single `while (SDL_PollEvent(&event))` drain).
  `SDL_GetKeyboardState` is a different SDL API entirely — a live snapshot array
  (`Uint8[]`, one entry per `SDL_Scancode`) that SDL's internal event pump keeps
  current as a side effect of `_event_proc`'s own polling; reading it does not consume
  anything and cannot race. The new keyboard HAL polls this snapshot once per router
  tick and diffs it against the previous tick's snapshot to synthesize press
  transitions itself, entirely independent of `Panel_sdl`'s own event loop.
- **Cross-thread read of `SDL_GetKeyboardState`'s array is the same category of
  cross-thread read this codebase already relies on.** `hal_input_poll_touch`'s
  existing sim implementation already reads `monitor.touch_x/touch_y/touched` (plain
  fields SDL's event thread writes in `Panel_sdl::_event_proc`) from the FreeRTOS
  thread with no additional lock beyond `gfx_get_lock()` (which protects the shared
  `LGFX` object's rendering state, not this data). `SDL_GetKeyboardState`'s backing
  array is analogous: written by the same main-thread event pump, read here from the
  FreeRTOS thread. No new synchronization primitive is introduced; none was needed for
  touch either.
- **Verified empirically, not assumed: `File`/`Dir`/`Array#insert`/`String#rindex`/
  `String#split`/`Integer#chr` all work in this project's actual compiled build.**
  `v2/components/mruby/mrbgems/default.gembox` → `stdlib-io.gembox` → `mruby-io`
  (`File`) + `mruby-dir` (`Dir`); `default.gembox` → `stdlib.gembox` → `mruby-array-ext`/
  `mruby-string-ext`. Confirmed not just by reading the gembox chain but by building
  and running four throwaway apps against the live `v2/sim` binary during this design
  pass (write/read/size/exist? round-trip on a real file; `Dir.open` on a real
  directory listing it back; `Dir.open` on a *non*-directory raising a catchable
  `Errno::ENOTDIR`; `Array#insert`, `String#rindex`, `String#split`, `Integer#chr`) —
  every one printed `PASS` from inside a live app VM. No new C binding is needed for
  filesystem access; the file manager and editor are pure Ruby against the classes
  already in this build.
- **`v2/home/` is a new, small, committed sandbox directory**, not the real host
  filesystem's root. `mruby-io`/`mruby-dir` wrap real POSIX calls — there is no virtual
  filesystem image of acid OS v2's own — so *some* real directory is what gets
  browsed. Browsing the actual host `/` (or a noisy `/tmp`) from inside a hobby-OS demo
  is a strange product experience and untestable reproducibly across machines. `v2/home/`
  (two seed files, `README.txt` and `notes.txt`) is self-contained, committed, and
  gives both new apps something real and reproducible to open. Paths resolve relative
  to the process's own working directory, the same already-documented convention
  `kernel_spawn_app`'s `script_path` itself relies on (must be launched from the repo
  root) — noted so a future reader doesn't relearn this the hard way twice.
- **Legibility complaint — addressed within this phase's own files, screen resolution
  is not.** The user's live complaint ("near-black `THEME_BG` + small window = just
  green dots") is real and this phase touches exactly the files where the actual fix
  lives: `THEME_HARD` (acid green) was always documented as *"accent, borders,
  pressed states, cursor"* (`v2/core/kernel/kernel_theme.h`'s own comment,
  `docs/BUILD_LOG.md`'s 2026-08-02 entry) — never body text or a fill color — but
  nothing enforced that discipline yet since this is the first phase adding
  substantial new UI surface. Every new screen this phase adds (taskbar buttons, file
  manager listing, editor body) uses `THEME_PANEL`/`THEME_TEXT` for surfaces and body
  text and reserves `THEME_HARD` for accents/selection/cursor only, exactly as
  documented; new windows also get meaningfully larger default sizes (220×160,
  240×170) than the tiny 140×100/120×100 demo-app placeholders. **Explicitly out of
  scope: the simulator's actual pixel resolution** (`LGFX lcd(320, 240)` in
  `hal_display_sim.cpp`) — that is the target hardware's own framebuffer size (or a
  bring-up placeholder for it), a hw-bring-up-phase concern, not a desktop-shell
  concern; `Panel_sdl` already has its own 1-6 zoom hotkeys for viewing it larger on a
  developer's monitor, which is the correct layer to solve "the window is small on my
  screen," not the OS's own palette or layout.

## Architecture

### 1. Keyboard input

**`v2/core/hal/hal_keycode.h` (new).** The keycode vocabulary `hal_input_poll_key`
reports and every consumer (router, Ruby binding, `v2/apps/lib/acid_keys.rb`) shares by
convention, not by a common header (this project's mruby build has no C/Ruby shared
header mechanism — `vm_host.c`'s own comment on why there's no `require`). Printable
keys report their resolved ASCII value (32-126) directly; non-printable keys use named
constants starting at 257 (never collides with ASCII):
`KERNEL_KEY_ENTER/BACKSPACE/ESCAPE/TAB/DELETE/UP/DOWN/LEFT/RIGHT`.

**`v2/core/hal/hal_input.h` (extended).** New `void hal_input_poll_key(int *keycode,
bool *pressed)`. Unlike `hal_input_poll_touch`'s level-triggered "is it down right now"
contract, this is edge-triggered: `*pressed` is true exactly when a *new* key press
happened since the last poll (never for a release, never for an already-held key — no
key-repeat this phase). `*keycode` is only meaningful when `*pressed` is true, same
convention as touch's `*x`/`*y`.

**`v2/sim/hal_input_sim.cpp` (new file).** Polls `SDL_GetKeyboardState` once per call,
diffs the full scancode array against a static previous-state snapshot, translates any
newly-pressed scancode to a keycode (resolving Shift at translation time — printable
letters/digits/punctuation get real upper/lower-case and shifted-symbol values, so no
caller downstream ever interprets a modifier bit itself, mirroring how
`hal_input_poll_touch` already hands back resolved coordinates rather than a raw device
event) and pushes it onto a small internal ring buffer (depth 16), draining one entry
per `hal_input_poll_key` call — this guarantees no dropped keystroke even if two
scancodes change state within the same 16ms router tick, which a naive "report at most
the single most-recent transition" design could lose.

**`v2/hw/main/hal_input_hw.c` (extended).** Always reports not-pressed — the same
honest-stub pattern every other `hw` HAL function already uses; Tab5's real keyboard
input (external USB/BLE keyboard, since Tab5 has no built-in one) is real
hardware-bring-up scope, not this phase.

**`v2/core/kernel/kernel_event.h` (extended).** New `KERNEL_EVENT_KEY = 3`. No struct
change needed — the existing 4-field `struct kernel_event {type, x, y, pressed}`
already has room: a key event uses `x` for the keycode, `pressed` for press/release
(always 1, since only presses are ever generated this phase), `y` unused.

**`v2/core/kernel/kernel_router.c` (extended) — focus model.** Touch already routes by
position (`kernel_window_find_at`); keyboard events have no position to hit-test
against, so they need a *focus* concept: which single window receives them.

**Decision: focus follows the most recently activated window** — "activated" meaning
*raised to front*, which already has exactly one trigger point in the existing code: a
fresh touch press landing on a window (`kernel_window_bring_to_front`, in the
`fresh_press` branch). A new `kernel_router_activate_window(void *task)` function does
both `kernel_window_bring_to_front(task)` *and* sets a new static `g_focus_task`,
called from that same existing call site. Task 3 (below) adds one more caller — tapping
a taskbar button — so there are exactly two ways to gain focus (click the window
directly, or tap its taskbar entry), both funneling through the one function, one rule.
No separate "focus" decision surface is introduced; whatever already gets raised to
front also gets keyboard focus. If the focused window closes, `g_focus_task` is cleared
(checked at the close-button unregister site) so a keystroke is never routed to a dead
task handle. Before anything is ever clicked, `g_focus_task` is `NULL` and key events
are silently dropped — the same "no window, no-op" behavior every other
nowhere-to-deliver input path in this file already has. This is simpler than
alternatives considered (auto-focus-the-newest-spawned-window adds a second,
competing "most recent" concept — spawn order vs. click order — for no real benefit;
every desktop OS's actual behavior is click-to-focus anyway, so this isn't even a
simplification trade-off, it's just correct).

`kernel_router_poll()` polls `hal_input_poll_key` once per tick, unconditionally,
before any of the touch-routing state machine's branches (so a touch drag in progress
never suppresses keyboard delivery to a *different* focused window) and forwards a
fresh press to `g_focus_task`'s queue via the existing non-blocking `send_event` (queue
depth 8, comfortably ahead of realistic human typing speed at a 16ms poll cadence — the
same best-effort tradeoff already accepted for touch).

### 2. Ruby-facing keyboard surface

**`v2/apps/lib/acid_keys.rb` (new).** `module AcidKeys` with constants
(`ENTER`/`BACKSPACE`/`ESCAPE`/`TAB`/`DELETE`/`UP`/`DOWN`/`LEFT`/`RIGHT`) numerically
identical to `hal_keycode.h`'s `KERNEL_KEY_*` values — the two are hand-kept in sync
(no shared header, per above), checked once in this phase's self-review and again
whenever either file changes. Loaded into every app's VM unconditionally by
`vm_host.c`, same load-order pattern as `acid_app.rb`/`acid_game.rb`.

**`v2/core/bindings/event_binding.c` (extended).** `acid_poll_event`'s existing
three-way return (`nil`/`:close`/`:moved`/`[x,y,pressed]`) gains one more shape: a key
event returns `[:key, code, pressed]` — a 3-element `Array` whose first element is the
symbol `:key`, distinguishing it unambiguously from touch's 3-element `[x, y, pressed]`
(`x` is always an `Integer`, never equal to a `Symbol` — no ambiguity, no new return
type introduced).

**`v2/apps/lib/acid_app.rb` (extended).** New `on_key(code, pressed)` hook (default
no-op, mirrors `on_touch`'s shape) and new `on_idle` hook (default no-op, called when
`acid_poll_event` times out with nothing having happened — needed by the desktop
shell, see below, but a generically useful addition to the one shared base class, not
desktop-specific plumbing). `start`'s dispatch gains one `elsif` for the `[:key, ...]`
shape before the generic-array (touch) fallback, and an `else on_idle` for the
timeout/nil case.

**`v2/apps/lib/acid_game.rb` (extended, in parallel).** `AcidGame` does not subclass
`AcidApp#start` — it has its own loop — so it needs the identical `[:key, ...]`
dispatch `elsif` added to its own loop too (its inherited `on_key` default no-op still
comes from `AcidApp` normally; only the *dispatch* needs duplicating, matching this
file's pre-existing duplication of `AcidApp`'s `:close`/`:moved` handling — not new
duplication this phase introduces). `AcidGame` does not need its own `on_idle`: its
`on_tick` already fires unconditionally every `TICK_MS`, which already *is* its idle
tick.

### 3. Window enumeration + activation (desktop shell's data source)

**`v2/core/kernel/kernel_router.h`/`.c` (extended).** New `void *
kernel_router_get_focus(void)` — a plain getter for `g_focus_task`, so a Ruby binding
can report which window is currently focused (for the taskbar to highlight it).

**`v2/core/bindings/window_binding.c`/`.h` (new file, new binding module).** Window-list
introspection is a distinct concern from chrome *drawing* (`chrome_binding.c`) — new
file, matching this project's established one-concern-per-binding-file pattern:

- `acid_window_max()` → `Integer`, the fixed slot count (`KERNEL_WINDOW_MAX`, 8) —
  Ruby iterates every slot and skips unused ones itself, rather than the binding
  hiding a "compacted count" concept the underlying fixed-array model doesn't actually
  have.
- `acid_window_info(i)` → `nil` if slot `i` is out of range or unused, else
  `[app_name, x, y, w, h, focused]` (`app_name` a `String`, `focused` a `bool` computed
  by comparing that slot's task handle against `kernel_router_get_focus()`).
- `acid_activate_window(i)` → `nil`; if slot `i` is in use, calls
  `kernel_router_activate_window` on its task — the exact same function a real click
  on that window would trigger, so a taskbar tap and a direct click are
  indistinguishable to the router.

### 4. Desktop shell

**`v2/core/kernel/kernel_layout.h` (extended).** `KERNEL_DESKTOP_STRIP_H`: 20 → 24, to
comfortably fit real taskbar buttons (label + padding) rather than the old bare
20px accent strip. `sim_main.c`/`app_main.c`'s desktop spawn call's own height
parameter is updated to match (kept numerically equal by convention, same "hand-kept
in sync, no shared source of truth beyond a header" situation the game phase's window
dimensions already established as this project's accepted pattern).

**`v2/apps/desktop.rb` (rewritten).** Replaces the current 9-line strip-only app.
`redraw` fills the strip (existing `acid_draw_desktop_strip` binding, unchanged) then
draws one button per *other* active window (`acid_window_max`/`acid_window_info`,
skipping its own slot by matching its own known `app_name` — literally the script path
it was spawned with, since `kernel_spawn.c` registers `app_name` as `script_path`
verbatim), highlighted (inverted to `THEME_HARD`, echoing `docs/BUILD_LOG.md`'s
documented "pressed state inverts to a solid `--hard` fill" convention) when that
window's `focused` flag is true. `on_touch` hit-tests the tap against the button grid
and calls `acid_activate_window`. `on_idle` (the new `AcidApp` hook) also triggers a
redraw, so the taskbar's focus highlight and window list stay live even when nothing
happens to *touch the desktop strip itself* — e.g. clicking directly from one app
window to another without an intervening desktop tap.

### 5. File manager

**`v2/home/README.txt`, `v2/home/notes.txt` (new, seed content).** A small, committed,
reproducible sandbox directory — not the host filesystem — per the Scope decision
above.

**`v2/apps/file_manager.rb` (new).** `Dir.open`/`File.size`/`File.exist?`-backed
listing rooted at `v2/home` (no navigation above it), `..` entry when not at root,
directories sorted before/alongside files (plain `sort`, matching the reference's own
`names.sort.each`), tap or Up/Down+Enter to select/activate, Backspace to go up,
Enter/tap on a file opens a read-only in-window text preview (`File.read`, first N
visible lines, Escape or a tap to close back to the listing). No Copy/Delete/Run/Edit —
explicitly cut, see Scope decisions.

### 6. Text editor

**`v2/apps/editor.rb` (new).** Always opens `v2/home/notes.txt` — no dynamic file
argument (cut, see Scope decisions). Loads the file into an `Array` of lines
(`File.read.split("\n")`); holds `@cx`/`@cy` cursor position and `@scroll_y`. Redraws
the whole window every keystroke (same "just repaint everything, no dirty-tracking"
simplicity this project's `AcidGame` already established for a 20fps game loop — an
editor redrawing on every keystroke, not every 50ms, is an even easier case). Arrow
keys move the cursor (Left/Right wrap across line boundaries at start/end-of-line,
matching ordinary text-editor behavior); Enter splits the current line; Backspace
deletes the character before the cursor, or joins with the previous line at column 0;
any printable keycode (32-126) inserts that character. **Escape saves** (`File.open(...,
"w")`, writing `@lines.join("\n")`) — the one keyboard shortcut this phase defines,
shown as an on-screen hint (`"ESC=save"`) in the status line, since there is no Ctrl
key support this phase to spend on a more conventional Ctrl-S.

## Data flow — one keypress, start to finish

1. `kernel_router_poll()` calls `hal_input_poll_key`; the sim HAL diffs
   `SDL_GetKeyboardState` against last tick's snapshot, finds one newly-pressed
   scancode, translates it (resolving current Shift state) to a keycode, returns it.
2. The router looks up `g_focus_task`'s window by task handle and posts
   `{type: KERNEL_EVENT_KEY, x: keycode, pressed: 1}` to its queue (non-blocking,
   best-effort, same as every other high-frequency event this file sends).
3. That app's own `AcidApp#start` (or `AcidGame#start`) loop's next
   `Kernel.acid_poll_event` call drains it, sees `[:key, code, pressed]`,
   dispatches to `on_key(code, pressed)`.
4. The app (editor or file manager) updates its own state and calls `redraw`, which
   issues `acid_fill_rect`/`acid_draw_text`/`acid_fill_circle` calls exactly as any
   other app already does — no new drawing primitives this phase.

## Data flow — one taskbar tap

1. A touch lands inside the desktop strip's y-range; `kernel_router_poll`'s existing
   desktop-strip special case (unchanged) routes it to the desktop's own queue as an
   ordinary `KERNEL_EVENT_TOUCH`, window-relative.
2. `DesktopApp#on_touch` hit-tests the x-position against its own button-slot grid
   (purely in Ruby, the kernel has no concept of taskbar buttons) and calls
   `acid_activate_window(i)`.
3. The binding looks up slot `i`'s task handle and calls
   `kernel_router_activate_window` — the exact same function a direct click on that
   window would call — raising it to front and setting keyboard focus to it, in one
   step, with one rule.
4. The desktop's own next `redraw` (triggered by its `on_idle` tick, since nothing
   touched the desktop strip again) picks up the new focus state and repaints the
   highlighted button.

## Error handling

Unchanged from the windowing phase's existing model: an app's mruby VM faulting still
degrades to a parked/exited task without affecting the router or any other app task —
nothing about keyboard focus or window enumeration changes that. One new specific
case: if the focused window closes (click its own close button) mid-keystroke, the
router clears `g_focus_task` at the same point it already unregisters the window (see
Architecture §1); a keystroke that was in flight to that window's queue in the narrow
window before that (already posted, not yet drained) is simply never consumed once
that app's own event loop exits on `:close` — a harmless dropped keystroke in a race
window too narrow to matter in practice, the same category of accepted best-effort
tradeoff already documented for touch's per-tick sends.

## Testing / definition of done

Same discipline as every prior phase: no automated test suite; real verification via
Xvfb + `xdotool` + `import`/`convert` screenshots, run against the real `v2/sim`
binary, plus `puts`-based throwaway-app checks for anything not visually observable
(already used during this design pass itself to confirm `File`/`Dir`/`Array#insert`/
`String#rindex`/`Integer#chr` really work in this build before designing against them).

Done when, on `sim`:
- Typing on a real (host) keyboard while an app window (editor) has focus inserts the
  correct characters, including Shift-cased letters and shifted punctuation, at the
  correct cursor position, confirmed via screenshot (not just "it didn't crash").
- The taskbar shows one button per running app (excluding the desktop's own window),
  correctly highlights whichever window most recently gained focus (by direct click
  *or* taskbar tap), and tapping a button raises that window and focuses it for
  keyboard input — confirmed by then typing and seeing it land in the newly-focused
  window, not the previously-focused one.
- The file manager lists `v2/home`'s real contents, navigates into/out of
  subdirectories, and previews a text file's real content.
- The editor loads `v2/home/notes.txt`'s real content on open, accepts edits, and
  Escape writes real changes back to that file on disk — confirmed by re-reading the
  file's content from outside the running process (a throwaway file-manager preview or
  a fresh `File.read` in a temp app) after closing/reopening.
- Every pre-existing app (`demo_touch`, `demo_swatch`, `acid_blaster`) still works
  unmodified, and the whole boot sequence (five app windows plus the desktop, six
  total, under `KERNEL_WINDOW_MAX`'s cap of 8) comes up clean.
- `v2/hw`: statically verified only (no ESP-IDF on this machine, same pre-existing gap
  as every prior phase). **Additional, explicitly flagged hw gap this phase
  introduces:** `mruby-io`/`mruby-dir`'s `File`/`Dir` classes wrap real POSIX calls,
  which on ESP-IDF require a mounted VFS (SPIFFS/LittleFS) that nothing in `v2/hw`
  sets up today — the file manager and editor will build cleanly for `hw` but their
  filesystem calls will not function on real hardware without that mount, which is
  real hw-bring-up scope (a later roadmap phase), not something this phase's
  static-only verification can close.

## Roadmap (updated)

1. Bring-up — DONE
2. Windowing/GUI — DONE
3. Audio — DONE
4. First real app/game — DONE
5. Desktop shell, keyboard, file manager, editor (this spec)
6. Real Tab5 hardware bring-up (flash + run) — including mounting a real filesystem
   for `File`/`Dir` to work on-device, which this phase's static-only `hw` changes
   defer to here
7. Custom cyberdeck hardware
