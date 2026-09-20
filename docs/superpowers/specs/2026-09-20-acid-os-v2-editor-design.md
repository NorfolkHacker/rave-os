# acid OS v2: a full editor (phase 5 follow-on) — design

**Goal:** grow `v2/apps/editor.rb` from the phase-5 minimal slice (cursor movement,
insert, backspace, one save key — 246 lines) into an editor you can actually write an
app in: undo/redo, selection and clipboard, find, go-to-line, save-as, Ruby syntax
highlighting, touch editing, and a Run command that launches the file being edited.
Everything runs unmodified on `v2/sim`; `v2/hw` gets the same source changes, statically
verified only, per every prior phase's documented ESP-IDF gap.

## Reference model

Read directly from family-mruby-os's real source, re-cloned for this work (session
scratchpads don't persist; re-reading real source rather than memory of it is this
project's established discipline):

- `main/prebuild_scripts/default_app/editor.app.rb` (838 lines) plus its `editor/`
  directory — `clipboard.rb`, `const.rb`, `debug_pane.rb`, `file_run.rb`, `i18n.rb`,
  `input.rb`, `keys.rb`, `menu.rb`, `palette_ui.rb`, `render.rb`, `search.rb`,
  `ti_ui.rb`. Roughly 4,950 lines in total.

What that editor has: a File/Edit/View menu bar with `Alt` hotkeys and dropdowns;
Open/Save/Save-As/Template/Exit; Cut/Copy/Paste/Select-All over an anchor-based
selection; View toggles for highlight, word wrap, fullscreen and a colour palette
picker; a Find/Find-next dialog; `F5` Run; per-line-cached syntax highlighting whose
default is chosen by file extension; a paged `Alt+K` key-list panel; `F1` API help and
`Ctrl+T`/`Ctrl+E` type queries backed by their Spinel type engine; an on-device debugger
pane with `F9` breakpoints; and Japanese i18n with kana composition.

**Adopted as concepts, rebuilt rather than ported:** undo-capable editing, an
anchor-based selection with a clipboard, find/find-next, extension-driven syntax
highlighting with a per-line cache, save-as, and running the edited file.

**Not adopted:** the menu bar and its `Alt`/`Ctrl`/function-key scheme (see below — the
keys do not exist here, and would be useless on the target hardware), the type-inference
engine and everything built on it (`F1` help, `Ctrl+T`, `Ctrl+E`), the debugger pane and
breakpoints, i18n and kana composition, word wrap, the colour-palette picker,
fullscreen, and templates. Each is either a much larger subsystem than this app warrants
or depends on a runtime acid OS v2 doesn't have.

## The constraint that shapes the whole design

`v2/sim/hal_input_sim.cpp`'s `translate_scancode` maps SDL scancodes to this project's
keycode vocabulary and **returns 0 for anything it doesn't map — explicitly including
Ctrl, Alt and the function keys**. `apps/lib/acid_keys.rb` is the whole vocabulary an
app ever sees: printable ASCII 32–126 (with Shift already resolved into the character,
so `Shift+Left` is indistinguishable from `Left`), plus `ENTER`, `BACKSPACE`, `ESCAPE`,
`TAB`, `DELETE` and the four arrows.

So the reference's entire command surface — `Ctrl+S`, `Ctrl+C`, `Alt+K`, `F3`, `F5`,
`F9` — is unreachable. Two ways out were considered:

1. **Extend the HAL** so modifiers and function keys reach apps (`hal_keycode.h`,
   `translate_scancode`, `hal_input_hw.c`, `kernel_router.c`, `acid_keys.rb`), then copy
   the reference's keymap.
2. **Design a command surface that needs no modifiers at all.**

(2) is chosen, and not only to avoid the kernel change. The hardware target is an
M5Stack Tab5 — a touchscreen tablet. A modifier-key-driven editor is the wrong shape for
a device with no modifier keys, and building one would mean the editor's primary command
path is the one path the real hardware can't offer. A command surface that works by
tapping is strictly better there, and costs nothing on the sim.

This is also the clearest point of departure from the reference, deliberately: the
family-mruby editor is an MS-DOS-style keyboard application, and acid OS v2's is not.

## Scope decisions

- **Command mode on ESC, not a menu bar.** ESC raises a command strip over the bottom of
  the text area; one keypress runs a command, and commands that need text take it on the
  prompt row below the strip. Frequent operations cost two keystrokes (`ESC u` to undo). On
  hardware with no keyboard, tapping the status line enters command mode and the strip's
  letters are tap targets — the same surface, driven by the same code, with no keyboard
  involved. ESC's current meaning (save) moves to `ESC s`.

