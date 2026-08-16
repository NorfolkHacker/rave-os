# FILES Multi-Select and Move/Copy, plus SHELL mv/cp Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `fs_move`/`fs_copy_file` filesystem primitives, wire them into a mouse-driven CUT/COPY/PASTE flow with multi-select in the FILES window, and into `mv`/`cp` commands in SHELL.

**Architecture:** Two new metadata-aware primitives in `fs.c` (a real relocate-only move, and a sector-streaming copy) shared by both callers. FILES gets a `uint32_t` selection bitmask (replacing its current single-index selection) and a small clipboard struct that survives navigation. SHELL gets two new command handlers reusing its existing resolve/case-correct/dispatch machinery. Both surfaces are files-only in v1 — directories are explicitly out of scope.

**Tech Stack:** Freestanding C (`-m32 -ffreestanding -nostdlib`), x86 kernel, no libc, no malloc. This codebase has **no unit-test framework** — it cannot have one in the traditional sense (no host process to run tests in; `fs.c` talks directly to ATA I/O ports). Every prior stage in `docs/BUILD_LOG.md` verifies behavior by booting the real kernel headlessly in QEMU (`-display none -monitor unix:/tmp/qemu-mon.sock,server,nowait`), driving it via monitor commands (`sendkey`, `mouse_move`, `mouse_button`) sent one at a time over the socket (via `socat` — batching multiple commands in one write is a documented false-alarm source), taking `screendump`s (PPM, converted to PNG via `python-pillow` to inspect), and where the filesystem itself is under test, parsing `fs.img`'s on-disk bytes directly with `python3`. This plan's "test" steps follow that same convention — there is no `pytest`-equivalent to substitute.

**Spec:** `docs/superpowers/specs/2026-08-16-files-multiselect-move-copy-design.md`

## Global Constraints

- Files-only for v1: `fs_move`/`fs_copy_file` both refuse a `path` that names a directory. Neither FILES nor SHELL needs to add its own extra check for this — it falls out of the primitives themselves.
- No overwrite-on-conflict anywhere: every mutation here fails (returns -1 / prints `<cmd>: failed`) if the destination name is already taken, matching `fs_create_file`'s existing write-once behavior.
- No visible error/status text in FILES: every failure in the FILES window (paste conflict, empty clipboard, directory in selection) is a silent no-op, matching `fs_delete`/`fs_create_dir`/`fs_rename`'s existing silent-failure convention in `kernel.c`.
- All builds happen inside the `forth-os` distrobox container (`distrobox enter forth-os -- bash -lc '...'`) — the SteamOS host has no toolchain. Build with `cd /home/deck/os/kernel && make` (compiles `kernel.bin`) or `cd /home/deck/os/boot && make` (rebuilds `disk.img` including the kernel).
- On-disk directory-table layout, needed for every `fs.img`-parsing verification step: `struct fs_entry { char name[16]; uint32_t start_lba; uint32_t size_bytes; uint8_t type; } __attribute__((packed))` — 25 bytes per entry, `FS_MAX_FILES` (20) entries per table, one table per 512-byte sector (`ATA_SECTOR_SIZE`), root's table at LBA 1. `fs.img`'s second drive image is what FILES/SHELL both read/write (`ATA_DRIVE_SLAVE`).

---

### Task 1: `fs_move` filesystem primitive

