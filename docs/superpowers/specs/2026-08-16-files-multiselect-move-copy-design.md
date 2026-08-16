# FILES multi-select and move/copy, plus SHELL `mv`/`cp`

## Purpose

`docs/IDEAS.md` flagged "upgrade the FILES window" without deciding what
that means. This spec picks the first of two candidate directions
(multi-select + move/copy; an in-OS text editor stays on IDEAS.md for its
own future brainstorm): today FILES supports single-selection (right-click
toggles one row) and only DELETE/rename/create — there is no way to
relocate or duplicate a file at all, in FILES or in SHELL. The goal is
selecting several files at once and moving or copying them, from both
surfaces.

## Scope

**In scope**: two new filesystem primitives (`fs_move`, `fs_copy_file`);
FILES gains multi-select (right-click toggles a bitmask instead of a
single index) and CUT/COPY/PASTE buttons using a clipboard-style
select-then-navigate-then-paste flow; SHELL gains `mv <src> <dest_dir>`
and `cp <src> <dest_dir>` commands built on the same two primitives.

**Out of scope, deliberately**: directories as move/copy targets (only
files can be cut/copied/moved/`mv`/`cp`'d — a selected directory is
silently excluded, same as any other out-of-scope click this codebase
already no-ops on). Directory move/copy needs either a recursive
walk or a relocate-the-whole-subtree primitive, real added complexity
that stays a follow-up idea rather than bundled in here. Also out of
scope: overwrite-on-conflict (paste/`mv`/`cp` onto an existing name
just fails, matching `fs_create_file`'s existing write-once, no-overwrite
behavior), any visible error/status text in FILES (every failure here
stays a silent no-op, the same convention `fs_delete`/`fs_create_dir`/
`fs_rename` failures already follow in `kernel.c`), and a real disabled-
button widget state (PASTE with an empty clipboard is a no-op click, not
a visually disabled button — no such widget state exists in this
codebase yet).

## Design

### `fs.c`/`fs.h`: two new primitives

`fs_move(const char *path, const char *dest_dir)` relocates `path`'s
directory-table entry into `dest_dir`'s table — metadata only, no file
data is read, written, or moved. This matters because `fs.c`'s allocator
is a one-way bump allocator that never reclaims space (already documented
on `fs_delete`): implementing move as copy-then-delete would permanently
leak the original data's disk space on every single move. Fails (-1),
leaving both `path` and `dest_dir` untouched, if `path` doesn't name an
existing file (directories are out of scope per above), `dest_dir`
doesn't exist or isn't a directory, `dest_dir`'s table is already full,
or `dest_dir` already has an entry named the same as `path`'s leaf name
— same failure shape `fs_rename` already uses for its own name-conflict
case.

`fs_copy_file(const char *path, const char *dest_dir)` reads `path` via
the existing `fs_read_file()` and writes it to `dest_dir` via the
existing `fs_create_file()` — unlike move, copy inherently needs a
second allocation, so there's no space-leak concern to design around
here, just composing two primitives that already exist. Fails (-1) under
the union of `fs_read_file()`'s and `fs_create_file()`'s existing failure
conditions (source not found or not a file; dest parent missing, full,
out of space, or already has that name).

Both are declared in `fs.h` next to `fs_rename`, and both are files-only
by construction — `fs_move` refuses a directory `path`, `fs_copy_file`
inherits `fs_read_file`'s existing "fails if path names a directory"
behavior. Living in `fs.c` (not `kernel.c` or `shell.c`) follows the same
precedent that already pulled `fs_path_join`/`fs_path_parent` out of
FILES and into `fs.c` for SHELL to share — one implementation, two
callers, same as this spec's own FILES-and-SHELL split below.

### FILES: multi-select (`kernel.c`)

`files_selected` (currently a single `int`, `FILES_HIT_NONE` or a row
index) becomes `uint32_t files_selected_mask`. `FS_LIST_MAX` is 21, well
under 32, so a plain bitmask needs no new data structure or dynamic
sizing. Right-click on a row **toggles that row's bit** (today's
overwrite-the-single-value toggle becomes a per-bit toggle; every other
row's bit is unaffected). `DELETE`'s existing single-index deletion
becomes an iterate-every-set-bit-and-delete loop — a generalization of
current behavior, not a change to it when only one bit is ever set. The
mask resets to 0 on every `cwd` change, same as today's
`files_selected = FILES_HIT_NONE` reset on navigation, since row indices
are only meaningful against the currently-listed `file_entries[]`.

### FILES: clipboard and CUT/COPY/PASTE (`kernel.c`)

```c
struct files_clipboard {
    char source_dir[FS_PATH_MAX];
    char names[FS_LIST_MAX][FS_NAME_MAX];
    unsigned int count;
    int is_cut; /* 1 = CUT (fs_move on paste), 0 = COPY (fs_copy_file on paste) */
};
```

A separate struct from the selection mask, since the clipboard must
survive navigating away — the entire point of "select, then navigate,
then paste" is that `cwd` changes in between.