- **Mark-based selection, not `Shift`+movement.** Shift is resolved at translate time,
  so shifted arrows cannot be detected. `ESC m` sets the mark at the cursor, movement
  extends the selection, `ESC m` again clears it. This is a constraint turned into a
  usable idiom rather than a workaround.

- **Operation-record undo, not buffer snapshots.** Snapshotting `@lines` per edit is the
  obvious implementation and the wrong one here: apps run in a fixed-size mruby pool
  (`vm_host.c` logs its usage per VM), and 200 copies of a 300-line file is not a cost
  this runtime should pay for a feature this ordinary. Records are
  `insert`/`delete`/`split`/`join`/`paste`, each carrying only the affected text and
  position.

- **Per-line highlighting only.** Multi-line strings, heredocs and `=begin` blocks are
  out of scope and documented as such: a line is tokenized in isolation so an edit
  invalidates exactly one cache entry. Getting heredocs right means re-tokenizing from
  the top of the file on every edit, which is the wrong trade for the payoff.

- **App-local clipboard.** A clipboard shared with the Terminal and File Manager would
  be a kernel service with its own lifetime and ownership questions. Not now; noted as a
  natural later addition.

- **The window grows 240×170 → 420×280.** 25 text lines × 65 columns, up from 14 × 35.
  The screen is 640×360, so two editors still fit side by side, and `multi = true` is
  unchanged.

## Per-app libraries

This runtime has no `require`. `vm_host.c` loads a fixed list of `v2/apps/lib/*.rb`
files into every app VM, then the app's own script. An editor with these features is
roughly 900 lines — nearly double `desktop.rb`, today's largest file — and putting its
tokenizer into the global lib list would make every game, the Terminal and the desktop
parse and hold it for nothing.

So manifests gain a `libs` field:

```toml
# v2/apps/editor.app.toml
libs = editor/buffer.rb, editor/hl.rb, editor/cmdbar.rb, editor/touch.rb
```

`desktop.rb` already parses every `.app.toml` at boot and calls `acid_launcher_register`;
that call gains the field, `struct launchable_app` in `window_binding.c` gains a `libs`
member alongside `multi`, and `kernel_spawn_app` passes it into the VM params. Because
the value lives in the registry and is looked up by path, **both** launch paths — the
Menu and File Manager's `launch_manifest` — get it without touching `acid_spawn_app`'s
signature.

**Trust boundary.** A manifest is app-controlled data and `libs` names files the VM will
execute. Paths are resolved under `v2/apps/` in C, rejecting absolute paths and any
`..` component, before anything is opened. This is the same boundary as the Terminal
sandbox escape fixed in `6cd6279`, and it is the reason the check lives in C rather than
in `desktop.rb`'s parser.

### Resulting layout

```
v2/apps/editor.rb          ~260  app shell: on_create, redraw, key routing, status line
v2/apps/editor/buffer.rb   ~230  lines, cursor, undo/redo, selection, clipboard
v2/apps/editor/hl.rb       ~160  Ruby tokenizer + per-line cache
v2/apps/editor/cmdbar.rb   ~180  command mode, prompts, find, goto, save-as, run
v2/apps/editor/touch.rb    ~110  tap-to-place, drag-select, flick-scroll
v2/apps/editor/layout.rb    ~75  geometry constants shared by EditorApp, EditorCmd, EditorTouch
```

`layout.rb` wasn't part of the original plan — it was pulled out mid-build after `EditorCmd`
raised `uninitialized constant EditorCmd::STATUS_Y` on its first ESC. Ruby resolves a bare
constant lexically, through a method's own module nesting and that nesting's ancestors,
never through whatever class happens to include the module the method lives in — so
`EditorApp include`-ing `EditorCmd` does not put `EditorApp`'s own constants (`STATUS_Y`,
`GUTTER_W`, etc.) within `EditorCmd`'s reach. The fix isn't for a mixin to reach into its
includer; it's `EditorLayout`, a module both `EditorApp` and its mixins (`EditorCmd`,
`EditorTouch`) genuinely include, so the shared geometry is in every side's own ancestry.

`buffer.rb` and `hl.rb` call no `acid_*` binding at all — they are pure data structures
over arrays of strings. That is a testability decision as much as a layering one (see
Testing).

## Command mode

