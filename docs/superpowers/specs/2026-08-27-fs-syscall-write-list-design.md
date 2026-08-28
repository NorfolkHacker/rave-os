# More of the fs syscall surface: `fs_create_file` + `fs_list_dir` (userspace sub-project C, second slice)

## Purpose

`docs/superpowers/specs/2026-08-27-fs-syscall-design.md` shipped sub-project
(C)'s first slice, `SYS_READ_FILE` -- deliberately scoped to exactly one
`fs.h` operation, with the hard architectural work (splitting
`syscall_dispatch()` into a pure `syscall_dispatch_core()` plus a real,
`fs.h`-touching `syscall_dispatch()` in a new `kernel/arch/syscall_fs.c`;
an args-struct-over-`ebx` ABI for multi-argument syscalls) done once and
explicitly left for later slices to reuse. This spec is that reuse: two
more `fs.h` operations, `fs_create_file()` and `fs_list_dir()`, added the
same way. No new architecture -- the file split and ABI pattern are
already proven and stay untouched.

## Scope

**In scope:**
- `SYS_CREATE_FILE` (`= 3`), wrapping `fs_create_file(const char *path,
  const void *data, unsigned int size)` (`kernel/fs/fs.h`) with zero
  changes to its behavior or contract.
- `SYS_LIST_DIR` (`= 4`), wrapping `fs_list_dir(const char *path, struct
  fs_dirent *out, unsigned int max_entries, unsigned int *out_count)`
  (`kernel/fs/fs.h`) with zero changes to its behavior or contract.
- Two new args structs (`kernel/arch/syscall.h`), field order/types
  matching each wrapped function's signature exactly, same convention as
  `struct sys_read_file_args`.
- Two new `case`s in `syscall_dispatch()`'s existing switch
  (`kernel/arch/syscall_fs.c`), same shape as `SYS_READ_FILE`'s.
- One-time, throwaway verification: a temporary ring-3 test payload
  (same shape as the first slice's Task 2) creates `/TMP/RING3.TXT` via
  `SYS_CREATE_FILE`, then lists `/TMP` via `SYS_LIST_DIR` and confirms
  the new entry appears -- verified via headless QEMU, then removed
  before shipping.

**Out of scope, deliberately:**
- **Every other remaining `fs.h` function** (`fs_delete`, `fs_rename`,
  `fs_move`, `fs_copy_file`, `fs_append_file`, `fs_create_dir`,
  `fs_path_join`/`fs_path_parent`). Two operations extend the proven
  pattern to cover create+list; the rest is further follow-on work.
- **Any change to `fs_create_file()`/`fs_list_dir()`'s own contracts.**
  Both pass through completely unchanged, including
  `fs_create_file()`'s existing write-once semantics (no delete/overwrite
  syscall exists yet, matching the underlying function itself).
- **gfx, audio, window-management syscalls; a loadable program format;
  per-process address spaces.** Same reasoning the first slice's spec
  already gives -- separate, unstarted future work.
- **Any pointer/bounds validation of ring-3-supplied pointers.**
  Deliberate continuation of the stance both prior syscall sub-projects
  already established.
- **A register-based multi-argument ABI, or any change to `ring3.asm` or
  `syscall_dispatch_core()`.** The existing args-struct convention
  already handles both new operations' argument counts (3 and 4) without
  needing more registers than `SYS_READ_FILE` already required.

## Design

### `kernel/arch/syscall.h` additions

```c
#define SYS_CREATE_FILE 3
#define SYS_LIST_DIR 4

/* Mirrors fs_create_file()'s signature exactly (kernel/fs/fs.h). */
struct sys_create_file_args {
    const char *path;
    const void *data;
    unsigned int size;
};

/* Forward-declared, not included from fs.h: only a pointer to this
 * type appears below, which needs the tag to exist, not its full
 * layout -- confirmed by compiling this exact shape standalone.
 * Keeps syscall.h's own dependency footprint at zero fs.h symbols,
 * same as it already is today; kernel/arch/syscall_fs.c (which
 * already includes fs.h for fs_read_file()) gets the real definition
 * for free when it includes syscall.h. */
struct fs_dirent;

/* Mirrors fs_list_dir()'s signature exactly (kernel/fs/fs.h). */
struct sys_list_dir_args {
    const char *path;
    struct fs_dirent *out;
    unsigned int max_entries;
    unsigned int *out_count;
};
```

### `kernel/arch/syscall_fs.c`

