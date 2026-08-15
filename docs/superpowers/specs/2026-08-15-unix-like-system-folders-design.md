# Unix-like system folders for the file manager

## Purpose

Rave-OS's file manager currently only ever shows whatever a user (or a
test session) happened to create by hand — the persistent `fs.img` root
is a mix of ad-hoc test directories (`TESTDIR`, `OLD DIR`, ...) and
nothing else. The goal is for root to look like a real Unix-like system
root instead: a fixed set of standard top-level directories that exist
from boot, without requiring a fresh reformat of the disk.

## Scope

Two tiers, chosen because this kernel currently has real, live state to
expose for exactly one of these folders (the two fixed ATA drives) and
nothing for the rest (no process loader, no config subsystem, no
logging):

- **Plain real directories**: `/BIN /ETC /HOME /USR /VAR /TMP`. Created
  via the existing `fs_create_dir()`, genuinely persisted, genuinely
  empty — the same "real gap, not solved before it has to be" deferral
  already used elsewhere in this codebase (e.g. `fs_delete()`'s
  never-reclaims-sectors gap).
- **`/DEV`, synthetic**: never written to disk at all. Listing it
  probes `kernel/ata.c`'s two fixed drives live and synthesizes
  entries, so it reflects real hardware presence rather than a stale
  snapshot.

Explicitly out of scope: cleaning up existing test clutter already
sitting in the persistent `fs.img` root (`TESTDIR`, `OLD DIR`, etc.) —
unrelated to this feature, left alone. Also out of scope: any real
content *inside* `/BIN`, `/ETC`, `/USR`, `/VAR` (no subsystem exists
yet to generate any), and any device other than the two ATA drives
(nothing else in this kernel is exposed as a discrete device).

## Design

### Bootstrap: standard directories

New `fs_bootstrap_dirs()` in `fs.c`/`fs.h`. Loops `fs_create_dir()` over
a fixed list (`/BIN`, `/ETC`, `/HOME`, `/USR`, `/VAR`, `/TMP`), ignoring
the `-1` "already exists" return — the same idempotent shape
`fs_selftest()` already relies on for `/TESTDIR`, so it's safe to call
every boot, not just on a fresh format. `/DEV` is deliberately *not* in
this list — see below.

Called once from `kmain()`, right after the existing `fs_status =
fs_selftest();` line, before the first `fs_list_dir(cwd, ...)` call —
so the standard directories (and the synthetic `/DEV` row) are present
in the very first rendered frame, not just after some other action
triggers a re-list.

If a standard directory's `fs_create_dir()` call fails for a reason
other than "already exists" (e.g. root's table is full — unlikely at 6
new entries against a 20-slot capacity, but possible if enough test
clutter has accumulated), that single directory is silently skipped.
This is boot-time internal plumbing, not a user action, so there's
nothing better to do with the failure and no new error-reporting UI is
warranted for it.

### `/DEV`: synthetic, not a real directory

`/DEV` is never created via `fs_create_dir()` and never gets a real
on-disk directory-table entry. Two places in `fs.c` change:

**`fs_list_dir("/DEV")`** is special-cased at the top of the function,
before the normal `resolve_dir_lba()` path: it skips real dirtable
resolution entirely and synthesizes entries instead —

- `HDA` is always listed. This kernel boots from the ATA master drive,
  so by construction it's present whenever the kernel is running at
  all; no probe is needed or meaningful.