ESC opens it. The status line turns into a prompt row, and a three-row strip is drawn
over the bottom of the text area above it:

```
 ...text above continues...
├──────────────────────────────────────────────────────────┤
│ s save   a save-as  q close   ! run    u undo   r redo   │
│ x cut    c copy     v paste   m mark   / find   n next   │
│ g goto   t top      b bottom  h hilite p prev   ? keys   │
│ :                                                        │
└──────────────────────────────────────────────────────────┘
```

Three fixed rows, not a paging list: at 65 columns the nineteen commands fit with room
to spare, and a fixed strip means a command's position never moves, which is what makes
the tap targets learnable. `?` restates the same list with fuller descriptions for
anyone who wants prose.

Commands act immediately except `a` (save-as), `/` (find) and `g` (goto), which leave
the strip up and take text on the prompt row — `find: on_touch_` — accepting printable
characters, `BACKSPACE`, `ENTER` to run and `ESC` to cancel. The last find query is
remembered so `n`/`p` repeat it without reopening the prompt.

`q` on a modified buffer asks `unsaved changes — q again to close, any key to cancel`
rather than opening a dialog window; there is no dialog concept in this app framework
and a status-line confirmation needs none.

## Feature designs

### Undo/redo (`buffer.rb`)

Every mutation goes through `Buffer`, which appends a record to `@undo` and clears
`@redo`. A record is `[type, x, y, text, cx, cy]` — type, where it happened, the text
involved, and the cursor position before it. Undo applies the inverse and pushes onto
`@redo`.

Consecutive `insert` records coalesce when they are adjacent, on the same line, and the
inserted character is not whitespace — so typing a word is one undo step, and the space
after it ends the group. `split`, `join`, `paste`, `cut` and any cursor jump end a group
unconditionally. The stack caps at 200 records, dropping oldest first.

### Selection and clipboard (`buffer.rb`)

`@mark_x`/`@mark_y`, `nil` when unset. `selection_range` normalises mark and cursor into
document order, so the rest of the code never asks which end is which. `copy` joins the
spanned text with `\n` into `@clipboard`; `cut` copies then deletes as one undo record;
`paste` inserts at the cursor as one record — unless there's an active selection, in
which case it costs two (`delete_selection`, then `insert_text`), not one compound
record. That's deliberate, not an oversight: the intermediate state after only the
first of the two is undone is exactly the post-delete buffer, the same state pressing
`Delete` alone would leave, so it's a coherent stopping point rather than a corrupt
half-step. A single compound record covering "replace this span with this text" would
need a third undo-record type alongside the two primitives (`insert`/`delete`) the rest
of the design already covers everything else with, for a case that already round-trips
correctly through the existing two. Pinned by `test_editor.rb`'s "paste over selection
(two-record behaviour)" group. Rendering fills the selected span of each visible line
with `SEL_BG` before drawing its text.

### Ruby highlighting (`hl.rb`)

`Hl.tokenize(line)` returns `[[text, color], ...]` for one line, recognising comments,
single- and double-quoted strings, `:symbols`, numbers, `@ivars`, `Constants` and a
keyword set (`def end class module if elsif else unless while until do return yield
nil true false self and or not begin rescue ensure case when then`).

