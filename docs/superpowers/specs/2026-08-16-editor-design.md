# An in-OS text editor for Rave-OS

## Purpose

`docs/IDEAS.md`'s "upgrade the FILES window" entry named two candidate
directions and left both undecided: multi-select/move/copy (now shipped)
and an in-OS text editor for authoring `/BIN` scripts and `/ETC/CONFIG`
without scratch-image byte editing. This spec picks up the second. Today
there is no way to change an existing file's content at all from inside
Rave-OS — FILES can create an empty file or delete one, SHELL can `cat`
one, but nothing can write new bytes into a file that already exists. The
goal is a real, editable, multi-line text buffer, opened by typing
`edit <path>` in SHELL.

## Scope

**In scope**: a new `kernel/editor.c`/`editor.h` module owning a
fixed-size, multi-line text buffer with real cursor movement (arrow
keys, Backspace, Enter-inserts-a-newline-at-cursor); a new
`WIN_KIND_EDITOR` window in `kernel.c` that renders it and owns a `SAVE`
button; SHELL's `edit <path>` opening that window loaded with the target
file's content (or empty, if the path doesn't exist yet); saving via
`fs_delete()` + `fs_create_file()` (no new `fs.c` write primitive).

**Out of scope, deliberately**: opening the editor from FILES (click or
otherwise) — `edit` is SHELL-only for v1, the same way `mv`/`cp` reach
only SHELL and not FILES' clipboard in this codebase's prior stage;
FILES itself is untouched by this spec. Home/End/Delete key support —
`keyboard.h` only decodes Up/Down/Left/Right as extended keys today;
adding Home/End/Delete scancode decoding would be real new scope in
`keyboard.c` with unverified headless-QEMU support (this project has a
documented history of specific keys not registering over the monitor
socket), so v1 supports only what's already decodable: Left/Right/Up/
Down/Backspace/Enter/printable characters. A future stage can add the
three missing keys to `keyboard.c` and wire them in without changing
`editor.c`'s own structure. Files larger than the fixed buffer
(`EDITOR_BUF_SIZE`, 512 bytes) fail to open with one generic
`edit: failed`, matching `cat`'s existing collapse of every read failure
into one message — no paging, no streaming a larger file through a
smaller buffer. No undo/redo. No syntax highlighting. No search/replace.
No "unsaved changes" confirmation on close — closing the window discards
the in-memory buffer unconditionally, matching every other window in
this kernel (none have modal confirmations of any kind). No keyboard
shortcut for Save (e.g. Ctrl-S) — this kernel has no modifier-combo
handling anywhere yet; `SAVE` is a button, matching FILES' own
button-driven chrome (`NEW DIR`/`DELETE`).

## Design

### The buffer and cursor model (`kernel/editor.c`/`editor.h`)

```c
#define EDITOR_BUF_SIZE 512

struct editor {
    char buf[EDITOR_BUF_SIZE];
    unsigned int len;      /* bytes currently used in buf */
    unsigned int cursor;   /* byte offset into buf, 0..len */
};
```

`EDITOR_BUF_SIZE` (512) matches this codebase's established "one
sector's worth" sizing already used for `VIEWER_BUF_SIZE`/
`SHELL_CAT_BUF_SIZE` for the identical reason: every real file this OS
has ever stored is tiny (`/ETC/CONFIG` is 5 bytes; `/BIN` scripts are
short Forth snippets), so 512 bytes is generous headroom, not a tight
fit.

