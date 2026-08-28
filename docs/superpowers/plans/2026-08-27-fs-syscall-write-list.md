# `fs_create_file` + `fs_list_dir` Syscalls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend RaveOS's syscall surface with two more real filesystem operations — a ring-3 program can create a new file and list a directory's contents via `int 0x80`, proven end-to-end with a one-time headless-QEMU verification.

**Architecture:** No new architecture — this reuses the pure/real dispatcher split and args-struct-over-`ebx` ABI already shipped in sub-project (C)'s first slice (`SYS_READ_FILE`). Two more syscall numbers, two more args structs, two more `case`s in `kernel/arch/syscall_fs.c`'s existing dispatch.

**Tech Stack:** C (`i686-elf-gcc`, freestanding), QEMU (headless verification via monitor socket + `socat`).

**Spec:** `docs/superpowers/specs/2026-08-27-fs-syscall-write-list-design.md`

## Global Constraints

- `SYS_CREATE_FILE` must equal `3`, `SYS_LIST_DIR` must equal `4` (`SYS_TEST`=0, `SYS_EXIT`=1, `SYS_READ_FILE`=2 already exist).
- `struct sys_create_file_args` fields, in order: `const char *path; const void *data; unsigned int size;` — matching `fs_create_file()`'s signature (`kernel/fs/fs.h`) exactly.
- `struct sys_list_dir_args` fields, in order: `const char *path; struct fs_dirent *out; unsigned int max_entries; unsigned int *out_count;` — matching `fs_list_dir()`'s signature (`kernel/fs/fs.h`) exactly.
- `struct fs_dirent` must be forward-declared in `kernel/arch/syscall.h` (`struct fs_dirent;`), NOT pulled in via `#include "fs.h"` — keeps `syscall.h` at zero `fs.h` dependency, matching its existing convention. Confirmed during spec-writing: a forward declaration is sufficient for a pointer-typed struct field and compiles cleanly.
- `kernel/arch/syscall.c`, `kernel/arch/ring3.asm`, and `kernel/tests/test_syscall.c` must NOT be modified by this plan at all — no new host-testable logic is introduced (both new syscalls are direct pass-throughs to real disk-touching `fs.h` functions).
- `i686-elf-gcc`/`i686-elf-ld`/`i686-elf-objcopy` are NOT on the default PATH in this environment — every kernel/boot build must prefix `export PATH="$HOME/opt/cross/bin:$PATH"` first.
- Every kernel rebuild + headless-QEMU check: `cd boot && make` (rebuilds `kernel.bin` and `disk.img` together), then boot with `-display none -monitor unix:/tmp/qemu-mon.sock,server,nowait`, `screendump` over that socket via `socat`, convert the `.ppm` to `.png` with `python3`/Pillow, and view it.

---

## Task 1: add `SYS_CREATE_FILE`/`SYS_LIST_DIR` to the dispatcher

**Files:**
- Modify: `kernel/arch/syscall.h`
- Modify: `kernel/arch/syscall_fs.c`

**Interfaces:**
- Consumes: `fs_create_file(const char *path, const void *data, unsigned int size)` and `fs_list_dir(const char *path, struct fs_dirent *out, unsigned int max_entries, unsigned int *out_count)` (both `kernel/fs/fs.h`, pre-existing, unchanged). `syscall_dispatch_core()` (`kernel/arch/syscall.c`, pre-existing, unchanged — the new cases fall through to it exactly like `SYS_READ_FILE` already does).
- Produces: `SYS_CREATE_FILE` (`= 3`), `SYS_LIST_DIR` (`= 4`), `struct sys_create_file_args`, `struct sys_list_dir_args` (all in `syscall.h`). Task 2 uses all four directly in its temporary test payload.

No host-testable logic is added by this task (both new syscalls are direct pass-throughs to real disk I/O) — there is no failing-test step here; this task's own verification is a clean cross-compiler build plus a headless-QEMU regression, matching how sub-project (B)'s wiring-only tasks were verified.

- [ ] **Step 1: Add the two syscall numbers and args structs**

In `kernel/arch/syscall.h`, add after the existing `struct sys_read_file_args` block (before the `syscall_dispatch_core()` declaration):

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
 * layout. Keeps syscall.h's own dependency footprint at zero fs.h
 * symbols, same as it already is today; kernel/arch/syscall_fs.c
 * (which already includes fs.h for fs_read_file()) gets the real
 * definition for free when it includes syscall.h. */
struct fs_dirent;

