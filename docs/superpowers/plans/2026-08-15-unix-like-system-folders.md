# Unix-like System Folders Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Rave-OS's file manager show a real Unix-like root directory structure (`/BIN /ETC /HOME /USR /VAR /TMP /DEV`) from boot, instead of only whatever a user happened to create by hand.

**Architecture:** Two tiers. `/BIN /ETC /HOME /USR /VAR /TMP` are plain real directories, created idempotently at boot via the existing `fs_create_dir()`. `/DEV` is fully synthetic — never written to disk — `fs_list_dir("/DEV")` synthesizes its contents live by probing `kernel/ata.c`'s two fixed drives, and `fs_list_dir("/")` injects one synthetic `DEV` row alongside real entries. Three small guards in `fs.c` stop the reserved name `DEV` from being shadowed by a real entry.

**Tech Stack:** C (`-m32 -ffreestanding`, no libc), x86 kernel, ATA PIO disk driver, this project's own flat-file-table filesystem (`kernel/fs.c`). No unit-test framework exists for this code — verification is build + headless QEMU monitor screendumps (see Testing Recipe below), the same discipline every prior stage in this project has used.

**Spec:** `docs/superpowers/specs/2026-08-15-unix-like-system-folders-design.md`

## Global Constraints

- Build inside the `forth-os` distrobox container — the SteamOS host has no toolchain. Every build command below is wrapped in `distrobox-enter forth-os -- bash -c "..."`.
- `cd /home/deck/os/boot && make disk.img` rebuilds `kernel/kernel.bin` (via `kernel/Makefile`) and re-links `disk.img` automatically. It does **not** touch `fs.img`.
- `fs.img` is persistent across builds and boots — real cross-boot state. It already contains test clutter from prior sessions (`TESTDIR`, `OLD DIR`, etc.). This feature does not clean that up; leave it alone.
- Kill any leftover QEMU before starting a new session: `pgrep -af 'qemu-system-i386.*disk.img'` then `kill` if anything shows up (excluding the grep's own shell wrapper). A stale process from a previous session corrupts the next one's results (`Failed to get "write" lock`, or you end up talking to an old build).
- Known-broken headless `sendkey` keys in this QEMU build: any shift-modified symbol (`equal`, `plus`, `slash`, `shift-8`, `colon`, `semicolon`, `exclam`, `at`) sends nothing at all over the monitor socket. Not relevant here — every name this feature uses (`BIN`, `ETC`, `HOME`, `USR`, `VAR`, `TMP`, `DEV`, `HDA`, `HDB`) is plain unshifted letters — but don't add a step that needs one.
- Cursor starts at screen position `(320, 380)` on every fresh boot (`mx = w/2`, `my = h - 100` in `kmain()`, screen is 640x480). `mouse_move dx dy` sends **relative** deltas, not absolute coordinates. A single delta over ~127px is unreliable — split into two or more smaller moves with a short pause between them.
- Taskbar tab `N` (0-indexed: `0`=PANEL, `1`=INFO, `2`=FORTH, `3`=FILES, `4`=VIEWER) is centered at pixel `(78 + 148*N, 468)`.
- The FILES window is at `x=420, y=120, w=180, h=250` (unless previously dragged — it won't be, in a fresh boot). Its listing rows start at `y = 132` (`window.y + FILES_LIST_Y_OFFSET(12)`) and repeat every `20px` (`FILES_ROW_HEIGHT`). Row 0 is the path header (not clickable as an entry); row 1 is `".."` if the current directory isn't root, otherwise the first real entry. `NEW DIR`/`DELETE` buttons are centered at `(467, 351)` / `(553, 351)`; the name field is centered at `(510, 324)`.

### Testing recipe (headless)

No ImageMagick or PIL is available in this environment. Convert QEMU's `.ppm` screendumps to `.png` with this exact inline Python snippet, then view the `.png` with the Read tool:

```bash
python3 - <<'EOF'
import struct, zlib
def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    i = 2
    vals = []
    while len(vals) < 3:
        while data[i] in b' \t\r\n': i += 1
        j = i
        while data[j] not in b' \t\r\n': j += 1
        vals.append(int(data[i:j])); i = j
    i += 1
    w, h, maxval = vals
    return w, h, data[i:i+w*h*3]
def write_png(path, w, h, rgb):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag+data))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)))
        raw = bytearray()
        stride = w*3
        for y in range(h):
            raw.append(0); raw.extend(rgb[y*stride:(y+1)*stride])
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        f.write(chunk(b'IEND', b''))
w, h, px = read_ppm('SRC.ppm')
write_png('DST.png', w, h, px)
EOF
```

(Replace `SRC.ppm`/`DST.png` per call, or just reuse fixed scratch filenames like `/tmp/rave_test.ppm`/`/tmp/rave_test.png` and overwrite them each round-trip.)

To boot headless with a monitor socket:

```bash
cd /home/deck/os/boot && distrobox-enter forth-os -- bash -c \
  "qemu-system-i386 -accel kvm -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 -display none -monitor unix:/tmp/rave_mon.sock,server,nowait -daemonize"
```

To drive it, send **one monitor command per `socat` call** (batching multiple newline-joined commands in one write is unreliable, confirmed in this project's own past sessions):

```bash
distrobox-enter forth-os -- bash -c "echo 'COMMAND HERE' | socat - unix-connect:/tmp/rave_mon.sock >/dev/null"
```

Useful commands: `mouse_move dx dy`, `mouse_button 1` (press) / `mouse_button 0` (release), `sendkey KEYNAME` (e.g. `sendkey d`, `sendkey e`, `sendkey v`, `sendkey enter`, `sendkey backspace`), `screendump /path/to/file.ppm`.

When done with a headless pass: `pkill -f 'qemu-system-i386.*disk.img'`.

---

## Task 1: Standard directory bootstrap (`/BIN /ETC /HOME /USR /VAR /TMP`)

**Files:**
- Modify: `kernel/fs.h` (add `fs_bootstrap_dirs()` declaration)
- Modify: `kernel/fs.c` (add the standard-dirs list and `fs_bootstrap_dirs()`)
- Modify: `kernel/kernel.c:1045` (call it once at boot)

**Interfaces:**
- Produces: `void fs_bootstrap_dirs(void);` — idempotent, safe to call every boot. Consumed by `kernel.c`'s `kmain()`.
- Consumes: existing `fs_create_dir(const char *path)` (declared in `fs.h`, already used by `fs_selftest()` and the FILES window's NEW DIR flow) and the existing `mounted`/`fs_init()` pattern already used by every other `fs.c` entry point.

- [ ] **Step 1: Add the declaration to `kernel/fs.h`**

Insert immediately after the existing `void fs_init(void);` line (near the top of the file, before the path-format comment block):

```c
void fs_init(void);

/* Idempotent: creates /BIN /ETC /HOME /USR /VAR /TMP if they don't
 * already exist (fs_create_dir()'s "already exists" failure is treated
 * as success), the same shape fs_selftest() already relies on for
 * /TESTDIR. Safe -- expected -- to call every boot, not just on a fresh
 * format. Deliberately does NOT create /DEV -- that name is reserved
 * for fs_list_dir()'s synthetic listing, see its own comment below. */
void fs_bootstrap_dirs(void);
```

- [ ] **Step 2: Add the implementation to `kernel/fs.c`**

Insert immediately before the existing `#define FS_SELFTEST_DIR "/TESTDIR"` line:

```c
#define FS_NUM_STANDARD_DIRS 6
static const char *const fs_standard_dirs[FS_NUM_STANDARD_DIRS] = {
    "/BIN", "/ETC", "/HOME", "/USR", "/VAR", "/TMP",
};

void fs_bootstrap_dirs(void) {
    int i;
    if (!mounted) {
        fs_init();
    }
    for (i = 0; i < FS_NUM_STANDARD_DIRS; i++) {
        fs_create_dir(fs_standard_dirs[i]); /* -1 ("already exists", or a full parent table) is fine to ignore here -- boot-time plumbing, not a user action */
    }
}
```

- [ ] **Step 3: Wire the call into `kernel/kernel.c`**

Find this existing block (around line 1044-1045):

```c
    ata_status = ata_selftest();
    fs_status = fs_selftest();
```

Change it to:

```c
    ata_status = ata_selftest();
    fs_status = fs_selftest();
    fs_bootstrap_dirs();
```

- [ ] **Step 4: Build**

```bash
distrobox-enter forth-os -- bash -c "cd /home/deck/os/boot && make disk.img 2>&1 | tail -n 20"
```

Expected: clean build, no warnings, ends with the `disk layout: ...` line. If `gcc` reports an undeclared-function or type-mismatch error, re-check Steps 1-3 against the exact existing surrounding code (line numbers may have drifted if other work landed first).

- [ ] **Step 5: Headless-verify — root now lists the six standard directories**

Kill any stale QEMU, boot headless with a monitor socket, raise the FILES window (taskbar tab index 3, so target `(522, 468)` from the boot cursor position `(320, 380)` — delta `(202, 88)`, split into two moves since 202 > 127), screendump, convert, and view:

```bash
pgrep -af 'qemu-system-i386.*disk.img' # confirm empty (ignore the grep's own wrapper line) before proceeding

cd /home/deck/os/boot && distrobox-enter forth-os -- bash -c \
  "qemu-system-i386 -accel kvm -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 -display none -monitor unix:/tmp/rave_mon.sock,server,nowait -daemonize"
sleep 2

distrobox-enter forth-os -- bash -c "
send() { echo \"\$1\" | socat - unix-connect:/tmp/rave_mon.sock >/dev/null; sleep 0.3; }
send 'mouse_move 101 44'
send 'mouse_move 101 44'
send 'mouse_button 1'
send 'mouse_button 0'
send 'screendump /tmp/rave_task1.ppm'
"
```

Then run the Testing Recipe's Python conversion (`SRC.ppm` = `/tmp/rave_task1.ppm`, `DST.png` = `/tmp/rave_task1.png`) and view `/tmp/rave_task1.png` with the Read tool.

Expected: the FILES window is raised (frontmost, its taskbar tab highlighted) and its root listing includes `BIN/`, `ETC/`, `HOME/`, `USR/`, `VAR/`, `TMP/` — alongside whatever pre-existing clutter is already there (`TESTDIR/`, `OLD DIR/`, etc. — leave it, don't worry about it). If the window isn't raised or the click landed wrong, re-derive the taskbar math from the Global Constraints section rather than guessing a new offset.

Kill the headless instance when done: `pkill -f 'qemu-system-i386.*disk.img'`.

Also confirm by reading the code (not a new screendump, same precedent Stage D of the file manager set for its non-empty-directory delete refusal): if root's table were ever full, `fs_create_dir()` would return `-1` for whichever standard directories don't fit, and `fs_bootstrap_dirs()`'s loop (Step 2) already discards that return value and moves on to the next name — no dereference of a failed result, no crash path. This already holds by construction from the code written in Step 2; nothing further to add.

- [ ] **Step 6: Commit**

```bash
cd /home/deck/os && git add kernel/fs.h kernel/fs.c kernel/kernel.c
git commit -m "$(cat <<'EOF'
kernel: bootstrap standard directories (/BIN /ETC /HOME /USR /VAR /TMP)

fs_bootstrap_dirs() creates the six standard top-level directories at
boot if they don't already exist -- idempotent, same shape
fs_selftest() already uses for /TESTDIR. Plain real directories via
the existing fs_create_dir(), genuinely empty since nothing in this
kernel generates real content for them yet.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Synthetic `/DEV` directory + reserved-name write guards

**Files:**
- Modify: `kernel/fs.c` (`fs_list_dir()` rewritten with a new `fs_list_dev()` helper; guards added to `fs_create_dir()`, `fs_create_file()`, `fs_rename()`)
- Modify: `kernel/fs.h` (update `fs_list_dir()`'s doc comment)

**Interfaces:**
- Consumes: `FS_ROOT_LBA`, `FS_SUPERBLOCK_LBA`, `ATA_DRIVE_SLAVE`, `ata_read_sector()` (from `ata.h`, already included), `str_eq()`, `name_copy()`, `walk_to_parent()`, `resolve_dir_lba()` — all pre-existing static helpers/functions in `fs.c`.
- Produces: `fs_list_dir("/DEV")` returns a synthesized listing (`HDA`, and `HDB` if live-readable); `fs_list_dir("/")` includes one synthetic `DEV` entry; `fs_create_dir()`/`fs_create_file()`/`fs_rename()` all reject attempts to create or rename something to the exact name `DEV` at root. No change to any function's signature — this task only changes behavior inside existing functions plus one new `static` helper.

- [ ] **Step 1: Add the reserved-name guard to `fs_create_dir()`**

Find this existing block in `kernel/fs.c`:

```c
    if (walk_to_parent(path, leaf, &table_lba) != 0) {
        return -1;
    }

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
    if (dirtable_find(entries, leaf) >= 0) {
        return -1; /* already exists */
    }
```

(this is inside `fs_create_dir()` — the first occurrence of this exact shape in the file). Change it to:

```c
    if (walk_to_parent(path, leaf, &table_lba) != 0) {
        return -1;
    }

    if (table_lba == FS_ROOT_LBA && str_eq(leaf, "DEV")) {
        return -1; /* reserved: /DEV is synthetic (see fs_list_dir()), never a real directory */
    }

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
    if (dirtable_find(entries, leaf) >= 0) {
        return -1; /* already exists */
    }
```

- [ ] **Step 2: Add the same guard to `fs_create_file()`**

Find this existing block (the second occurrence of this shape, inside `fs_create_file()`):

```c
    if (walk_to_parent(path, leaf, &table_lba) != 0) {
        return -1;
    }

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
    if (dirtable_find(entries, leaf) >= 0) {
        return -1; /* write-once: already exists */
    }
```

Change it to:

```c
    if (walk_to_parent(path, leaf, &table_lba) != 0) {
        return -1;
    }

    if (table_lba == FS_ROOT_LBA && str_eq(leaf, "DEV")) {
        return -1; /* reserved: /DEV is synthetic (see fs_list_dir()), never a real directory */
    }

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
    if (dirtable_find(entries, leaf) >= 0) {
        return -1; /* write-once: already exists */
    }
```

- [ ] **Step 3: Add the shadow guard to `fs_rename()`**

Find this existing block inside `fs_rename()`:

```c
    for (i = 0; new_name[i]; i++) {
        if (new_name[i] == '/' || i >= FS_NAME_MAX - 1) {
            return -1;
        }
    }

    existing = dirtable_find(entries, new_name);
```

Change it to:

```c
    for (i = 0; new_name[i]; i++) {
        if (new_name[i] == '/' || i >= FS_NAME_MAX - 1) {
            return -1;
        }
    }

    if (table_lba == FS_ROOT_LBA && str_eq(new_name, "DEV")) {
        return -1; /* reserved: renaming something to shadow synthetic /DEV */
    }

    existing = dirtable_find(entries, new_name);
```

- [ ] **Step 4: Replace `fs_list_dir()` and add `fs_list_dev()`**

Find the entire existing `fs_list_dir()` function:

```c
int fs_list_dir(const char *path, struct fs_dirent *out, unsigned int max_entries, unsigned int *out_count) {
    struct fs_entry entries[FS_MAX_FILES];
    unsigned int table_lba;
    unsigned int count = 0;
    int i;

    if (!mounted) {
        fs_init();
    }

    if (resolve_dir_lba(path, &table_lba) != 0) {
        return -1;
    }

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (entries[i].name[0] == 0) {
            continue;
        }
        if (count >= max_entries) {
            break; /* truncate rather than overflow the caller's buffer */
        }
        name_copy(out[count].name, entries[i].name);
        out[count].type = entries[i].type;
        out[count].size_bytes = entries[i].size_bytes;
        count++;
    }

    *out_count = count;
    return 0;
}
```

Replace it entirely with:

```c
/* /DEV is synthetic -- never a real directory-table entry. HDA is always
 * listed (this kernel booted from the ATA master, so it's present by
 * construction whenever code is running to ask -- no probe needed). HDB
 * is listed only if the filesystem disk is actually readable right now,
 * reusing the exact live check fs_init() already trusts to mount,
 * instead of new ATA-protocol presence-detection code. */
static void fs_list_dev(struct fs_dirent *out, unsigned int max_entries, unsigned int *out_count) {
    unsigned char probe[ATA_SECTOR_SIZE];
    unsigned int count = 0;

    if (count < max_entries) {
        name_copy(out[count].name, "HDA");
        out[count].type = FS_TYPE_FILE;
        out[count].size_bytes = 0;
        count++;
    }

    if (count < max_entries && ata_read_sector(ATA_DRIVE_SLAVE, FS_SUPERBLOCK_LBA, probe) == 0) {
        name_copy(out[count].name, "HDB");
        out[count].type = FS_TYPE_FILE;
        out[count].size_bytes = 0;
        count++;
    }

    *out_count = count;
}

int fs_list_dir(const char *path, struct fs_dirent *out, unsigned int max_entries, unsigned int *out_count) {
    struct fs_entry entries[FS_MAX_FILES];
    unsigned int table_lba;
    unsigned int count = 0;
    int is_root;
    int dev_shadowed = 0;
    int i;

    if (!mounted) {
        fs_init();
    }

    if (str_eq(path, "/DEV")) {
        fs_list_dev(out, max_entries, out_count);
        return 0;
    }

    if (resolve_dir_lba(path, &table_lba) != 0) {
        return -1;
    }
    is_root = str_eq(path, "/");

    if (dirtable_read(table_lba, entries) != 0) {
        return -1;
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (entries[i].name[0] == 0) {
            continue;
        }
        if (count >= max_entries) {
            break; /* truncate rather than overflow the caller's buffer */
        }
        if (is_root && str_eq(entries[i].name, "DEV")) {
            dev_shadowed = 1; /* a real entry already claims the name; don't double-list it */
        }
        name_copy(out[count].name, entries[i].name);
        out[count].type = entries[i].type;
        out[count].size_bytes = entries[i].size_bytes;
        count++;
    }

    if (is_root && !dev_shadowed && count < max_entries) {
        name_copy(out[count].name, "DEV");
        out[count].type = FS_TYPE_DIR;
        out[count].size_bytes = 0;
        count++;
    }

    *out_count = count;
    return 0;
}
```

- [ ] **Step 5: Update the doc comment in `kernel/fs.h`**

Find:

```c
/* Lists path's direct entries (not recursive) into out[], up to
 * max_entries (callers should size their buffer to FS_MAX_FILES to never
 * truncate). path must name a directory -- "/" for root. Returns 0 and
 * sets *out_count on success, -1 if path doesn't exist or isn't a
 * directory. */
int fs_list_dir(const char *path, struct fs_dirent *out, unsigned int max_entries, unsigned int *out_count);
```

Replace with:

```c
/* Lists path's direct entries (not recursive) into out[], up to
 * max_entries (callers should size their buffer to FS_MAX_FILES to never
 * truncate). path must name a directory -- "/" for root. Returns 0 and
 * sets *out_count on success, -1 if path doesn't exist or isn't a
 * directory.
 *
 * "/DEV" is special: synthetic, never a real on-disk directory. Listing
 * it probes the two fixed ATA drives live (see fs.c) instead of reading
 * a stored table. Listing "/" always includes a synthetic "DEV" entry
 * alongside whatever real entries exist. Nothing can be created inside
 * "/DEV", and nothing can be created/renamed to shadow the name "DEV" at
 * root -- fs_create_dir()/fs_create_file()/fs_rename() all reject it. */
int fs_list_dir(const char *path, struct fs_dirent *out, unsigned int max_entries, unsigned int *out_count);
```

- [ ] **Step 6: Build**

```bash
distrobox-enter forth-os -- bash -c "cd /home/deck/os/boot && make disk.img 2>&1 | tail -n 20"
```

Expected: clean build, no warnings.

- [ ] **Step 7: Headless-verify — `/DEV` lists `HDA`/`HDB`, clicking one fails cleanly**

Kill any stale QEMU, boot headless, raise FILES (same taskbar click as Task 1 Step 5), screendump root and read it to find which row is `DEV` (it will be the **last** row — `fs_list_dev()`'s synthetic entry is always appended after every real one, and `TMP` — the last of the six standard directories, created last by `fs_bootstrap_dirs()` — will be the row immediately above it).

```bash
pgrep -af 'qemu-system-i386.*disk.img' # confirm empty first

cd /home/deck/os/boot && distrobox-enter forth-os -- bash -c \
  "qemu-system-i386 -accel kvm -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 -display none -monitor unix:/tmp/rave_mon.sock,server,nowait -daemonize"
sleep 2

distrobox-enter forth-os -- bash -c "
send() { echo \"\$1\" | socat - unix-connect:/tmp/rave_mon.sock >/dev/null; sleep 0.3; }
send 'mouse_move 101 44'
send 'mouse_move 101 44'
send 'mouse_button 1'
send 'mouse_button 0'
send 'screendump /tmp/rave_task2_root.ppm'
"
```

Convert and view `/tmp/rave_task2_root.png`. Count the listed rows starting at `y=132` in steps of `20px` (row 1 = first entry, since root has no `".."` row) to find `DEV`'s row index `R` (its pixel `y` is `132 + (R-1)*20`, i.e. `120 + R*20`, since row 1 sits at `132 = 120+12`). Click the middle of that row (`x` anywhere inside `420..600`, e.g. `460`) to navigate into it — compute the delta from the current cursor position (`522, 468`, wherever the taskbar click left it) to `(460, 120+R*20+9)`, splitting into two moves if either delta exceeds ~127px:

```bash
distrobox-enter forth-os -- bash -c "
send() { echo \"\$1\" | socat - unix-connect:/tmp/rave_mon.sock >/dev/null; sleep 0.3; }
send 'mouse_move DX DY'
send 'mouse_button 1'
send 'mouse_button 0'
send 'screendump /tmp/rave_task2_dev.ppm'
"
```

(fill in the real `DX DY` computed above). Convert and view `/tmp/rave_task2_dev.png`.

Expected: the listing now shows `".."` on row 1, `HDA` on row 2, and `HDB` on row 3 (QEMU backs both drives with real files in this project's setup, so both should be live-readable). Click `HDA`'s row (delta from wherever the cursor now is to `(460, 132+1*20+9=161)` for row 2, or `181` for row 3 if targeting `HDB`):

```bash
distrobox-enter forth-os -- bash -c "
send() { echo \"\$1\" | socat - unix-connect:/tmp/rave_mon.sock >/dev/null; sleep 0.3; }
send 'mouse_move DX DY'
send 'mouse_button 1'
send 'mouse_button 0'
send 'screendump /tmp/rave_task2_viewer.ppm'
"
```

Convert and view. Expected: a VIEWER window is now raised showing `(READ FAILED)` — not a crash, not a hang, not real file content (there's nothing real behind `/DEV/HDA` to read).

Kill the headless instance: `pkill -f 'qemu-system-i386.*disk.img'`.

- [ ] **Step 8: Headless-verify — the `DEV` name is protected**

Restart headless (previous step's session is fine to keep running if it's still up, but re-verify no stale process first), raise FILES, navigate back to root if the previous step left you inside `/DEV` (click `".."` at row 1, i.e. `y=141`).

Type `DEV` into the name field (click the field first, centered at `(510, 324)`, then `sendkey d`, `sendkey e`, `sendkey v`), then click the NEW DIR button (centered at `(467, 351)`):

```bash
distrobox-enter forth-os -- bash -c "
send() { echo \"\$1\" | socat - unix-connect:/tmp/rave_mon.sock >/dev/null; sleep 0.3; }
send 'mouse_move DX_TO_FIELD DY_TO_FIELD'
send 'mouse_button 1'
send 'mouse_button 0'
send 'sendkey d'
send 'sendkey e'
send 'sendkey v'
send 'mouse_move DX_TO_NEWDIR DY_TO_NEWDIR'
send 'mouse_button 1'
send 'mouse_button 0'
send 'screendump /tmp/rave_task2_guard1.ppm'
"
```

(compute the two deltas from wherever the cursor currently is). Convert and view.

Expected: root's listing is unchanged — still exactly one `DEV/` row (the synthetic one), no duplicate, no error indication needed (silent no-op, same as every other FILES-window failure in this project).

Then test the rename-shadow guard: right-click the `TMP` row (second-to-last row, immediately above `DEV`) to select it, type `DEV` into the name field, press Enter:

```bash
distrobox-enter forth-os -- bash -c "
send() { echo \"\$1\" | socat - unix-connect:/tmp/rave_mon.sock >/dev/null; sleep 0.3; }
send 'mouse_move DX_TO_TMP_ROW DY_TO_TMP_ROW'
send 'mouse_button 2'
send 'mouse_button 0'
send 'mouse_move DX_TO_FIELD DY_TO_FIELD'
send 'mouse_button 1'
send 'mouse_button 0'
send 'sendkey d'
send 'sendkey e'
send 'sendkey v'
send 'sendkey enter'
send 'screendump /tmp/rave_task2_guard2.ppm'
"
```

(`mouse_button 2` is a right-click — press with button code 2, release with `mouse_button 0`, matching this project's established right-click-to-select convention from the file manager's delete/rename stages). Convert and view.

Expected: `TMP/` is still named `TMP/`, not renamed to `DEV`; the typed text `DEV` remains sitting in the name field (per this project's established failure-visibility convention — a failed rename leaves the typed text in place rather than clearing it); root still has exactly one `DEV/` row.

Kill the headless instance: `pkill -f 'qemu-system-i386.*disk.img'`.

- [ ] **Step 9: Commit**

```bash
cd /home/deck/os && git add kernel/fs.c kernel/fs.h
git commit -m "$(cat <<'EOF'
kernel: synthetic /DEV directory, reserved-name write guards

/DEV is never written to disk -- fs_list_dir("/DEV") synthesizes HDA
(always present, we booted from it) and HDB (present if a live read
of the fs superblock sector succeeds right now) by probing ata.c's
two fixed drives directly. fs_list_dir("/") injects one synthetic
DEV row. fs_create_dir()/fs_create_file()/fs_rename() all reject the
exact name "DEV" at root, so nothing can shadow the synthetic entry
with a real one.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Post-implementation: build log entry

After both tasks are committed, add one entry to `docs/BUILD_LOG.md` (append at the end, following the existing dated-heading style already used for every prior stage — read the last few entries in that file for the exact tone/format before writing this one) covering: why this was picked up (making the file manager look like a real Unix root), the two-tier design decision and why `/DEV` needed to be synthetic rather than a plain directory, the `HDA`/`HDB` live-probe reasoning, the three write-guards, and the headless verification results from Task 1 Step 5 and Task 2 Steps 7-8. This is documentation, not a code task — no new build/verify cycle needed, just accurately describe what the two commits above actually did and what was confirmed on screen. Commit it separately:

```bash
cd /home/deck/os && git add docs/BUILD_LOG.md
git commit -m "$(cat <<'EOF'
docs: BUILD_LOG entry for Unix-like system folders

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```