The cursor is a single byte offset, not a row/col pair — simpler state,
with row/col derived by scanning for `\n` bytes only when needed (for
drawing the caret, and for Up/Down's column-matching). This buffer is
capped at 512 bytes, so scanning it on every keystroke is cheap; no gap
buffer, rope, or other amortized-insert structure is needed.

**Key handling** (mirrors `console_input.c`'s existing `feed_char`/
`move_cursor` shape, but generalized from "always at the end" to "at any
offset"):

- Printable characters: insert at `cursor`, shifting every byte from
  `cursor` to `len` right by one (bounded — if `len == EDITOR_BUF_SIZE -
  1`, the insert is a silent no-op, same "truncate rather than overflow"
  guarantee every bounded buffer in this codebase already gives), then
  advance `cursor` and `len` by one.
- **Enter**: inserts a literal `\n` byte at `cursor` via the same
  insert-and-shift path above — a real mid-buffer split, not an append.
- **Backspace**: if `cursor > 0`, removes the byte at `cursor - 1`
  (shifting everything after it left by one) and decrements `cursor`
  and `len`. Removing a `\n` merges the two lines it separated, which
  falls out naturally from this being a plain byte removal — no special
  case needed.
- **Left/Right**: `cursor -= 1` / `cursor += 1`, clamped to `[0, len]`.
  ASCII-only text, so no multi-byte-character stepping is needed.
- **Delete, Home, End**: out of scope for v1 (see Scope) — `keyboard.h`
  doesn't decode these as distinct keys yet, so there's nothing for
  `editor.c` to receive for them. Backspace and Left/Right already cover
  every edit and cursor move a Delete/Home/End keypress would otherwise
  shortcut.
- **Up/Down**: derive the cursor's current row and column (column =
  distance back to the nearest preceding `\n` or buffer start), find the
  target line (previous/next `\n`-delimited span), and set `cursor` to
  that line's start plus the same column, clamped to that line's actual
  length if it's shorter (standard editor behavior — moving up from a
  long line's far-right column onto a short line lands at that short
  line's end, not off past it).

**Rendering**: the buffer is walked once per frame, splitting on `\n`
into rows drawn top-to-bottom via the same `text_puts` calls
`console_output.c` already uses for scrollback lines, with a caret drawn
at the cursor's derived row/column — visually the same caret
`console_input.c` already renders, just positioned mid-text instead of
always trailing the last character.

### Window integration (`kernel.c`)

`WIN_KIND_EDITOR` joins the window-kind enum (`MAX_WINDOWS` grows by
one), closed at boot like every other window. Its rect reuses FORTH/
SHELL's existing 400×180 footprint — no new size to design, this is a
single-region editable pane, not a windowed-list-plus-footer shape like
FILES. `draw_editor_group()` mirrors `draw_forth_group()`/
`draw_shell_group()`'s exact shape: `window_draw()` then the editor
widget's own draw call, then `button_draw()` for `SAVE`. The window's
title is set dynamically to the currently-open file's path
(`"RAVE-OS EDIT: /ETC/CONFIG"`) rather than a fixed string like every
other window's title today — the first window in this codebase whose
title changes at runtime, since it's the first window whose whole
purpose is tied to a specific, user-chosen file. `move_window_content()`,
`draw_window_by_index()`, `draw_scene()`, `update_and_present()`, the
old-state snapshot, and `touched[]` all gain the same `WIN_KIND_EDITOR`
arm every other window kind already has in each of those places — no new
mechanism, just the established per-window-kind wiring repeated once
more.

`SAVE`'s click handler: `fs_delete(current_path)` (return value ignored
— if the file doesn't exist yet, that's expected and fine, not an
error), then `fs_create_file(current_path, editor.buf, editor.len)`.
Unifies "this file doesn't exist yet" and "overwrite this file's
existing content" into one code path, since after an unconditional
delete attempt both cases look identical to `fs_create_file`. A `SAVE`
failure (parent directory disappeared out from under the open editor,
disk full, etc.) is a silent no-op, matching this codebase's blanket
no-error-UI convention for filesystem mutations everywhere else (FILES'
DELETE/rename/create already work this way).

### SHELL integration (`kernel.c`'s Enter-key handler, not `shell.c`)