```c
int syscall_dispatch(int num, int arg) {
    if (num == SYS_READ_FILE) {
        const struct sys_read_file_args *a = (const struct sys_read_file_args *)arg;
        return fs_read_file(a->path, a->buf, a->buf_size, a->out_size);
    }
    if (num == SYS_CREATE_FILE) {
        const struct sys_create_file_args *a = (const struct sys_create_file_args *)arg;
        return fs_create_file(a->path, a->data, a->size);
    }
    if (num == SYS_LIST_DIR) {
        const struct sys_list_dir_args *a = (const struct sys_list_dir_args *)arg;
        return fs_list_dir(a->path, a->out, a->max_entries, a->out_count);
    }
    return syscall_dispatch_core(num, arg);
}
```

### Verification: why the proof gates on `SYS_LIST_DIR`, not `SYS_CREATE_FILE`'s return value

`fs_create_file()` is write-once (`kernel/fs/fs.h`: fails `-1` if a file
at the path already exists) and `boot/fs.img` persists across QEMU boots
within a worktree -- the same repeat-boot situation the first slice's own
implementation hit once already (needing a second attempt after a
call-site ordering bug). A re-run of this task's verification against a
`fs.img` that already has `/TMP/RING3.TXT` from an earlier attempt would
see `SYS_CREATE_FILE` correctly return `-1`, which is not a failure of
this syscall -- it is `fs_create_file()`'s documented, correct behavior.

The temporary test payload therefore ignores `SYS_CREATE_FILE`'s return
value entirely and gates success only on `SYS_LIST_DIR` actually finding
the entry:

```c
static void ring3_fs_write_list_payload(void) {
    static char data[] = "HI\n";
    static struct fs_dirent entries[FS_LIST_MAX];
    unsigned int out_count = 0;
    static const struct sys_create_file_args create_args = {
        "/TMP/RING3.TXT", data, sizeof(data) - 1
    };
    static const struct sys_list_dir_args list_args = {
        "/TMP", entries, FS_LIST_MAX, &out_count
    };
    int create_result, list_result;
    unsigned int i;
    int found = 0;

    __asm__ volatile("int $0x80" : "=a"(create_result) : "a"(SYS_CREATE_FILE), "b"(&create_args) : "ecx", "edx", "memory");
    __asm__ volatile("int $0x80" : "=a"(list_result) : "a"(SYS_LIST_DIR), "b"(&list_args) : "ecx", "edx", "memory");
    (void)create_result;

    if (list_result == 0) {
        for (i = 0; i < out_count; i++) {
            if (entries[i].type == FS_TYPE_FILE &&
                entries[i].size_bytes == 3 &&
                entries[i].name[0] == 'R' && entries[i].name[1] == 'I' &&
                entries[i].name[2] == 'N' && entries[i].name[3] == 'G') {
                found = 1;
            }
        }
    }
    if (found) {
        __asm__ volatile("int $0x80" : : "a"(SYS_EXIT), "b"(0) : "ecx", "edx", "memory");
        __asm__ volatile("cli");  /* deliberate: CPL0-only from CPL3 */
    }
    for (;;) { }
}
```

`/TMP` already exists at every boot (`fs_bootstrap_dirs()`, called in
`kmain()` before the point this payload will be placed -- same ordering
lesson the first slice's own bug already taught: place the call site
after whatever real disk state it depends on, not by blindly copying an
earlier slice's placement). `FS_LIST_MAX` (`fs.h`, `FS_MAX_FILES + 1`,
`= 21`) sizes the listing buffer correctly regardless of whether `/DEV`'s
synthetic root-only entry could ever apply (it can't, for `/TMP`, but
sizing to the documented worst case is simpler than reasoning about
which cap applies to which path).

## Testing

**Host-buildable:** no new host test. Same reasoning as `SYS_READ_FILE`
-- both new cases are direct pass-throughs to real disk-touching `fs.h`
functions with no new logic of their own to unit-test in isolation;
`syscall_dispatch_core()` and its existing host test are untouched by
this spec.

**Headless QEMU, one-time and throwaway:** boot with the temporary
payload in place (placed after `fs_bootstrap_dirs()` in `kmain()`, so
`/TMP` is guaranteed to exist), screendump, confirm the same `#GP` banner
(`PANIC: GENERAL PROTECTION FAULT` / `CODE=0x00000000`) both prior
syscall sub-projects have used as their proof tail -- reaching it here is
proof `/TMP/RING3.TXT` was created (this run or a prior one) and found
via a real directory listing. Then remove the temporary payload, rebuild,
and confirm a final regression screendump matches the ordinary desktop.