/* Mirrors fs_list_dir()'s signature exactly (kernel/fs/fs.h). */
struct sys_list_dir_args {
    const char *path;
    struct fs_dirent *out;
    unsigned int max_entries;
    unsigned int *out_count;
};
```

- [ ] **Step 2: Wire the dispatch**

In `kernel/arch/syscall_fs.c`, replace:

```c
int syscall_dispatch(int num, int arg) {
    if (num == SYS_READ_FILE) {
        const struct sys_read_file_args *a = (const struct sys_read_file_args *)arg;
        return fs_read_file(a->path, a->buf, a->buf_size, a->out_size);
    }
    return syscall_dispatch_core(num, arg);
}
```

with:

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

- [ ] **Step 3: Rebuild and confirm a clean build**

Run:
```
export PATH="$HOME/opt/cross/bin:$PATH"
cd kernel && make clean && make 2>&1 | tail -20
```
Expected: builds `kernel.bin` with no warnings beyond the pre-existing linker warning `kernel.elf has a LOAD segment with RWX permissions`.

Also confirm the existing host test still builds and passes unchanged (this task doesn't touch `syscall.c`/`test_syscall.c`, so this is a pure regression check):
```
gcc -m32 -Wall -Wextra -o /tmp/test_syscall kernel/tests/test_syscall.c kernel/arch/syscall.c && /tmp/test_syscall
```
Expected: `PASS: all syscall tests passed`, zero warnings.

- [ ] **Step 4: Headless-QEMU regression check**

Nothing new is invoked yet — `SYS_CREATE_FILE`/`SYS_LIST_DIR` exist in `syscall_dispatch()`'s permanent switch, but nothing calls `int 0x80` with either number anywhere in the shipped tree. This confirms the two new cases are a true no-op for the existing boot path.

Run:
```bash
cd /home/norfolkh/os/boot
make
rm -f /tmp/qemu-mon.sock
qemu-system-i386 -accel kvm -display none -monitor unix:/tmp/qemu-mon.sock,server,nowait -device sb16 -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 &
sleep 3
echo "screendump /tmp/shot.ppm" | socat - unix-connect:/tmp/qemu-mon.sock
python3 -c "from PIL import Image; Image.open('/tmp/shot.ppm').save('/tmp/shot.png')"
echo "quit" | socat - unix-connect:/tmp/qemu-mon.sock
```
Then view `/tmp/shot.png`.
Expected: the normal desktop — black backdrop, green cursor square, taskbar's green rule and `MENU` label at the bottom, no red panic banner, no hang.

- [ ] **Step 5: Commit**

```bash
git add kernel/arch/syscall.h kernel/arch/syscall_fs.c
git commit -m "kernel: add SYS_CREATE_FILE/SYS_LIST_DIR to the syscall dispatcher"
```

---

## Task 2: the ring-3 create+list proof, cleanup, and closeout

**Files:**
- Modify: `kernel/kernel.c` (temporarily, then reverted within this same task)
- Modify: `docs/BUILD_LOG.md`
- Modify: `docs/IDEAS.md`

**Interfaces:**
- Consumes: `SYS_CREATE_FILE`/`SYS_LIST_DIR`/`struct sys_create_file_args`/`struct sys_list_dir_args` (Task 1), `enter_ring3()`/`paging_set_user()` (sub-project B, unchanged), `SYS_EXIT` (sub-project B, unchanged).
- Produces: nothing new and permanent in code — this task's own temporary code is added and then removed within itself, proving the two new syscalls actually work before this slice is declared done.

- [ ] **Step 1: Add the temporary ring-3 test payload**

In `kernel/kernel.c`, add `#include "syscall.h"` to the include list (after `#include "idt.h"`, alongside the other arch-level includes) — this is temporary; see Step 4.

Immediately after `fs_bootstrap_dirs();` in `kmain()` (this task's payload depends on `/TMP` existing, which that call guarantees — placing it any earlier would repeat the exact ordering bug the first slice's own implementation hit), add:

```c
    /* TEMPORARY -- one-time proof that SYS_CREATE_FILE/SYS_LIST_DIR
     * work, removed later in this same task's own history (see
     * docs/superpowers/specs/2026-08-27-fs-syscall-write-list-design.md's
     * Testing section). Not present in the final state of this file. */
    {
        extern void enter_ring3(void (*entry)(void), void *user_stack_top);
        static uint8_t ring3_fs_write_list_stack[1024] __attribute__((aligned(16)));
        paging_set_user(0, 1);
        enter_ring3(ring3_fs_write_list_payload, ring3_fs_write_list_stack + sizeof(ring3_fs_write_list_stack));
    }
```

and, above `kmain()`'s own definition, add the payload function it references:

```c
/* TEMPORARY -- see the call site inside kmain(). Runs entirely at
 * CPL 3. Creates /TMP/RING3.TXT via SYS_CREATE_FILE, then lists /TMP
 * via SYS_LIST_DIR and looks for it. SYS_CREATE_FILE's own return
 * value is deliberately ignored: fs_create_file() is write-once, and
 * fs.img persists across QEMU boots within a worktree, so a re-run
 * seeing -1 ("already exists") is correct, expected behavior, not a
 * failure -- the listing is the real, definitive proof either this
 * run's or an earlier run's create actually worked. Reaching the
 * deliberate cli requires the entry to actually be found -- a broken
 * syscall path instead falls into the infinite loop below, a
 * permanently black screen on screendump, clearly distinguishable
 * from the expected #GP banner. */
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

- [ ] **Step 2: Rebuild and confirm a clean build**

Run: `export PATH="$HOME/opt/cross/bin:$PATH"; cd kernel && make clean && make 2>&1 | tail -30`
Expected: builds `kernel.bin` with no warnings beyond the pre-existing RWX-segment linker warning.

- [ ] **Step 3: Headless-QEMU proof screendump**

Run:
```bash
cd /home/norfolkh/os/boot
make
rm -f /tmp/qemu-mon.sock
qemu-system-i386 -accel kvm -display none -monitor unix:/tmp/qemu-mon.sock,server,nowait -device sb16 -drive file=disk.img,format=raw,if=ide,bus=0,unit=0 -drive file=fs.img,format=raw,if=ide,bus=0,unit=1 &
sleep 3
echo "screendump /tmp/shot.ppm" | socat - unix-connect:/tmp/qemu-mon.sock
python3 -c "from PIL import Image; Image.open('/tmp/shot.ppm').save('/tmp/shot.png')"
echo "quit" | socat - unix-connect:/tmp/qemu-mon.sock
```
Then view `/tmp/shot.png`.
Expected: the red `#GP` panic banner reading `PANIC: GENERAL PROTECTION FAULT` / `CODE=0x00000000` — reaching it is proof `/TMP/RING3.TXT` was created (this run or an earlier one against the same `fs.img`) and found via a real directory listing.

If instead the screen is black (not the desktop, not a banner): the `for (;;) {}` fallback was hit, meaning either `SYS_CREATE_FILE` or `SYS_LIST_DIR` did not round-trip correctly — do not proceed to Step 4; debug the syscall path (check `syscall_fs.c`'s two new cases, the args structs' field order, and `fs_create_file()`/`fs_list_dir()`'s own return contracts) before continuing.

- [ ] **Step 4: Remove the temporary code**

Revert exactly the additions from Step 1 — delete the `{ ... }` block added after `fs_bootstrap_dirs();`, delete the `ring3_fs_write_list_payload()` function, and delete the `#include "syscall.h"` line you added (nothing permanent in `kernel.c` needs it — same lesson the first slice's own closeout already established: leaving a now-dead include in is the exact class of finding a prior final review had to catch once).

Confirm with:

Run: `git diff kernel/kernel.c`
Expected: empty.

- [ ] **Step 5: Rebuild and final regression screendump**

Run: `export PATH="$HOME/opt/cross/bin:$PATH"; cd kernel && make clean && make 2>&1 | tail -30` (expect clean build), then repeat Step 3's exact QEMU sequence.
Expected: back to the normal desktop, identical to Task 1 Step 4's screendump — confirming removal was clean and the two new syscalls being wired into the permanent switch are still a true no-op with nothing invoking them.

- [ ] **Step 6: `docs/BUILD_LOG.md` entry**

Append a new dated entry (`## 2026-08-27 -- More of the fs syscall surface: fs_create_file + fs_list_dir (userspace sub-project C, second slice)`) summarizing: the two new syscalls and that no new architecture was needed (the pure/real split and args-struct ABI from the first slice covered this directly), the forward-declaration decision for `struct fs_dirent` and why, the write-once/persistent-`fs.img` reasoning behind gating the proof on `SYS_LIST_DIR` rather than `SYS_CREATE_FILE`'s own return value, and the Step 3 screendump's exact banner text as the verification evidence — following the same structure and level of detail as the `2026-08-27` fs-syscall (first slice) entry elsewhere in the same file.

- [ ] **Step 7: `docs/IDEAS.md` update**

In the "Real userspace" entry (the paragraph that already marks (C)'s first slice, `SYS_READ_FILE`, done), add a paragraph in the same style marking this second slice done as of 2026-08-27: what shipped (`SYS_CREATE_FILE`, `SYS_LIST_DIR`), and that this is still not the rest of `fs.h`'s surface (`fs_delete`, `fs_rename`, `fs_move`, `fs_copy_file`, `fs_append_file`, `fs_create_dir` all remain unwrapped) — plus gfx/audio/window-management syscalls and a loadable program format all still entirely unbuilt, same as before.

- [ ] **Step 8: Commit**

```bash
git add docs/BUILD_LOG.md docs/IDEAS.md
git commit -m "kernel: prove SYS_CREATE_FILE/SYS_LIST_DIR end-to-end; close out sub-project (C)'s second slice"
```

(Note: if Step 4's `git diff kernel/kernel.c` was empty, there's nothing further to stage from `kernel.c` itself — Task 1's commit already carries the permanent code changes. This final commit is docs-only, matching how every prior sub-project/slice in this series has closed out.)