The cache is an array parallel to `@lines`; an edit to line *n* clears entry *n* only,
and insert/delete of a line splices the cache to match. `draw_lines` walks tokens and
issues one `acid_draw_text` per token instead of one per line. That is more draw calls,
which is affordable now that canvas draws no longer round-trip through the SDL backend
per call (see `gfx.h`'s `gfx_present`).

Colours are the wallpaper's neon palette, not the reference's:

| token | colour | |
|---|---|---|
| keyword | `0xFF2D78` | neon pink |
| string | `0xFFD400` | yellow |
| number | `0x00E5FF` | cyan |
| symbol, constant | `0xB026FF` | violet (`THEME_VIOLET`) |
| ivar | `0xFF7A00` | orange |
| comment | `0x9DAAA3` | `THEME_MUTED` |
| everything else | `0xD4E6DB` | `THEME_TEXT` |

On by default for `.rb`, off otherwise, `ESC h` toggles per buffer.

### Touch (`touch.rb`)

A press in the text area places the cursor at the character under it, accounting for
`@scroll_x`/`@scroll_y`; holding and moving sets the mark at the press point and extends
the selection. A press in the gutter selects that whole line. A vertical drag in the
gutter scrolls. A press on the status line enters command mode, and in command mode a
press on a letter in the strip runs it.

Press and release are tracked explicitly here rather than with the usual
press-once-per-hold guard, because dragging is exactly the case that needs the repeat
events the guard exists to suppress. The guard still applies to the command strip's tap
targets, which are one-shot.

### Run (`ESC !`)

Saves, then looks for `<name>.app.toml` beside the file being edited and spawns the
file with that manifest's `w`/`h` (falling back to 240×170 with no manifest). Only for
`.rb` paths; anything else reports `not a ruby file` on the status line. Editing an app
and seeing it run is then a two-keystroke loop with no rebuild — which is what the
editor's own doc comment already claims the app is for.

### Saving (`editor.rb`)

`save_file` never truncates `@path` directly. It writes the full buffer to a sibling
path (`@path + ".editor-save-tmp"`), and only on a fully successful write does
`File.rename` move that sibling over `@path`. `File.open(@path, "w")` truncates the
moment it succeeds, so writing straight to `@path` would leave a failed save (a full
disk, a yanked SD card on the hardware target) with the original gone rather than
merely unsaved, and no way back in from inside this OS. A write or rename failure
leaves `@path` byte-for-byte untouched and best-effort deletes the leftover temp file.

Before that write, `backup_own_source` copies whatever is currently on disk at `@path`
to `<path>.bak` — but only when `EditorLayout#own_source?(@path)` is true. `own_source?`
answers yes for the editor's own source and its mixins (this file, `buffer.rb`, `hl.rb`,
`cmdbar.rb`, `layout.rb`, `touch.rb`) plus `lib/acid_app.rb` (every app loads it, so a bad
save there bricks every app, the editor included) — anchored to the two roots those
files can actually be reached through (`v2/apps/` and the live `v2/fsroot/App/` symlink)
and matched against the exact relative path list, not a bare filename-tail suffix: an
earlier suffix-only version of this check false-positived on a user's own
`v2/fsroot/Home/editor.rb`. This is the one editor feature that can edit and immediately
re-run the very code it's running as, so it's also the one editor feature that can brick
itself on a bad save; the backup exists for exactly that case, and a backup failure never
blocks the real save.

## Status line

```
acid_blaster.rb •     14,9   334L   hl
```

Filename, a `•` when modified, cursor line/column, total lines, and `hl` when
highlighting is on. Transient messages (`saved`, `not found`, `nothing to undo`,
`not a ruby file`) replace the left portion until the next keypress or touch — the same
lifetime the current editor's `@saved_flash` already has, and it needs no timer, which
this app framework gives no reliable way to run anyway.

## Testing

`buffer.rb` and `hl.rb` depend on nothing but the mruby core, so they run headless under
the vendored `v2/components/mruby/build/host/bin/mruby`. A new `v2/tools/test_editor.rb`
asserts:

- undo coalescing (a typed word undoes as one step; the space ends the group), undo past
  the start of the stack, redo invalidated by a new edit, and the 200-record cap;
- selection ranges with the mark before and after the cursor, spanning lines, and
  degenerate (mark == cursor);
- cut/paste round-trips, including a paste that spans lines and a cut at end-of-buffer;
- find forward, wraparound, no-match, and a match on the cursor's own line;
- tokenizer output for a comment mid-line, a string containing `#`, a symbol next to a
  colon, a number in an identifier, an ivar, and a keyword that is a substring of an
  identifier (`ending` must not highlight as `end`).

`cmdbar.rb`, `touch.rb` and the shell are verified live in the sim, by driving the real
window — the project's existing practice.

## Order of work

1. Per-app `libs`: manifest field, registry, `kernel_spawn_app`, `vm_host`, path
   validation. Verified by moving nothing yet — an empty `libs` must change nothing.
2. `buffer.rb` with undo/redo, selection, clipboard, plus its tests; editor.rb rewired
   onto it with no new UI.
3. Command mode and the status line.
4. Find, goto, save-as, the close confirmation.
5. `hl.rb` and its tests; highlighting in `draw_lines`.
6. `touch.rb`.
7. Run.
8. Window resize to 420×280 and a pass over the layout constants.

## Out of scope, deliberately

Word wrap; a colour-palette picker; fullscreen; templates; open-a-different-file from
within the editor (File Manager already launches an editor per file, and `multi = true`
means several can be open); multi-line-aware highlighting; a cross-app clipboard; key
repeat; regular-expression or replace-all search; and anything touching the type-inference
or debugger surfaces the reference builds on Spinel.