`shell_eval_line()` cannot itself open a window — by design, `shell.c`
has zero dependency on any GUI type (`shell.h` includes only `fs.h`),
the same isolation `forth.c` already maintains for itself. `RUN` already
established the precedent for a typed command needing to do something
`shell_eval_line()`'s plain text-in/text-out contract can't express:
`kernel.c`'s own Enter-key handler recognizes the special line *before*
falling through to the normal `shell_eval_line()` call. `edit <path>`
follows the identical shape: the handler checks for a leading `edit `
token, resolves the argument via `shell_resolve()`/`shell_case_correct()`
(`correct_last = 1` — the target is expected to already exist for the
common "edit an existing file" case; a genuinely new path still resolves
correctly since `shell_case_correct()` already degrades to leaving
unmatched components as-typed), attempts `fs_read_file()` into the
editor buffer (a failure here is treated as "start empty," not an error
— `edit` on a path that doesn't exist yet is exactly how a new file gets
created, mirroring FILES' own Enter-with-no-selection-creates-a-file
convention), sets `editor.cursor = editor.len` (start editing at the end
of whatever loaded, or at 0 for a fresh empty buffer), stores the
resolved path as the window's current file, sets the window title, and
opens+raises `WIN_KIND_EDITOR` — the same open+raise pattern `FORTH`/
`FILES`/`SHELL`'s own start-menu launchers already use. No separate
`shell_cmd_edit()` in `shell.c` — unlike `RUN` (which still calls into
`forth_run_command()` for the actual script execution after its own
interception), there is no `shell.c`-side work left to delegate once the
window-opening and file-loading both have to happen in `kernel.c`
anyway; inventing a `shell_cmd_edit()` that just formats an error string
your Enter-handler mostly discards would be duplicated plumbing for the
same information (Enter-handler still needs to have resolved a real
path before it can call `fs_read_file()` itself). SHELL's own scrollback
still echoes the typed `edit <path>` line, same as `RUN`'s command
gets echoed before its own interception fires.

## Testing

All headless, via the existing `-display none -monitor unix:...` harness
plus direct `fs.img` byte-parsing where content needs byte-exact
confirmation (this project's established pattern for every
filesystem-mutating stage):

1. `edit /ETC/CONFIG` (an existing, real file) — confirm the EDITOR
   window opens, raises, titled `RAVE-OS EDIT: /ETC/CONFIG`, and the
   buffer shows the real on-disk content (`FX=0` or `FX=1`).
2. Move the cursor to the middle of the loaded text (Left arrow a few
   times), type a character — confirm it inserts at that position, not
   at the end (proves mid-buffer insert, not append-only).
3. Press Enter mid-line — confirm the line visibly splits into two.
4. Press Backspace at the start of the second line (right after the
   Enter from step 3) — confirm the two lines merge back into one,
   content intact on both sides of the former split point.
5. Type several lines of differing length, then Up/Down through them —
   confirm the cursor lands on the same column each time it can, and
   clamps to end-of-line on a shorter line (both directions).
6. Click `SAVE` — confirm success (no visible error, window stays open);
   then parse `fs.img` directly to confirm `/ETC/CONFIG`'s on-disk bytes
   now match the edited buffer exactly, byte-for-byte.
7. `edit /HOME/NEWFILE` (a path that does not exist yet) — confirm the
   window opens with an empty buffer, titled for the new path. Type
   content, click `SAVE` — confirm `fs.img` now has a real
   `/HOME/NEWFILE` entry with that exact content (proves the
   doesn't-exist-yet path through the same Save logic).
8. `edit` on a path that resolves to a directory, and on a file whose
   on-disk size exceeds `EDITOR_BUF_SIZE` (if a large enough real or
   synthetic file exists to test against) — confirm both fail with one
   `edit: failed`-equivalent line in SHELL's scrollback and no window
   opens, and that the previously-open EDITOR window (if any) is left
   completely undisturbed by the failed attempt.
9. Close the EDITOR window via its titlebar X without saving, mid-edit
   — reopen the same path with `edit` again — confirm the file's
   on-disk content is exactly what it was before the discarded edit
   (proves close-without-save truly discards, no partial write).