- `HDB` is listed only if a live read of the filesystem superblock
  sector (`ata_read_sector(ATA_DRIVE_SLAVE, FS_SUPERBLOCK_LBA, ...)`)
  succeeds *right now*. This reuses the exact check `fs_init()` already
  trusts to decide whether the filesystem disk is present and
  readable, rather than inventing new ATA-protocol-level per-drive
  presence detection (risky to get right without real hardware to
  verify against, and this kernel's ATA layer has never been tested
  against anything but QEMU's always-present backing files).

Both are listed as `FS_TYPE_FILE`, with `size_bytes` left at `0`
(this kernel has no runtime-available notion of either drive's true
size — `disk.img`'s size is a `boot/Makefile`-computed build artifact,
not something `ata.c`/`fs.c` knows — so `0` avoids implying false
precision). Because they're type `FILE`, clicking one in the file
manager already routes through the Stage C viewer's existing
`fs_read_file()` call, which already handles a failed read cleanly
(`"(READ FAILED)"`) — since nothing backs `/DEV/HDA` as a real
filesystem path, that's exactly what happens. No `kernel.c` changes
are needed for this to work correctly.

**`fs_list_dir("/")`** gains one addition: after the existing loop over
real root entries, append one synthetic `DEV` entry (type `FS_TYPE_DIR`,
`size_bytes` 0) — unless a real entry already happens to be named
exactly `DEV` (checked during the same loop, `str_eq(entries[i].name,
"DEV")`, matching the file's existing case-sensitive comparison
convention). This is defensive rather than expected to ever trigger
given the write-guards below, but it's a single cheap string comparison
already being done in the loop anyway.

### Write-protection for the reserved name `DEV`

Three small guards, all case-sensitive exact-match against `"DEV"`
(matching `fs.c`'s existing case-sensitive convention throughout — this
does mean a user could still create a real directory named `dev`
lowercase, which would visually collide once the font force-uppercases
it for display; deliberately not solved here, narrow and consistent
with the file's existing case handling rather than introducing a new
comparison style for just this one guard):

- `fs_create_dir()` / `fs_create_file()`: after `walk_to_parent()`
  succeeds, refuse (`-1`) if the resolved parent is root and the leaf
  name is exactly `"DEV"`.
- `fs_rename()`: refuse (`-1`) if the resolved parent is root and
  `new_name` is exactly `"DEV"` (renaming some other real entry to
  shadow the synthetic one). The existing `dirtable_find(entries,
  new_name)` collision check doesn't catch this, since there's no real
  `DEV` entry to collide with.

Nothing else needs a guard:

- `fs_create_dir("/DEV/FOO")` (or `fs_create_file`) already fails for
  free — `walk_to_parent()` calls `resolve_dir_lba("/DEV")` internally,
  which fails to find a real `DEV` entry in root's table and returns
  `-1`, exactly the same as trying to create something inside any other
  nonexistent directory.
- `fs_delete("/DEV")` already fails for free — `dirtable_find()` never
  finds it, since it's never actually written to root's table. Same
  "not found" path as deleting anything else that doesn't exist.

## Testing

Unlike the two most recent Forth stages, this needs no shift-modified
symbol keys at all (`BIN`, `ETC`, `HOME`, `USR`, `VAR`, `TMP`, `DEV`,
`HDA`, `HDB` are all plain letters) — headless `sendkey` should fully
cover verification, no live-only blocker expected.

Plan:
1. Boot, screendump root listing — expect `BIN/ ETC/ HOME/ USR/ VAR/
   TMP/ DEV/` alongside whatever existing test clutter is already in
   the persistent `fs.img` (left untouched, per scope).
2. Navigate into `DEV` — expect `HDA` and `HDB` both listed (QEMU backs
   both drives with real files in this project's `disk.img`/`fs.img`
   setup).
3. Click `HDA` (or `HDB`) — expect the viewer to raise showing
   `(READ FAILED)`, not a crash or hang.
4. Attempt to create a directory named `DEV` at root via the NEW DIR
   flow — expect a silent no-op (existing `DEV/` row unchanged, typed
   text stays in the field, same convention as every other FILES-window
   failure).
5. Attempt to rename an existing real entry to `DEV` — expect the same
   silent no-op.
6. Re-list root a second time (e.g. navigate away and back) — confirm
   only one `DEV/` row ever appears, not a duplicate.

Also confirm (by reading the code, not a dedicated screendump — same
"code review covers the untestable-headlessly case" precedent Stage D
set for its non-empty-directory delete refusal): a build with a
genuinely full root directory table skips the standard-directory
bootstrap for whichever entries don't fit, without crashing or
corrupting the table.