- **CUT**/**COPY** button: snapshots every currently-selected row that
  is a *file* (a selected directory's bit is silently skipped — v1 is
  files-only) into `clipboard.names[]`, records the current `cwd` as
  `source_dir`, sets `is_cut` accordingly. If the selection is empty or
  covers only directories, the button click is a no-op and the clipboard
  keeps whatever it already held.
- **PASTE** button: for each `clipboard.names[i]`, joins `source_dir` +
  name as the source path and calls `fs_move(source, cwd)` (if
  `is_cut`) or `fs_copy_file(source, cwd)` (otherwise) — `cwd` is
  wherever FILES has navigated to at the moment PASTE is clicked, which
  is what makes navigation double as destination-picking. Each name is
  independent: one failing (e.g. a name collision at the destination)
  doesn't stop the rest from being attempted. Clicking PASTE with an
  empty clipboard (`count == 0`) is a no-op.
- **Post-paste**: a successful CUT's clipboard clears (`count = 0`)
  after the paste completes — cut-once semantics, since the originals
  no longer exist at `source_dir`. A COPY's clipboard is left intact, so
  the same files can be pasted into several directories in a row,
  matching ordinary copy/paste convention.

### FILES: window layout (`kernel.c`)

`windows[WIN_KIND_FILES].w`/`.h` grow to fit a second button row (`CUT`,
`COPY`, `PASTE`) below the existing `NEW DIR`/`DELETE` row, rather than
shrinking the already-small visible file list or overloading the
existing two buttons with contextual relabeling. Exact new pixel
dimensions are an implementation-time fit (three buttons at roughly the
existing ~78px scale) rather than a design-level decision.

### SHELL: `mv`/`cp` (`shell.c`)

`shell_cmd_mv(struct shell *sh, const char *args, char *out, int out_cap)`
and `shell_cmd_cp(...)`, same shape as the existing six command handlers.
Both arguments — source file and destination directory — are split on
whitespace (same trim-and-borrow token shape `shell_first_word()`/the
`RUN` argument split already use) and each independently resolved via
`shell_resolve()` + `shell_case_correct()` (full correction on both,
`correct_last = 1` — unlike `mkdir`, both the source file and the
destination directory must already exist, there's no new name being
typed). `shell_cmd_mv()` calls `fs_move(resolved_src, resolved_dest)`;
`shell_cmd_cp()` calls `fs_copy_file(resolved_src, resolved_dest)`.
Failure prints `mv: failed` / `cp: failed`, matching `shell_cmd_rm()`'s
existing one-line `rm: failed` wording exactly. `shell_token_is()`
dispatch gains `"mv"`/`"cp"` entries alongside the existing six.
Directory scope matches FILES: files-only falls out for free, since
`fs_move`/`fs_copy_file` themselves refuse directories — no extra check
needed in `shell.c`.

## Testing

All headless, via the existing `-display none -monitor unix:...` harness
plus direct `fs.img` byte-parsing for on-disk confirmation (this
project's established pattern for every filesystem-mutating stage):

**FILES**:
1. Boot, open FILES, navigate to a directory with at least two files.
   Right-click both — confirm both rows highlight as selected
   (multi-select actually holds two bits, not just the latest).
2. Right-click one of the two again — confirm only it deselects, the
   other stays selected (proves per-bit toggle, not a shared/overwritten
   selection).
3. Click CUT. Navigate (left-click) into a different directory. Click
   PASTE — confirm both files now appear in the new listing.
4. Navigate back to the original directory, `ls`-equivalent (open the
   listing) — confirm both are gone, proving `fs_move` actually
   relocated rather than duplicated. Parse `fs.img` directly to confirm
   the entries moved parent tables rather than leaving a stale copy.
5. Repeat with COPY instead of CUT into a third directory — confirm the
   files appear at the destination *and* remain at the source (parse
   `fs.img` to confirm two on-disk copies now exist, proving `fs_copy_file`
   actually allocated new data rather than sharing the original).
6. Click PASTE a second time in a row after a COPY (clipboard should
   still be full) — confirm it succeeds again into a fourth directory.
   Click PASTE a second time in a row after a CUT (clipboard should now
   be empty) — confirm it's a no-op (nothing new appears).
7. Right-click a directory row, click CUT — confirm the clipboard does
   not pick it up (e.g. by then pasting elsewhere and confirming nothing
   new appears), proving the files-only skip.
8. Attempt to paste onto a destination that already has a file with the
   same name — confirm the silent-no-op (that one file doesn't
   overwrite; a screendump before/after shows no change to the
   conflicting row).

**SHELL**:
9. `mv` a real file between two real directories by typed path; `pwd`
   the destination and confirm the file's listed there; confirm the
   source directory's listing no longer shows it. Parse `fs.img` to
   confirm.
10. `cp` a real file between two real directories; confirm it now
    appears in both listings, and parse `fs.img` to confirm two on-disk
    copies.
11. `mv`/`cp` with a bogus source path — confirm `mv: failed` /
    `cp: failed`, not a hang or crash.
12. `mv`/`cp` a directory (not a file) as the source — confirm it fails
    the same way, proving the files-only restriction holds from SHELL
    too.
