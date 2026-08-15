# A bash-like shell for Rave-OS

## Purpose

Rave-OS currently has two ways to touch the filesystem: the mouse-driven
FILES window (click to navigate, buttons for NEW DIR/DELETE), and typing
`RUN <name>` at the Forth console to execute a Forth script from `/BIN`.
Neither is a real command shell. The goal is a third window, launched
from the start menu, where a user types Unix-style commands (`ls`,
`cd`, `cat`, ...) against the real filesystem — a bash-like console
alongside, not instead of, the existing two.

## Scope

**In scope**: a new `shell.h`/`shell.c` module with its own command
parser and a fixed v1 command set (`pwd`, `cd`, `ls`, `cat`, `mkdir`,
`rm`, `echo`), a new `WIN_KIND_SHELL` window with its own scrollback and
command history, a `SHELL` start-menu entry, and a small refactor moving
`kernel.c`'s existing `path_join()`/`path_parent()` into `fs.c` (as
`fs_path_join()`/`fs_path_parent()`) so both the FILES window and the
new shell share one implementation instead of two.

**Out of scope, deliberately**: quoting, pipes, redirection (`>`),
environment variables, multi-segment relative paths beyond a bare `..`
(e.g. `cd ../foo` isn't supported — only `..` alone, same granularity
the FILES window's own "up" row already has), tab completion, and a
`RUN`-equivalent for executing a stored shell script. All are real bash
features nothing here asks for yet; the shell.c structure doesn't
preclude adding any of them later. The shell also does not call into
Forth or vice versa — two fully separate command languages, per
[[project_os_vision]]'s "own Forth" plus this new addition being
explicitly bash-*like*, not Forth-extended.

## Design

### `fs.c` refactor: shared path helpers

`kernel.c` already has `path_join(dst, cap, cwd, name)` (appends a name
onto a cwd, handling the "/" root case without a doubled slash) and
`path_parent(cwd)` (truncates cwd at its last `/`, or resets to `/` from
a top-level directory), both used today only by the FILES window. The
shell needs the exact same two operations for `cd`/`ls`/`cat`/`mkdir`/
`rm`'s path resolution — rather than a second copy, both move into
`fs.c` as public functions, `fs_path_join()`/`fs_path_parent()`,
declared in `fs.h` with the same signatures. `kernel.c`'s FILES-window
call sites switch to the `fs_`-prefixed versions; the local static
copies are deleted.

One implementation detail changes in the move: the original
`path_join()` uses `kernel.c`'s own unbounded `str_eq()` to check
`cwd == "/"`. `fs.c` already has a *different*, FS_NAME_MAX(16)-bounded
`str_eq()` for comparing directory-entry names — reusing it here would
silently miscompare on cwds longer than 16 bytes (a real, documented
trap noted in `fs.c`'s own comment on that function). `fs_path_join()`
avoids the whole question by checking `cwd[0] == '/' && cwd[1] == 0`
directly instead of calling any `str_eq()`. `fs.c` gains its own small
private `static` bounded-append helper (mirroring `kernel.c`'s
`str_append()`) for building the joined path — same "each file owns its
small string primitives" pattern already used throughout this codebase
(`taskbar.c`, `desktop_icon.c`, `startmenu.c` each duplicate their own
tiny helpers rather than sharing a common-utils file).

`FS_PATH_MAX` (80 — room for 4 path components at up to 15 characters
each, plus separators and a terminator) moves from `kernel.c`'s
`FILES_PATH_MAX` into `fs.h` as the single source of truth, since it's
derived entirely from `fs.c`'s own `FS_MAX_PATH_DEPTH`/`FS_NAME_MAX`
constants. `kernel.c` keeps using it under its existing name via `#define
FILES_PATH_MAX FS_PATH_MAX` — no call site needs to change.

### `shell.h`/`shell.c`: the command language

New module, deliberately *not* held to the same isolation `forth.c`
enforces on itself (`forth.c`'s whole point is staying generic and
filesystem-free; the shell's whole point is filesystem interaction, so
depending directly on `fs.h` is correct here, not a layering violation).

```c
struct shell {
    char cwd[FS_PATH_MAX];
};

void shell_init(struct shell *sh);              /* cwd = "/HOME" */
void shell_eval_line(struct shell *sh, const char *line, char *out, int out_cap);
```

Same shape as `forth_eval_line()`: one call per typed line, writing
`\n`-separated output into a caller-owned buffer — `kernel.c`'s existing
`append_split_lines()` renders it into the window's console output the
same way it already does for Forth and RUN. No new rendering code
needed.

**Parsing**: a line splits into a command token (up to the first
whitespace run) and a trimmed argument remainder (everything after,
borrowed not copied) — the exact same trim-and-borrow shape
`match_run_command()` already uses for `RUN <arg>`. Unlike `RUN` and
Forth's word lookup (both case-insensitive), shell commands are
lowercase and case-sensitive, matching real shell convention. An empty
line is a silent no-op, same as pressing Enter at an empty bash prompt.

**Path resolution** (shared by `cd`/`ls`/`cat`/`mkdir`/`rm`'s argument):
a leading `/` is used as-is (absolute); the bare token `..` copies `cwd`
and calls `fs_path_parent()` on it; anything else calls
`fs_path_join(cwd, arg)`. This is deliberately the same two cases the
FILES window already supports via mouse clicks (a directory row, or the
".." row) — the shell reaches the identical set of destinations through
typing instead of clicking, not a superset.

**Commands**:

- `pwd` — writes `cwd`.
- `cd [path]` — no argument resets to `/HOME` (matches bash's own
  bare-`cd`-goes-home behavior, and `/HOME` is already this OS's real
  home directory). Otherwise resolves the argument and, unlike the
  FILES window's own click-to-navigate (which commits to the new cwd
  unconditionally and only shows an empty listing on failure), validates
  first: calls `fs_list_dir()` on the resolved path and only commits
  `cwd` if it succeeds. On failure: `cd: no such directory`. This is a
  real, deliberate improvement over FILES' current silent-fail
  navigation, needed here because a shell's whole value is textual
  feedback, not because FILES is being changed too (out of scope).
- `ls [path]` — defaults to `cwd` if no argument. `fs_list_dir()`s the
  resolved path; on failure, `ls: no such directory`. Otherwise one line
  per entry, `NAME/` for a directory or `NAME SIZEB` for a file — the
  exact format `draw_files_group()` already uses for FILES' own rows,
  just as text instead of pixels. An empty directory prints nothing
  (real `ls` behavior), not FILES' `(EMPTY)` placeholder (a GUI
  affordance, not needed in text output).
- `cat <path>` — resolves the argument, `fs_read_file()`s it into a
  buffer, and writes the raw content straight into `out` (so any `\n`
  bytes in the file split into separate console lines for free, via the
  same `append_split_lines()` pass RUN and Forth already go through). On
  failure (not found, not a file, or too big for the buffer — `fs_read_file()`
  doesn't distinguish these), one generic `cat: read failed`, matching
  RUN's own single generic `(RUN FAILED)` message for the same
  underlying failure modes.
- `mkdir <path>` — `fs_create_dir()`s the resolved path. Failure (already
  exists, parent full, too deep, or the reserved name `DEV` at root):
  `mkdir: failed`.
- `rm <path>` — `fs_delete()`s the resolved path. Failure (not found, or
  a non-empty directory — `fs_delete()` still has no recursive delete,
  same real limitation FILES' own DELETE button already lives with):
  `rm: failed`.
- `echo <text>` — writes the trimmed remainder verbatim. An empty
  remainder writes an empty line.
- Anything else — `<token>: command not found`, matching bash's own
  wording.

### Window integration (`kernel.c`)

`WIN_KIND_SHELL` (`MAX_WINDOWS` 2 → 3), closed at boot like FORTH/FILES
today. Owns `struct shell sh`, its own `console_output`/`console_input`/
`console_history` triple — a fully separate scrollback and up/down
recall history from the Forth console, same reasoning FORTH and the
FILES window's `name_input` already don't share state. `draw_shell_group()`
mirrors `draw_forth_group()` exactly (`window_draw()` +
`console_output_draw()` + `console_input_draw()`). `move_window_content()`
gains a `WIN_KIND_SHELL` branch moving the shell's own console
output/input, same pattern as FORTH's branch. `touched[WIN_KIND_SHELL]`
folds in the shell console/input's own generation/len/cursor/focus
fields, mirroring `touched[WIN_KIND_FORTH]`'s existing computation
exactly. The Enter-key handler gains a parallel `shell_ci.focused`
branch calling `shell_eval_line()` instead of `forth_eval_line()`, with
its own history push via `shell_hist` — no `RUN`-style interception, the
shell has nothing analogous to intercept in v1. Its output buffer is
`char shell_out[VIEWER_BUF_SIZE]` (512 bytes, the same constant RUN
already uses for its own file-read buffer) rather than Forth's smaller
128-byte `out` — sized for `cat`ing a small file or `ls`ing a full
21-entry directory without truncating in the common case; a
pathologically large result still truncates safely rather than
overflowing, the same `str_append()` guarantee every other bounded
buffer in this file already relies on.

A new `shell_is_topmost` boolean joins `forth_is_topmost`/
`files_is_topmost` in the per-packet hover/focus block, gating
`shell_ci`'s hover/focus exactly the way the other two windows' input
widgets are already gated.

### Start menu

`STARTMENU_ITEM_SHELL` joins the item list (`STARTMENU_ITEM_COUNT` 6 →
7), positioned right after `FILES` — the menu already groups "launch a
window" items (`FORTH`, `FILES`) before "navigate FILES somewhere"
items (`CONFIG`, `GAMES`) before system actions (`FX`, `EXIT`); `SHELL`
is a window launcher, so it belongs in the first group: `FORTH`,
`FILES`, `SHELL`, `CONFIG`, `GAMES`, `FX`, `EXIT`. Its click handler is
the same open+raise pattern `FORTH`/`FILES` already use.

## Testing

Unlike FILES' mouse-driven navigation or Forth's shift-key-symbol gaps,
every v1 command uses only plain lowercase letters and digits — fully
within the confirmed-working headless `sendkey` range, no live-only
blockers expected anywhere in this stage.

Plan, all via the existing `-display none -monitor unix:...` harness:

1. Open SHELL from the start menu (mouse) — confirm the window opens
   and raises, empty scrollback.
2. Type `pwd`, Enter — expect `/HOME`.
3. Type `cd /ETC`, Enter, then `pwd` — expect `/ETC`, confirming
   absolute-path navigation and that `cd` actually commits.
4. Type `ls` — expect `CONFIG` to appear (the real file
   `fx_default_from_config()` seeds), confirming `fs_list_dir()`
   integration and the `NAME SIZEB` formatting for a file.
5. Type `cat CONFIG` — expect `FX=0` (or `FX=1`, whatever the current
   on-disk value is), confirming real file content renders, not a
   placeholder.
6. Type `cd ..`, `pwd` — expect `/`, confirming the bare-`..` case.
7. Type `mkdir SHELLTEST`, `ls` — expect `SHELLTEST/` to appear; `cd
   SHELLTEST`, `pwd` — expect the new absolute path; `cd ..`, `rm
   SHELLTEST`, `ls` — expect it gone. Round-trips `fs_create_dir()`/
   `fs_delete()` through the shell end to end, then cleans up after
   itself (unlike the standard system folders, this is throwaway test
   state with no reason to survive the test).
8. Type `echo hello rave-os` — expect `hello rave-os` verbatim.
9. Type `bogus`, Enter — expect `bogus: command not found`, confirming
   the error path (not a hang or a crash).
10. Confirm the FORTH window's own history (`console_history`) is
    unaffected by anything typed in SHELL — separate scrollback, no
    cross-window state bleed.

Also confirm, on the real persistent `fs.img` (not a scratch copy, same
as every other stage that touches the standard system folders): after
step 7's `mkdir`/`rm` round-trip, the on-disk root table has no
leftover `SHELLTEST` entry — parsed directly the same way `/ETC/CONFIG`
was confirmed earlier today, stronger evidence than a screendump alone.