**Files:**
- Modify: `kernel/fs.h` (insert after `fs_rename`'s declaration, line 99)
- Modify: `kernel/fs.c` (insert after `fs_rename`'s closing brace, line 720)

**Interfaces:**
- Consumes: `fs.c`'s existing private helpers `walk_to_parent(path, leaf, table_lba)`, `resolve_dir_lba(dir_path, table_lba)`, `dirtable_read/write(lba, entries)`, `dirtable_find(entries, name)`, `dirtable_find_free(entries)`, and the `struct fs_entry`/`mounted` state already defined earlier in the file.
- Produces: `int fs_move(const char *path, const char *dest_dir)` — 0 on success, -1 on any failure (source missing/not-a-file, dest missing/not-a-directory/full/name-taken). Used by Task 3 (`shell_cmd_mv`) and Task 5 (FILES PASTE handler).

- [ ] **Step 1: Add the declaration to `fs.h`**

Insert immediately after line 99 (`int fs_rename(const char *path, const char *new_name);`):

```c
/* Relocates path (must name an existing file -- directories are out of
 * scope for v1) into dest_dir, a metadata-only operation: no file data
 * is read, written, or reallocated, only the directory-table entry moves
 * from its current parent's table into dest_dir's. This matters because
 * the allocator never reclaims sectors -- implementing move as
 * copy-then-delete would permanently leak the original data's space on
 * every call. Fails (-1), leaving both path and dest_dir untouched, if
 * path doesn't exist or isn't a file, dest_dir doesn't exist or isn't a
 * directory, dest_dir's table is full, or dest_dir already has an entry
 * named the same as path's leaf name (this also covers "moving" a file
 * into the directory it's already in -- a guaranteed name collision with
 * itself, so it correctly fails without needing a special case). Returns
 * 0 on success. */
int fs_move(const char *path, const char *dest_dir);
```

- [ ] **Step 2: Implement it in `fs.c`**

Insert immediately after line 720 (`fs_rename`'s closing `}`), before `fs_list_dir`'s doc comment:

```c
int fs_move(const char *path, const char *dest_dir) {
    struct fs_entry src_entries[FS_MAX_FILES];
    struct fs_entry dest_entries[FS_MAX_FILES];
    struct fs_entry moved;
    char leaf[FS_NAME_MAX];
    unsigned int src_table_lba;
    unsigned int dest_table_lba;
    int src_slot;
    int dest_slot;
    int i;

    if (!mounted) {
        fs_init();
    }

    if (walk_to_parent(path, leaf, &src_table_lba) != 0) {
        return -1;
    }
    if (dirtable_read(src_table_lba, src_entries) != 0) {
        return -1;
    }
    src_slot = dirtable_find(src_entries, leaf);
    if (src_slot < 0 || src_entries[src_slot].type != FS_TYPE_FILE) {
        return -1; /* not found, or a directory (out of scope for v1) */
    }

    if (resolve_dir_lba(dest_dir, &dest_table_lba) != 0) {
        return -1;
    }
    if (dirtable_read(dest_table_lba, dest_entries) != 0) {
        return -1;
    }
    if (dirtable_find(dest_entries, leaf) >= 0) {
        return -1; /* name already taken at destination (self-move included) */
    }
    dest_slot = dirtable_find_free(dest_entries);
    if (dest_slot < 0) {
        return -1; /* destination table full */
    }

    moved = src_entries[src_slot];
    dest_entries[dest_slot] = moved;
    if (dirtable_write(dest_table_lba, dest_entries) != 0) {
        return -1;
    }

    for (i = 0; i < FS_NAME_MAX; i++) {
        src_entries[src_slot].name[i] = 0;
    }
    src_entries[src_slot].start_lba = 0;
    src_entries[src_slot].size_bytes = 0;
    src_entries[src_slot].type = 0;
    return dirtable_write(src_table_lba, src_entries);
}
```

- [ ] **Step 3: Build cleanly**

Run (inside the `forth-os` container):
```bash
distrobox enter forth-os -- bash -lc 'cd /home/deck/os/kernel && make'
```
Expected: `kernel.bin` builds with no warnings from `fs.c` (this project builds with `-Wall -Wextra`). `fs_move` has no caller yet at this point in the plan, so this compile-clean check is this task's whole testable deliverable — Task 3 exercises its actual behavior once SHELL's `mv` can call it.

- [ ] **Step 4: Commit**

```bash
cd /home/deck/os
git add kernel/fs.h kernel/fs.c
git commit -m "$(cat <<'EOF'
kernel: add fs_move, a metadata-only file relocation primitive

Relocates a file's directory-table entry between parents without
touching its data -- the allocator never reclaims sectors, so a
copy-then-delete implementation would leak the original's space on
every call. No caller yet; SHELL's mv (next) and FILES' PASTE
(later) both build on this.
EOF
)"
```

---

### Task 2: `fs_copy_file` filesystem primitive

**Files:**
- Modify: `kernel/fs.h` (insert after `fs_move`'s declaration from Task 1)
- Modify: `kernel/fs.c` (insert after `fs_move`'s closing brace from Task 1)

**Interfaces:**
- Consumes: same `fs.c` private helpers as Task 1, plus `alloc_sectors(count)`, `lba_span_valid(lba, span)`, `name_copy(dst, src)`, and `ata_read_sector`/`ata_write_sector` (`kernel/ata.h`) — all already used the same way inside `fs_create_file`/`fs_read_file`.
- Produces: `int fs_copy_file(const char *path, const char *dest_dir)` — same 0/-1 contract as `fs_move`. Used by Task 3 (`shell_cmd_cp`) and Task 5 (FILES PASTE handler).

- [ ] **Step 1: Add the declaration to `fs.h`**

Insert immediately after `fs_move`'s declaration (added in Task 1):

```c
/* Duplicates the file at path into dest_dir under the same leaf name --
 * unlike fs_move(), a real second copy of the data is allocated and
 * written (there's no way around that for a genuine copy). Fails (-1)
 * under the same conditions as fs_move() (path must be an existing file;
 * dest_dir must exist, be a directory, have table space, and not already
 * have an entry with that name), plus running out of disk space for the
 * new copy. Returns 0 on success. */
int fs_copy_file(const char *path, const char *dest_dir);
```

- [ ] **Step 2: Implement it in `fs.c`**

Insert immediately after `fs_move`'s closing `}` (added in Task 1):

```c
int fs_copy_file(const char *path, const char *dest_dir) {
    struct fs_entry src_entries[FS_MAX_FILES];
    struct fs_entry dest_entries[FS_MAX_FILES];
    unsigned char sector_buf[ATA_SECTOR_SIZE];
    char leaf[FS_NAME_MAX];
    unsigned int src_table_lba;
    unsigned int dest_table_lba;
    unsigned int sectors;
    unsigned int new_lba;
    unsigned int s;
    int src_slot;
    int dest_slot;

    if (!mounted) {
        fs_init();
    }

    if (walk_to_parent(path, leaf, &src_table_lba) != 0) {
        return -1;
    }
    if (dirtable_read(src_table_lba, src_entries) != 0) {
        return -1;
    }
    src_slot = dirtable_find(src_entries, leaf);
    if (src_slot < 0 || src_entries[src_slot].type != FS_TYPE_FILE) {
        return -1; /* not found, or a directory (out of scope for v1) */
    }

    if (resolve_dir_lba(dest_dir, &dest_table_lba) != 0) {
        return -1;
    }
    if (dirtable_read(dest_table_lba, dest_entries) != 0) {
        return -1;
    }
    if (dirtable_find(dest_entries, leaf) >= 0) {
        return -1; /* name already taken at destination */
    }
    dest_slot = dirtable_find_free(dest_entries);
    if (dest_slot < 0) {
        return -1; /* destination table full */
    }

    sectors = src_entries[src_slot].size_bytes / ATA_SECTOR_SIZE +
              (src_entries[src_slot].size_bytes % ATA_SECTOR_SIZE != 0 ? 1 : 0);
    if (sectors == 0) {
        sectors = 1;
    }
    if (!lba_span_valid(src_entries[src_slot].start_lba, sectors)) {
        return -1;
    }

    new_lba = alloc_sectors(sectors);
    if (new_lba == 0xFFFFFFFFu) {
        return -1;
    }

    for (s = 0; s < sectors; s++) {
        if (ata_read_sector(ATA_DRIVE_SLAVE, src_entries[src_slot].start_lba + s, sector_buf) != 0) {
            return -1;
        }
        if (ata_write_sector(ATA_DRIVE_SLAVE, new_lba + s, sector_buf) != 0) {
            return -1;
        }
    }

    name_copy(dest_entries[dest_slot].name, leaf);
    dest_entries[dest_slot].start_lba = new_lba;
    dest_entries[dest_slot].size_bytes = src_entries[src_slot].size_bytes;
    dest_entries[dest_slot].type = FS_TYPE_FILE;
    return dirtable_write(dest_table_lba, dest_entries);
}
```

Note: this copies sector-by-sector directly via `ata_read_sector`/`ata_write_sector` rather than round-tripping through `fs_read_file()`/`fs_create_file()`'s whole-buffer signatures — this kernel has no `malloc` and a file can be far larger than a safe kernel stack buffer, so streaming one `ATA_SECTOR_SIZE` (512-byte) sector at a time (the same technique `fs_read_file`/`fs_create_file` already use internally) is the only memory-safe option. This is a refinement of the spec's "reads via fs_read_file(), writes via fs_create_file()" wording — same behavior and same two composed responsibilities, implemented at the sector-streaming level those two functions themselves already use rather than through their public whole-buffer signatures.

- [ ] **Step 3: Build cleanly**

```bash
distrobox enter forth-os -- bash -lc 'cd /home/deck/os/kernel && make'
```
Expected: no warnings. Same "no caller yet" reasoning as Task 1 — behavioral verification comes in Task 3.

- [ ] **Step 4: Commit**

```bash
cd /home/deck/os
git add kernel/fs.h kernel/fs.c
git commit -m "$(cat <<'EOF'
kernel: add fs_copy_file, a sector-streaming file duplication primitive

Unlike fs_move, a copy genuinely needs a second data allocation --
streams one sector at a time via the same primitives
fs_read_file/fs_create_file already use internally, avoiding a
whole-file buffer this freestanding kernel has no malloc to size
safely. No caller yet; SHELL's cp (next) and FILES' PASTE (later)
both build on this.
EOF
)"
```

---

### Task 3: SHELL `mv`/`cp` commands

**Files:**
- Modify: `kernel/shell.h` (update the command-list doc comment on `shell_eval_line`)
- Modify: `kernel/shell.c` (add `shell_two_args`, `shell_cmd_mv`, `shell_cmd_cp`, and their dispatch entries)

**Interfaces:**
- Consumes: `fs_move`/`fs_copy_file` (Tasks 1-2); `shell.c`'s existing `shell_resolve`, `shell_case_correct`, `shell_append`, `shell_arg`, `shell_token_is`.
- Produces: two new commands reachable from `shell_eval_line`. No other task depends on new symbols from this one — this task's own verification is also the first real behavioral proof of Tasks 1-2.

- [ ] **Step 1: Add the two-argument splitter to `shell.c`**

Insert after `shell_cmd_rm` (after its closing `}`, currently ending at line 423), before `shell_init`:

```c
/* Splits arg (everything after the command word) into its first
 * whitespace-delimited token (copied, bounded, into first) and
 * everything after that token, trimmed of leading spaces (borrowed, not
 * copied -- same shape shell_arg() already uses for the single-argument
 * commands). mv/cp are the only two commands needing a source and a
 * destination rather than one path. */
static void shell_two_args(const char *arg, char *first, int first_cap, const char **second) {
    int i = 0, pos = 0;
    while (arg[i] && arg[i] != ' ' && pos < first_cap - 1) {
        first[pos++] = arg[i++];
    }
    first[pos] = 0;
    while (arg[i] == ' ') {
        i++;
    }
    *second = &arg[i];
}

/* Both the source file and the destination directory must already exist
 * -- unlike mkdir's own new name, mv doesn't invent anything, so both
 * arguments get full case correction (correct_last = 1). fs_move() is
 * itself files-only (directories out of scope for v1), so that
 * restriction applies here for free, no extra check needed. */
static void shell_cmd_mv(struct shell *sh, const char *arg, char *out, int *pos, int cap) {
    char src_arg[FS_PATH_MAX];
    const char *dest_arg;
    char src[FS_PATH_MAX];
    char dest[FS_PATH_MAX];

    shell_two_args(arg, src_arg, (int)sizeof(src_arg), &dest_arg);
    if (src_arg[0] == 0 || dest_arg[0] == 0) {
        shell_append(out, pos, cap, "mv: failed");
        return;
    }
    shell_resolve(sh, src_arg, src, (int)sizeof(src));
    shell_case_correct(src, (int)sizeof(src), 1);
    shell_resolve(sh, dest_arg, dest, (int)sizeof(dest));
    shell_case_correct(dest, (int)sizeof(dest), 1);
    if (fs_move(src, dest) != 0) {
        shell_append(out, pos, cap, "mv: failed");
    }
}

/* Same argument handling as mv -- see its comment. */
static void shell_cmd_cp(struct shell *sh, const char *arg, char *out, int *pos, int cap) {
    char src_arg[FS_PATH_MAX];
    const char *dest_arg;
    char src[FS_PATH_MAX];
    char dest[FS_PATH_MAX];

    shell_two_args(arg, src_arg, (int)sizeof(src_arg), &dest_arg);
    if (src_arg[0] == 0 || dest_arg[0] == 0) {
        shell_append(out, pos, cap, "cp: failed");
        return;
    }
    shell_resolve(sh, src_arg, src, (int)sizeof(src));
    shell_case_correct(src, (int)sizeof(src), 1);
    shell_resolve(sh, dest_arg, dest, (int)sizeof(dest));
    shell_case_correct(dest, (int)sizeof(dest), 1);
    if (fs_copy_file(src, dest) != 0) {
        shell_append(out, pos, cap, "cp: failed");
    }
}
```

- [ ] **Step 2: Wire dispatch in `shell_eval_line`**

In `shell_eval_line`, insert two new `else if` branches after the `"rm"` branch and before the `"echo"` branch:

```c
        } else if (shell_token_is(line, "rm")) {
            shell_cmd_rm(sh, shell_arg(line, "rm"), out, &pos, out_cap);
        } else if (shell_token_is(line, "mv")) {
            shell_cmd_mv(sh, shell_arg(line, "mv"), out, &pos, out_cap);
        } else if (shell_token_is(line, "cp")) {
            shell_cmd_cp(sh, shell_arg(line, "cp"), out, &pos, out_cap);
        } else if (shell_token_is(line, "echo")) {
```

- [ ] **Step 3: Update `shell.h`'s doc comment**

In `shell_eval_line`'s doc comment, change:
```
 * mkdir <path>, rm <path>, echo <text> -- the command word itself is
```
to:
```
 * mkdir <path>, rm <path>, mv <src> <dest_dir>, cp <src> <dest_dir>,
 * echo <text> -- both mv and cp take a bare destination *directory*, not
 * a full destination path (there's no rename-during-move here); the
 * command word itself is
```

- [ ] **Step 4: Build**

```bash
distrobox enter forth-os -- bash -lc 'cd /home/deck/os/boot && make'
```
Expected: clean build, `disk.img` produced.

- [ ] **Step 5: Headless verification**

Boot headlessly and drive via the monitor socket, one command per round-trip (per this project's own documented lesson — batching several commands in one `socat` write is unreliable):

```bash
distrobox enter forth-os -- bash -lc '
cd /home/deck/os/boot
cp fs.img /tmp/rave_scratch_fs.img   # never test mv/cp against the persistent fs.img
qemu-system-i386 -accel kvm -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 \
  -drive file=/tmp/rave_scratch_fs.img,format=raw,if=ide,bus=0,unit=1 \
  -display none -monitor unix:/tmp/rave_qemu.sock,server,nowait -no-reboot &
sleep 1
'
```

Then, one at a time (`echo "<cmd>" | socat - unix-connect:/tmp/rave_qemu.sock`):
1. Open the start menu, click `SHELL` (raise the window via its taskbar tab if it opens behind another).
2. Type `mkdir /ETC/MVTEST1`, Enter — sets up an empty destination directory (mv's destination arg must already exist).
3. Type `mkdir /ETC/MVTEST2`, Enter — a second destination for cp.
4. Type `echo hello > nothing` — actually this shell has no redirection; instead type `cat CONFIG` first to confirm `/ETC/CONFIG` exists (it does, from `fx_default_from_config()`), then use it as the file under test.
5. Type `mv /ETC/CONFIG /ETC/MVTEST1`, Enter.
6. Type `ls /ETC`, Enter — expect `CONFIG` gone from `/ETC`'s own listing (only `MVTEST1/` and `MVTEST2/` remain alongside whatever else was there).
7. Type `ls /ETC/MVTEST1`, Enter — expect `CONFIG 5B` now listed there (5 bytes, the real `FX=0`/`FX=1` content).
8. Type `cat /ETC/MVTEST1/CONFIG`, Enter — expect the real content (`FX=0` or `FX=1`), proving the data moved intact, not truncated.
9. Type `cp /ETC/MVTEST1/CONFIG /ETC/MVTEST2`, Enter.
10. Type `ls /ETC/MVTEST1`, Enter — expect `CONFIG` still present (copy doesn't remove the source).
11. Type `ls /ETC/MVTEST2`, Enter — expect `CONFIG 5B` now also present there.
12. Type `mv /ETC/BOGUS /ETC/MVTEST1`, Enter — expect `mv: failed` (nonexistent source).
13. Type `mv /HOME /ETC/MVTEST1`, Enter — expect `mv: failed` (source is a directory, out of scope).
14. Screendump after each step to confirm the scrollback text matches.

Then confirm on disk directly (this is stronger evidence than a screendump alone, matching this project's established practice):

```python
import struct
with open('/tmp/rave_scratch_fs.img', 'rb') as f:
    data = f.read()

def read_table(lba):
    off = lba * 512
    entries = []
    for i in range(20):
        e = data[off + i*25 : off + i*25 + 25]
        name = e[0:16].split(b'\x00')[0].decode()
        start_lba, size_bytes = struct.unpack('<II', e[16:24])
        etype = e[24]
        if name:
            entries.append((name, start_lba, size_bytes, etype))
    return entries

print("root:", read_table(1))
# Confirms ETC's own table lba, then walk into it and MVTEST1/MVTEST2 the same way.
```

Expected: `CONFIG`'s entry no longer appears in `/ETC`'s own table; it appears in `MVTEST1`'s table with the *same* `start_lba` it originally had (proving `fs_move` really did relocate metadata only, not reallocate data) — and a *different* `start_lba` in `MVTEST2`'s table for the copy (proving `fs_copy_file` genuinely allocated new sectors).

- [ ] **Step 6: Commit**

```bash
cd /home/deck/os
git add kernel/shell.c kernel/shell.h
git commit -m "$(cat <<'EOF'
kernel: SHELL mv/cp commands

Built on fs_move/fs_copy_file (previous two commits) -- both take a
source file and a destination directory, both files-only (falls out
of the underlying primitives). Verified headlessly: mv relocates
CONFIG between /ETC's real subdirectories with its original on-disk
start_lba intact; cp duplicates it with a distinct start_lba,
leaving the original in place.
EOF
)"
```

---

### Task 4: FILES multi-select (single index → bitmask)

**Files:**
- Modify: `kernel/kernel.c` (`files_selected` → `files_selected_mask` throughout)

**Interfaces:**
- Consumes: nothing new.
- Produces: `uint32_t files_selected_mask` (kmain-local and threaded param, replacing the `int files_selected` used today) — Task 5's clipboard staging reads this mask directly.

This task is a mechanical type/rename change at every site listed in Step 1, plus three sites whose *behavior* genuinely changes (Steps 2-4).

- [ ] **Step 1: Rename `files_selected` to `files_selected_mask` and change `int`/`FILES_HIT_NONE` to `uint32_t`/`0`**

Apply this exact replacement at each site (all in `kernel/kernel.c`):

| Site | Old | New |
|---|---|---|
| `draw_files_group` signature (~line 499) | `int files_selected` | `uint32_t files_selected_mask` |
| `open_files_at` signature (~line 603) | `int *files_selected` | `uint32_t *files_selected_mask` |
| `open_files_at` body (~line 606) | `*files_selected = FILES_HIT_NONE;` | `*files_selected_mask = 0;` |
| `draw_window_by_index` signature (~line 621) | `int files_selected` | `uint32_t files_selected_mask` |
| `draw_window_by_index` body, call into `draw_files_group` (~line 626) | `files_selected,` | `files_selected_mask,` |
| `draw_scene` signature (~line 644) | `int files_selected` | `uint32_t files_selected_mask` |
| `draw_scene` body, call into `draw_window_by_index` (~line 659) | `files_selected,` | `files_selected_mask,` |
| `update_and_present` signature (~line 716) | `int files_selected` | `uint32_t files_selected_mask` |
| `update_and_present` body, call into `draw_window_by_index` (~line 835) | `files_selected,` | `files_selected_mask,` |
| `kmain` local (~line 882) | `int files_selected = FILES_HIT_NONE;` | `uint32_t files_selected_mask = 0;` |
| First `draw_scene` call (~line 1114) | `file_entry_count, files_selected,` | `file_entry_count, files_selected_mask,` |
| Old-state snapshot (~line 1141) | `int old_files_selected = files_selected;` | `uint32_t old_files_selected_mask = files_selected_mask;` |
| `open_files_at` call for CONFIG (~line 1264) | `&files_selected);` | `&files_selected_mask);` |
| `open_files_at` call for GAMES (~line 1267) | `&files_selected);` | `&files_selected_mask);` |
| `touched[WIN_KIND_FILES]` (~line 1601) | `(files_selected != old_files_selected) \|\|` | `(files_selected_mask != old_files_selected_mask) \|\|` |
| Final `update_and_present` call (~line 1618) | `file_entry_count, files_selected,` | `file_entry_count, files_selected_mask,` |

- [ ] **Step 2: `draw_files_group`'s selection highlight (~line 547)**

Old:
```c
        if ((int)i == files_selected) {
```
New:
```c
        if (files_selected_mask & (1u << i)) {
```

- [ ] **Step 3: Right-click toggle becomes a bit toggle (~line 1392-1397)**

Old:
```c
                if (files_is_topmost && right_held && !prev_right_held) {
                    int hit = files_list_hit_test(&windows[WIN_KIND_FILES], cwd, file_entry_count, cx, cy);
                    if (hit >= 0) {
                        files_selected = (files_selected == hit) ? FILES_HIT_NONE : hit;
                    }
                }
```
New:
```c
                /* Multi-select: right-click toggles that row's own bit,
                 * leaving every other row's selection state alone --
                 * unlike the old single-index version, several rows can
                 * be selected at once now (needed for CUT/COPY/DELETE to
                 * act on more than one file per click). */
                if (files_is_topmost && right_held && !prev_right_held) {
                    int hit = files_list_hit_test(&windows[WIN_KIND_FILES], cwd, file_entry_count, cx, cy);
                    if (hit >= 0) {
                        files_selected_mask ^= (1u << hit);
                    }
                }
```

- [ ] **Step 4: DELETE iterates every set bit (~line 1399-1416)**

Old:
```c
                /* Deletes whatever files_selected names. A non-empty
                 * directory or an already-stale selection just makes
                 * fs_delete() fail, left as a silent no-op -- no
                 * error-message UI in the FILES window yet, same
                 * deferred-for-now choice as this window's other no-ops
                 * (e.g. clicking the header row). */
                delete_btn.hovered = files_is_topmost && button_hit_test(&delete_btn, cx, cy);
                if (delete_btn.hovered && click_edge && files_selected >= 0) {
                    char del_path[FILES_PATH_MAX];
                    fs_path_join(del_path, (int)sizeof(del_path), cwd, file_entries[files_selected].name);
                    if (fs_delete(del_path) == 0) {
                        files_selected = FILES_HIT_NONE;
                        if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, &file_entry_count) != 0) {
                            file_entry_count = 0;
                        }
                    }
                }
                delete_btn.pressed = delete_btn.hovered && left_held;
```
New:
```c
                /* Deletes every row named by a set bit in
                 * files_selected_mask -- a generalization of the old
                 * single-index delete, not a behavior change when only
                 * one bit is ever set. Each row is attempted
                 * independently (a non-empty directory or an
                 * already-stale entry just makes that one fs_delete()
                 * fail, silently, same as before); the mask always resets
                 * and the listing always re-reads afterward regardless of
                 * any individual failure, since row indices are stale
                 * either way once anything might have changed. */
                delete_btn.hovered = files_is_topmost && button_hit_test(&delete_btn, cx, cy);
                if (delete_btn.hovered && click_edge && files_selected_mask != 0) {
                    unsigned int di;
                    for (di = 0; di < file_entry_count; di++) {
                        if (files_selected_mask & (1u << di)) {
                            char del_path[FILES_PATH_MAX];
                            fs_path_join(del_path, (int)sizeof(del_path), cwd, file_entries[di].name);
                            fs_delete(del_path);
                        }
                    }
                    files_selected_mask = 0;
                    if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, &file_entry_count) != 0) {
                        file_entry_count = 0;
                    }
                }
                delete_btn.pressed = delete_btn.hovered && left_held;
```

- [ ] **Step 5: Enter-key rename only fires for exactly one selected row (~line 1544-1580)**

Old:
```c
                if (console_input_feed_char(&name_input, c) && name_input.text[0] != 0) {
                    int ok;
                    if (files_selected >= 0) {
                        char old_path[FILES_PATH_MAX];
                        fs_path_join(old_path, (int)sizeof(old_path), cwd, file_entries[files_selected].name);
                        ok = fs_rename(old_path, name_input.text) == 0;
                    } else {
                        char new_path[FILES_PATH_MAX];
                        fs_path_join(new_path, (int)sizeof(new_path), cwd, name_input.text);
                        ok = fs_create_file(new_path, 0, 0) == 0;
                    }
                    if (ok) {
                        console_input_clear(&name_input);
                        if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, &file_entry_count) != 0) {
                            file_entry_count = 0;
                        }
                    }
                }
```
New:
```c
                if (console_input_feed_char(&name_input, c) && name_input.text[0] != 0) {
                    int ok = 0;
                    if (files_selected_mask == 0) {
                        char new_path[FILES_PATH_MAX];
                        fs_path_join(new_path, (int)sizeof(new_path), cwd, name_input.text);
                        ok = fs_create_file(new_path, 0, 0) == 0;
                    } else if ((files_selected_mask & (files_selected_mask - 1)) == 0) {
                        /* Exactly one bit set (a power of two, including
                         * this check itself being the standard
                         * single-bit test) -- rename that one entry,
                         * same meaning the old files_selected >= 0
                         * branch had before multi-select existed. */
                        unsigned int idx;
                        char old_path[FILES_PATH_MAX];
                        for (idx = 0; idx < file_entry_count; idx++) {
                            if (files_selected_mask & (1u << idx)) {
                                break;
                            }
                        }
                        fs_path_join(old_path, (int)sizeof(old_path), cwd, file_entries[idx].name);
                        ok = fs_rename(old_path, name_input.text) == 0;
                    }
                    /* else: more than one row selected -- a single typed
                     * name can't unambiguously rename several entries,
                     * so Enter is a silent no-op here, same convention
                     * as every other unsupported action in this window. */
                    if (ok) {
                        console_input_clear(&name_input);
                        if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, &file_entry_count) != 0) {
                            file_entry_count = 0;
                        }
                    }
                }
```

- [ ] **Step 6: Build**

```bash
distrobox enter forth-os -- bash -lc 'cd /home/deck/os/boot && make'
```
Expected: clean build.

- [ ] **Step 7: Headless verification**

Against a scratch copy of `fs.img` (never the persistent one):
```bash
distrobox enter forth-os -- bash -lc '
cd /home/deck/os/boot
cp fs.img /tmp/rave_scratch_fs.img
qemu-system-i386 -accel kvm -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 \
  -drive file=/tmp/rave_scratch_fs.img,format=raw,if=ide,bus=0,unit=1 \
  -display none -monitor unix:/tmp/rave_qemu.sock,server,nowait -no-reboot &
sleep 1
'
```
1. Raise FILES from the start menu. Create two files via the name field + Enter (e.g. `A` then `B`) so there are two rows to select.
2. Right-click row `A`, right-click row `B` — screendump: confirm *both* rows show the selected highlight simultaneously (this is the actual regression check multi-select needs — the old code could never show two at once).
3. Right-click row `A` again — screendump: confirm only `A` deselects, `B` stays highlighted (proves per-bit toggle, not a shared/overwritten single value).
4. With only `B` selected, type a new name and press Enter — confirm `B` renames (the single-selection rename path still works after the bitmask conversion).
5. Right-click both `A`(now renamed) and the renamed `B` rows again (both selected), type a name, press Enter — confirm neither renames and the typed name stays in the field (the new "no-op on multi-select" branch, not a crash or an arbitrary rename).
6. Right-click both rows, click DELETE — screendump: confirm both are gone from the listing in one click (the new multi-delete loop).

- [ ] **Step 8: Commit**

```bash
cd /home/deck/os
git add kernel/kernel.c
git commit -m "$(cat <<'EOF'
kernel: FILES multi-select via a selection bitmask

files_selected (a single row index) becomes files_selected_mask
(uint32_t) -- right-click now toggles a row's own bit instead of
overwriting the whole selection, DELETE acts on every set bit, and
Enter-to-rename only fires when exactly one row is selected (a typed
name can't unambiguously rename several). Groundwork for CUT/COPY,
which need to stage more than one file at once.
EOF
)"
```

---

### Task 5: FILES clipboard + CUT/COPY/PASTE

**Files:**
- Modify: `kernel/kernel.c` (window resize, new buttons, clipboard struct/helper, threading through the draw/move pipeline, click handlers, damage tracking)

**Interfaces:**
- Consumes: `files_selected_mask` (Task 4), `fs_move`/`fs_copy_file` (Tasks 1-2).
- Produces: nothing further downstream — this is the last task touching `kernel.c` before the final wrap-up.

- [ ] **Step 1: Grow the FILES window and shift the existing button row up to make room**

Change (line 914):
```c
    windows[WIN_KIND_FILES].h = 250;
```
to:
```c
    /* 28px taller than before -- room for a second button row
     * (CUT/COPY/PASTE) below NEW DIR/DELETE without shrinking the list's
     * existing visible-row capacity, since name_input/new_dir_btn/
     * delete_btn's own y positions (computed from this h below) end up
     * completely unchanged; only the new row appends below them. */
    windows[WIN_KIND_FILES].h = 278;
```

Change (line 931):
```c
    new_dir_btn.y = windows[WIN_KIND_FILES].y + windows[WIN_KIND_FILES].h - 30;
```
to:
```c
    /* -58, not -30 -- the old "-30 from the bottom" position now belongs
     * to the new CUT/COPY/PASTE row below this one (Step 2). Since h grew
     * by exactly 28 to compensate, this still evaluates to the exact same
     * absolute y it always has, so name_input and the list area above it
     * are visually unchanged. */
    new_dir_btn.y = windows[WIN_KIND_FILES].y + windows[WIN_KIND_FILES].h - 58;
```

- [ ] **Step 2: Add `cut_btn`/`copy_btn`/`paste_btn` locals and their init**

Add to `kmain`'s local declarations (after `struct button new_dir_btn;`, ~line 875):
```c
    struct button cut_btn;
    struct button copy_btn;
    struct button paste_btn;
    struct files_clipboard clipboard;
```

Insert after `delete_btn`'s init block (after `delete_btn.pressed = 0;`, ~line 944), before `name_input`'s init:
```c
    /* Second footer row, CUT/COPY/PASTE, directly below NEW DIR/DELETE --
     * three buttons instead of two, so each gets a third of the same
     * margin/gap formula NEW DIR/DELETE already use rather than a new
     * layout scheme. paste_btn absorbs the integer-division remainder so
     * the row still fills edge-to-edge symmetrically (margins match on
     * both sides). */
    cut_btn.x = new_dir_btn.x;
    cut_btn.y = windows[WIN_KIND_FILES].y + windows[WIN_KIND_FILES].h - 30;
    cut_btn.w = (windows[WIN_KIND_FILES].w - 16 - 16) / 3;
    cut_btn.h = 22;
    cut_btn.label = "CUT";
    cut_btn.hovered = 0;
    cut_btn.pressed = 0;

    copy_btn.x = cut_btn.x + cut_btn.w + 8;
    copy_btn.y = cut_btn.y;
    copy_btn.w = cut_btn.w;
    copy_btn.h = 22;
    copy_btn.label = "COPY";
    copy_btn.hovered = 0;
    copy_btn.pressed = 0;

    paste_btn.x = copy_btn.x + copy_btn.w + 8;
    paste_btn.y = cut_btn.y;
    paste_btn.w = (windows[WIN_KIND_FILES].x + windows[WIN_KIND_FILES].w - 8) - paste_btn.x;
    paste_btn.h = 22;
    paste_btn.label = "PASTE";
    paste_btn.hovered = 0;
    paste_btn.pressed = 0;

    clipboard.count = 0;
    clipboard.is_cut = 0;
    clipboard.source_dir[0] = 0;
```

- [ ] **Step 3: Define `struct files_clipboard` and `files_clipboard_stage()`**

Insert immediately before `draw_files_group`'s definition (~line 498), right after the existing comment block that explains `files_selected`/row numbering:

```c
/* Holds files cut/copied from the FILES window, independent of the
 * current listing/selection so it survives navigating to a different
 * directory before pasting -- the whole point of "select, then navigate,
 * then paste". v1 is files-only: a selected directory is never staged
 * here (see files_clipboard_stage() below). */
struct files_clipboard {
    char source_dir[FILES_PATH_MAX];
    char names[FS_LIST_MAX][FS_NAME_MAX];
    unsigned int count;
    int is_cut; /* 1 = CUT (fs_move on paste), 0 = COPY (fs_copy_file on paste) */
};

/* Snapshots every currently-selected row in file_entries that's a file
 * (a selected FS_TYPE_DIR row is silently skipped -- directories are out
 * of scope for move/copy in v1) into clip, recording cwd as where they
 * came from and whether this was a CUT or a COPY. If nothing file-typed
 * ended up selected, clip is left completely untouched -- same silent-
 * no-op convention as every other unsupported action in this window. */
static void files_clipboard_stage(struct files_clipboard *clip, const char *cwd,
                                  const struct fs_dirent *file_entries, unsigned int file_entry_count,
                                  uint32_t files_selected_mask, int is_cut) {
    unsigned int i;
    unsigned int n = 0;

    for (i = 0; i < file_entry_count; i++) {
        if ((files_selected_mask & (1u << i)) && file_entries[i].type == FS_TYPE_FILE) {
            int p = 0;
            str_append(clip->names[n], &p, (int)sizeof(clip->names[n]), file_entries[i].name);
            n++;
        }
    }
    if (n == 0) {
        return;
    }
    {
        int p = 0;
        str_append(clip->source_dir, &p, (int)sizeof(clip->source_dir), cwd);
    }
    clip->count = n;
    clip->is_cut = is_cut;
}
```

- [ ] **Step 4: Thread `cut_btn`/`copy_btn`/`paste_btn` through the draw/move pipeline**

At each of the five function signatures below, add three parameters immediately after the existing `delete_btn` parameter (matching that parameter's own const-ness at each site):

- `move_window_content` (~line 324): after `struct button *delete_btn,` add `struct button *cut_btn, struct button *copy_btn, struct button *paste_btn,`
- `draw_files_group` (~line 500): after `const struct button *delete_btn` add `, const struct button *cut_btn, const struct button *copy_btn, const struct button *paste_btn`
- `draw_window_by_index` (~line 622): same addition as `draw_files_group`
- `draw_scene` (~line 645): same addition
- `update_and_present` (~line 717): same addition

At each of these bodies, add the matching arguments immediately after the existing `delete_btn`/`&delete_btn` argument:

- `move_window_content`'s `WIN_KIND_FILES` branch (~line 336-337, after `delete_btn->y += applied_dy;`):
  ```c
        cut_btn->x += applied_dx;
        cut_btn->y += applied_dy;
        copy_btn->x += applied_dx;
        copy_btn->y += applied_dy;
        paste_btn->x += applied_dx;
        paste_btn->y += applied_dy;
  ```
- `draw_files_group`'s body (~line 558, after `button_draw(delete_btn);`):
  ```c
    button_draw(cut_btn);
    button_draw(copy_btn);
    button_draw(paste_btn);
  ```
- `draw_window_by_index`'s call into `draw_files_group` (~line 626-627): append `, cut_btn, copy_btn, paste_btn` before the closing `)`
- `draw_scene`'s call into `draw_window_by_index` (~line 659-660): append `cut_btn, copy_btn, paste_btn,` after `delete_btn`
- `update_and_present`'s call into `draw_window_by_index` (~line 835-836): same as `draw_scene`'s

- [ ] **Step 5: Update the three top-level call sites in `kmain`**

- First `draw_scene` call (~line 1113-1115): append `, &cut_btn, &copy_btn, &paste_btn` after `&delete_btn`
- `move_window_content` call (~line 1241-1242): append `, &cut_btn, &copy_btn, &paste_btn` after `&delete_btn`
- Final `update_and_present` call (~line 1615-1619): append `, &cut_btn, &copy_btn, &paste_btn` after `&delete_btn`

- [ ] **Step 6: Add CUT/COPY/PASTE click handlers**

Insert after `new_dir_btn.pressed = new_dir_btn.hovered && left_held;` (~line 1437), still inside the same `files_is_topmost` block:

```c
                /* CUT/COPY stage the current selection into clipboard
                 * (files only -- a selected directory is silently
                 * skipped inside files_clipboard_stage()). An empty
                 * selection, or one covering only directories, leaves
                 * whatever the clipboard already held untouched. */
                cut_btn.hovered = files_is_topmost && button_hit_test(&cut_btn, cx, cy);
                if (cut_btn.hovered && click_edge && files_selected_mask != 0) {
                    files_clipboard_stage(&clipboard, cwd, file_entries, file_entry_count, files_selected_mask, 1);
                }
                cut_btn.pressed = cut_btn.hovered && left_held;

                copy_btn.hovered = files_is_topmost && button_hit_test(&copy_btn, cx, cy);
                if (copy_btn.hovered && click_edge && files_selected_mask != 0) {
                    files_clipboard_stage(&clipboard, cwd, file_entries, file_entry_count, files_selected_mask, 0);
                }
                copy_btn.pressed = copy_btn.hovered && left_held;

                /* PASTE applies every clipboard entry against cwd --
                 * wherever FILES has navigated to is the destination,
                 * which is what makes navigating double as picking where
                 * to paste. Each name is attempted independently; one
                 * failing (e.g. a name collision at the destination)
                 * doesn't stop the rest. A successful CUT's clipboard
                 * clears after paste (the originals are gone, so it only
                 * makes sense once); a COPY's clipboard is left intact so
                 * the same files can be pasted into several directories
                 * in a row. */
                paste_btn.hovered = files_is_topmost && button_hit_test(&paste_btn, cx, cy);
                if (paste_btn.hovered && click_edge && clipboard.count > 0) {
                    unsigned int pi;
                    for (pi = 0; pi < clipboard.count; pi++) {
                        char paste_src[FILES_PATH_MAX];
                        fs_path_join(paste_src, (int)sizeof(paste_src), clipboard.source_dir, clipboard.names[pi]);
                        if (clipboard.is_cut) {
                            fs_move(paste_src, cwd);
                        } else {
                            fs_copy_file(paste_src, cwd);
                        }
                    }
                    if (clipboard.is_cut) {
                        clipboard.count = 0;
                    }
                    if (fs_list_dir(cwd, file_entries, FS_LIST_MAX, &file_entry_count) != 0) {
                        file_entry_count = 0;
                    }
                }
                paste_btn.pressed = paste_btn.hovered && left_held;
```

- [ ] **Step 7: Old-state snapshot for damage tracking**

Add after `int old_new_dir_btn_pressed = new_dir_btn.pressed;` (~line 1145):
```c
        int old_cut_btn_hovered = cut_btn.hovered;
        int old_cut_btn_pressed = cut_btn.pressed;
        int old_copy_btn_hovered = copy_btn.hovered;
        int old_copy_btn_pressed = copy_btn.pressed;
        int old_paste_btn_hovered = paste_btn.hovered;
        int old_paste_btn_pressed = paste_btn.pressed;
```

- [ ] **Step 8: Fold the new buttons into `touched[WIN_KIND_FILES]`**

Add to the `touched[WIN_KIND_FILES] = ... ;` expression (~line 1600-1609), immediately after the `(new_dir_btn.pressed != old_new_dir_btn_pressed) ||` line:
```c
                                      (cut_btn.hovered != old_cut_btn_hovered) ||
                                      (cut_btn.pressed != old_cut_btn_pressed) ||
                                      (copy_btn.hovered != old_copy_btn_hovered) ||
                                      (copy_btn.pressed != old_copy_btn_pressed) ||
                                      (paste_btn.hovered != old_paste_btn_hovered) ||
                                      (paste_btn.pressed != old_paste_btn_pressed) ||
```

- [ ] **Step 9: Build**

```bash
distrobox enter forth-os -- bash -lc 'cd /home/deck/os/boot && make'
```
Expected: clean build. This is the task most likely to surface a missed threading site (a forgotten parameter at one of the five signatures in Step 4) — the compiler will error on any mismatched call, since these are all plain C functions with fixed positional parameters, not variadic or default-argument.

- [ ] **Step 10: Headless verification**

Against a fresh scratch copy of `fs.img`:
```bash
distrobox enter forth-os -- bash -lc '
cd /home/deck/os/boot
cp fs.img /tmp/rave_scratch_fs2.img
qemu-system-i386 -accel kvm -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 \
  -drive file=/tmp/rave_scratch_fs2.img,format=raw,if=ide,bus=0,unit=1 \
  -display none -monitor unix:/tmp/rave_qemu2.sock,server,nowait -no-reboot &
sleep 1
'
```
1. Raise FILES. Screendump: confirm the window now shows three rows of controls (name field, NEW DIR/DELETE, CUT/COPY/PASTE) and the list area looks unchanged in size from before this task (same number of visible rows as Task 4's screendumps).
2. Create two files (`A`, `B`) via the name field. Right-click both to select them.
3. Click CUT. Navigate (left-click) into a different directory (e.g. `/ETC`). Click PASTE — screendump: confirm both `A` and `B` now appear in `/ETC`'s listing.
4. Navigate back to the original directory — confirm the listing no longer shows `A`/`B` (proves `fs_move`, not a duplicate).
5. In `/ETC`, select `A` and `B` again, click COPY. Navigate into a third directory (e.g. `/VAR`). Click PASTE — confirm both appear in `/VAR`, and navigate back to `/ETC` to confirm they're *still there too* (copy leaves the source intact).
6. Immediately click PASTE again while still in `/VAR` (clipboard should still be full after a COPY) — expect nothing changes (the same names already exist there, so each `fs_copy_file` call fails on the name collision — the documented silent-no-op). Navigate into a fourth, empty directory and click PASTE there instead — confirm it succeeds (proves COPY's clipboard really does persist across multiple pastes, not just accidentally still there for one).
7. Create a directory (e.g. `NEW DIR` button, name `SUBDIR`) alongside a file, right-click both to select them, click CUT, navigate elsewhere, click PASTE — confirm only the file appears at the destination, and navigating back confirms the directory `SUBDIR` is still in its original place, untouched (proves the files-only skip inside `files_clipboard_stage`).
8. Parse `/tmp/rave_scratch_fs2.img` directly (same technique as Task 3's Step 5) to confirm: the moved files' `start_lba` values are unchanged from their original allocation (metadata-only relocation), and the copied files have distinct `start_lba` values from their originals (real second allocation).

- [ ] **Step 11: Commit**

```bash
cd /home/deck/os
git add kernel/kernel.c
git commit -m "$(cat <<'EOF'
kernel: FILES CUT/COPY/PASTE, clipboard-style move/copy

Grows the FILES window by 28px for a new CUT/COPY/PASTE button row.
CUT/COPY stage the current multi-selection (files only) into a
clipboard that survives navigating to a different directory; PASTE
applies it against cwd via fs_move/fs_copy_file. A cut clipboard
clears after one paste; a copied one persists for repeated pastes.
Verified headlessly against fs.img directly: moved files keep their
original start_lba, copied files get a distinct one.
EOF
)"
```

---

### Task 6: End-to-end regression pass and `docs/BUILD_LOG.md` entry

**Files:**
- Modify: `docs/BUILD_LOG.md` (append one entry covering the whole feature)

**Interfaces:**
- Consumes: everything from Tasks 1-5.
- Produces: nothing further — this is the plan's final task.

- [ ] **Step 1: Full regression walkthrough**

Against a fresh scratch `fs.img` copy, headlessly:
1. Boot, open FORTH, type something into it (e.g. a simple word), leave it open.
2. Open SHELL, run `mv`/`cp` once each (per Task 3's steps) — confirm FORTH's own scrollback is untouched (no cross-window state bleed, same check every prior stage's regression pass includes).
3. Open FILES, run the full CUT → navigate → PASTE and COPY → paste-twice sequences from Task 5's Step 10.
4. Confirm the pre-existing single-select DELETE/rename/create-file/NEW DIR flows (from before this plan) still work exactly as before — right-click one row, DELETE it; right-click one row, rename it via Enter; type a name with nothing selected, press Enter to create a file; type a name, click NEW DIR.
5. Drag the FILES window by its titlebar — screendump: confirm the name field *and* all five buttons (NEW DIR, DELETE, CUT, COPY, PASTE) move together with it (regression-checks Task 5 Step 4's `move_window_content` threading).

- [ ] **Step 2: Append the BUILD_LOG.md entry**

Append to the end of `docs/BUILD_LOG.md`, following the file's existing entry format (a `## YYYY-MM-DD -- <title>` heading, prose paragraphs covering what changed and why, a bolded "Verified headlessly" paragraph, and a closing `Files:` line):

```markdown
## 2026-08-16 -- FILES multi-select, CUT/COPY/PASTE, and SHELL mv/cp

Picked up from `docs/IDEAS.md`'s "upgrade the FILES window" entry, brainstormed
into a design spec (`docs/superpowers/specs/2026-08-16-files-multiselect-move-copy-design.md`)
and implementation plan first. FILES had no way to relocate or duplicate a
file at all before this -- only DELETE/rename/create -- and only ever
selected one row at a time.

**Two new fs.c primitives, both files-only (directories stay out of scope,
flagged as a follow-up in IDEAS.md).** `fs_move(path, dest_dir)` relocates a
file's directory-table entry between parents' tables with no data read,
write, or reallocation at all -- deliberately, since the allocator's
one-way bump design (no free list, documented on `fs_delete()` since Stage
D) means a copy-then-delete implementation would permanently leak the
original's sectors on every single move. `fs_copy_file(path, dest_dir)`
does need a genuine second allocation (there's no way around two copies
needing two allocations) -- streams the source sector-by-sector through
the same `ata_read_sector`/`ata_write_sector` primitives `fs_read_file`/
`fs_create_file` already use internally, rather than requiring a whole-file
buffer this malloc-less kernel has no safe way to size.

**FILES: `files_selected` (a single row index) becomes
`files_selected_mask` (a `uint32_t` bitmask)** -- right-click now toggles
one row's own bit instead of overwriting the whole selection, so several
rows can be selected simultaneously. DELETE generalizes to iterate every
set bit. Enter-to-rename only fires when exactly one bit is set (a typed
name can't unambiguously rename several entries at once) -- with more than
one selected, Enter is a silent no-op, same convention as every other
unsupported click in this window.

**FILES: clipboard-style CUT/COPY/PASTE**, a new button row below NEW
DIR/DELETE (the window grew 28px taller to fit it, with the existing list
area's size completely unchanged). CUT/COPY snapshot every *file* in the
current selection (a selected directory is silently skipped) into a small
clipboard struct that survives navigating to a different directory --
that's the whole mechanism: select, click CUT or COPY, click through
directories using the FILES window's existing navigation, click PASTE
wherever you land. PASTE applies `fs_move`/`fs_copy_file` per clipboard
entry against the current directory; a cut clipboard clears after one
successful paste (the originals are gone), a copied one persists so the
same files can be pasted into several places in a row. Every failure here
(a name collision at the destination, an empty clipboard, a directory in
the selection) is a silent no-op, matching this window's existing
DELETE/rename/create failure conventions -- no new error-message UI was
added.

**SHELL: `mv <src> <dest_dir>` and `cp <src> <dest_dir>`**, built on the
same two primitives -- unlike FILES' clipboard, a typed command line
already names both ends in one shot, so no clipboard state was needed
here. Both arguments go through the existing `shell_resolve()`/
`shell_case_correct()` pipeline (full correction on both -- unlike
`mkdir`'s new name, neither argument here is being newly typed into
existence). Failure prints `mv: failed`/`cp: failed`, matching
`shell_cmd_rm()`'s existing wording.

**Verified headlessly, in stages, each against a disposable scratch copy of
fs.img (never the persistent one):** SHELL's `mv`/`cp` first, since a typed
command is far faster to drive than FILES' mouse/clipboard flow and
directly exercises the two new primitives -- moved `/ETC/CONFIG` into a
fresh subdirectory, confirmed it vanished from `/ETC`'s own listing and
reappeared with its real content (`FX=0`/`FX=1`) intact in the new
location; copied it a second time into another subdirectory, confirmed it
now existed in *both* places. Parsed `fs.img`'s on-disk directory tables
directly (not just screendumps) to confirm the moved file's `start_lba`
was byte-identical to its original allocation (proving `fs_move` really is
metadata-only) while the copy got a distinct `start_lba` (proving
`fs_copy_file` genuinely allocated new sectors). Then FILES: multi-select
(two rows highlighted simultaneously, independently toggleable, DELETE
removing both in one click), CUT/navigate/PASTE (files relocated, source
directory empty afterward), COPY/paste-into-several-directories-in-a-row
(clipboard persisting correctly across repeated pastes, unlike CUT's
single-use clipboard), and the directories-are-silently-skipped case
(a mixed file+directory selection only moved the file, leaving the
directory in place). A full regression pass confirmed FORTH's own
scrollback stayed untouched by SHELL's mv/cp session, and that dragging
the FILES window by its titlebar still moves the name field and all five
buttons (NEW DIR, DELETE, CUT, COPY, PASTE) together, and that every
pre-existing single-select DELETE/rename/create-file/NEW DIR flow still
works unchanged after the bitmask conversion.

Files: `kernel/fs.h`/`fs.c` (`fs_move`, `fs_copy_file`, new); `kernel/shell.h`/
`shell.c` (`shell_two_args`, `shell_cmd_mv`, `shell_cmd_cp`, new; dispatch and
doc comment updated); `kernel/kernel.c` (`files_selected` → `files_selected_mask`
throughout; `struct files_clipboard`, `files_clipboard_stage()`, `cut_btn`/
`copy_btn`/`paste_btn`, new; FILES window height, `move_window_content()`,
`draw_files_group()`, `draw_window_by_index()`, `draw_scene()`,
`update_and_present()`, and the main event loop's click/keyboard handling
all updated).
```

- [ ] **Step 3: Commit**

```bash
cd /home/deck/os
git add docs/BUILD_LOG.md
git commit -m "$(cat <<'EOF'
docs: BUILD_LOG entry for FILES multi-select/move/copy + SHELL mv/cp
EOF
)"
```
