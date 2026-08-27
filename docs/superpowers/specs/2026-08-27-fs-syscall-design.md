# A real syscall surface: `fs_read_file` (userspace sub-project C, first slice)

## Purpose

`docs/IDEAS.md`'s "Real userspace" entry decomposes into (A) paging,
(B) ring 3 + a minimal syscall ABI, (C) a real syscall surface for
existing kernel services (fs, gfx, audio, window management), (D) a
loadable/relocatable program format. (A) and (B) both shipped
2026-08-26/27 (see `docs/superpowers/specs/2026-08-26-paging-design.md`
and `docs/superpowers/specs/2026-08-27-ring3-syscall-design.md`): the
CPU can now execute at CPL 3 and round-trip a trivial `int 0x80` call
carrying one integer argument (`SYS_TEST`/`SYS_EXIT`, neither of which
touches any real kernel service).

**This spec is not all of (C).** Brainstorming flagged that "fs, gfx,
audio, window management" isn't four wrappers of similar size:
`fs.h`'s ~13 functions are a clean, self-contained, thin-wrappable
API; `graphics.h` is similar but raises "what does a ring-3 program
draw into"; `window.h` and the audio drivers have no existing
per-program ownership model at all -- exposing those as syscalls is
real new architecture, not a thin wrapper. This spec scopes down to
exactly one operation, `fs_read_file()`, deliberately following the
same discipline (A) and (B) already established: prove the pattern
with the smallest real, useful slice, defer the rest.

## Scope

**In scope:**
- One new syscall, `SYS_READ_FILE` (`= 2`), wrapping the existing
  `fs_read_file(const char *path, void *buf, unsigned int buf_size,
  unsigned int *out_size)` (`kernel/fs/fs.h`) with zero changes to its
  behavior or contract.
- A new `struct sys_read_file_args` (in `kernel/arch/syscall.h`)
  carrying `fs_read_file()`'s four arguments across the syscall
  boundary as a single pointer -- `ring3.asm`'s ABI (one value in
  `ebx`) is unchanged; that one value is now interpreted as a struct
  pointer for this syscall instead of a plain integer.
- `kernel/arch/syscall.c`'s existing dispatch function, renamed from
  `syscall_dispatch()` to `syscall_dispatch_core()`, otherwise
  byte-identical to what (B) shipped (still exactly `SYS_TEST`/
  `SYS_EXIT`/default, still zero dependency on `fs.h`). A new file,
  `kernel/arch/syscall_fs.c`, defines the real `syscall_dispatch()` --
  the exact name `ring3.asm` already calls, requiring **zero changes
  to `ring3.asm`** -- handling `SYS_READ_FILE` and falling through to
  `syscall_dispatch_core()` for everything else. See Design for why
  this split is necessary, not just tidy.
- One-time, throwaway verification: a temporary ring-3 test payload
  (mirroring (B)'s Task 6 exactly) reads `/BIN/HELLO` -- a file this
  kernel already seeds at every boot (`kernel/kernel.c`:
  `": GREET 42 . CR ;\nGREET\n"`, 24 bytes) -- and compares the
  result against that known content, verified via headless QEMU, then
  removed before shipping.

**Out of scope, deliberately:**
- **Every other `fs.h` function** (`fs_create_file`, `fs_delete`,
  `fs_list_dir`, `fs_rename`, etc.). One operation proves the
  multi-argument pattern; the rest is follow-on work once this lands,
  not a bigger first cut.
- **gfx, audio, window-management syscalls.** Per this spec's own
  Purpose section, these need real new design (a program-owned
  window/canvas concept doesn't exist yet) and are separate future
  sub-projects, not a smaller version of this one.
- **A register-based multi-argument ABI extension.** Considered and
  rejected in brainstorming (Approach B): would require reworking
  `ring3.asm`'s `syscall_entry` register-scratch discipline, the exact
  routine where two real, subtle clobber bugs already surfaced during
  (B)'s implementation (see `docs/BUILD_LOG.md`'s 2026-08-27 entry).
  The args-struct approach this spec uses touches that file not at
  all.
- **A handle/fd abstraction.** Considered and rejected (Approach C):
  `fs.c` has no resource-lifetime concept today; inventing one purely
  to keep every syscall to one scalar argument is solving a problem
  the args-struct approach doesn't have.
- **Any pointer/bounds validation of ring-3-supplied pointers**
  (`path`, `buf`, `out_size`, or the args struct itself). Deliberate
  continuation of (B)'s already-stated "zero isolation, deliberately"
  stance -- `paging_set_user(0, 1)` (B's own primitive, reused
  unchanged by this spec's test) already grants ring-3 code full
  read/write over the same PDE, including the page tables themselves.
  A malformed pointer faults the same way any other CPL0 access to
  unmapped memory already does today -- a crash, not a new class of
  silent corruption.
- **Any change to `fs_read_file()`'s own error contract.** Its
  existing return value (`kernel/fs/fs.h`: `0` on success, `-1` if not
  found, not a file, or too big for `buf_size`) passes through the
  syscall wrapper completely unchanged.
- **Scheduler integration, a loadable program, per-process address
  spaces.** Same reasoning (A)/(B)/(C)'s own Purpose sections already
  give -- this is (D)'s territory, entirely unbuilt.

## Design

### Why the args-struct indirection, not more registers

`ring3.asm`'s `syscall_entry` (unchanged since (B)) currently uses
`ecx` as its own scratch register for reloading the kernel data
selector into DS/ES/FS/GS before calling `syscall_dispatch()`. A
register-based multi-argument ABI (`ebx`/`ecx`/`edx`/`esi` as four
argument registers, the idiomatic approach real syscall ABIs use)
would need that scratch moved to a different register (`edi` is the
only one left unclaimed by either the argument list or the
num/return-value slot) -- a small change, but one made in exactly the
file that has already produced two real bugs
(`docs/superpowers/specs/2026-08-27-ring3-syscall-design.md`'s Testing
section and `docs/BUILD_LOG.md`'s 2026-08-27 entry both document
them). The args-struct approach sidesteps this entirely: `ebx` still
carries exactly one value, exactly like `SYS_TEST`/`SYS_EXIT` already
do, and `ring3.asm` needs no changes at all. The cost is one extra
pointer dereference inside `syscall_dispatch()` -- pure C, not asm,
and not a new *kind* of risk given this spec's Out-of-Scope section
already accepts untrusted pointer dereferencing as this project's
current posture.

### Why this needs a file split, not just a new `case`

`kernel/arch/paging.c`'s pure/real split (`paging_build_directory()` /
`paging_enable()`) and `kernel/arch/gdt.c`'s (`gdt_pack_entry()` /
`gdt_init()`) both keep the pure and real halves in the **same file**,
and their host tests still compile and link that whole file. That
works only because their "real" halves are impure exclusively via
inline asm -- no reference to any symbol outside the file, so the file
still links cleanly on a host toolchain even though the impure
function is never called by the test.

`SYS_READ_FILE`'s impurity is a different kind: it's a plain C
function call to `fs_read_file()`, defined in a wholly separate
translation unit (`kernel/fs/fs.c`). A C linker must resolve every
symbol a translation unit references before producing *any* binary
from it -- including a host test binary that never actually reaches
that code path at runtime. Confirmed empirically: adding a
`fs_read_file()`-calling case directly into the existing
`syscall_dispatch()` and trying to link it against nothing but
`kernel/tests/test_syscall.c` (as (B)'s existing host-test command
already does) fails with `undefined reference to 'fs_read_file'` --
the same class of problem `syscall_init()` deliberately avoided during
(B)'s own plan-writing by keeping `idt_set_gate()`/`syscall_entry` out
of `syscall.c` entirely. The pure/impure boundary here has to be a
**file** boundary, not just a function boundary within one file.

### `kernel/arch/syscall.h`

```c
#define SYS_READ_FILE 2

/* Carries fs_read_file()'s four arguments across the syscall boundary
 * as a single pointer -- ring3.asm's ABI still passes exactly one
 * value in ebx; for SYS_READ_FILE, that value is the address of one
 * of these, built by the caller in its own memory. Field types and
 * order match fs_read_file()'s own signature exactly (kernel/fs/fs.h)
 * -- this struct exists only to fit four arguments through one
 * register, not to add or reinterpret any of them. */
struct sys_read_file_args {
    const char *path;
    void *buf;
    unsigned int buf_size;
    unsigned int *out_size;
};

/* Pure -- exactly what (B) shipped as syscall_dispatch(), renamed.
 * SYS_TEST/SYS_EXIT/default only, zero dependency on fs.h or any
 * other kernel module. This is what kernel/tests/test_syscall.c
 * links and calls directly. */
int syscall_dispatch_core(int num, int arg);

/* Real -- defined in syscall_fs.c, not syscall.c. This is the exact
 * name ring3.asm's syscall_entry already calls; giving the real
 * dispatcher this name in a different file means ring3.asm needs no
 * changes at all. Handles SYS_READ_FILE, falls through to
 * syscall_dispatch_core() for everything else. */
int syscall_dispatch(int num, int arg);
```

### `kernel/arch/syscall.c` (renamed function, otherwise unchanged from (B))

```c
int syscall_dispatch_core(int num, int arg) {
    (void)arg;
    switch (num) {
        case SYS_TEST:
            return 0x1234;
        case SYS_EXIT:
            return 0;
        default:
            return -1;
    }
}
```

### `kernel/arch/syscall_fs.c` (new file)

```c
#include "syscall.h"
#include "fs.h"

int syscall_dispatch(int num, int arg) {
    if (num == SYS_READ_FILE) {
        const struct sys_read_file_args *a = (const struct sys_read_file_args *)arg;
        return fs_read_file(a->path, a->buf, a->buf_size, a->out_size);
    }
    return syscall_dispatch_core(num, arg);
}
```

`ring3.asm` is untouched: its `extern syscall_dispatch` / `call
syscall_dispatch` now resolve to this file's definition instead of
`syscall.c`'s (the linker doesn't care which `.o` a symbol comes from)
-- the asm-level ABI, register discipline, and everything (B) already
verified about `syscall_entry` are completely unaffected. Verified by
hand-linking a throwaway three-file reproduction of exactly this shape
(`syscall.c` + `syscall_fs.c` + a stub `fs_read_file()`): the host
test's link command (`syscall.c` alone) succeeds with zero undefined
symbols, and the full chain (`syscall_dispatch()` in `syscall_fs.c`
falling through to `syscall_dispatch_core()` in `syscall.c`) returns
the correct `SYS_TEST` value end-to-end.

### Verification: the temporary ring-3 payload

Same shape as (B)'s Task 6 (add, verify, remove within one task) --
not a new technique, reusing the established one:

```c
static void ring3_test_payload(void) {
    static char buf[64];
    unsigned int out_size = 0;
    static const struct sys_read_file_args args = {
        "/BIN/HELLO", buf, sizeof(buf), &out_size
    };
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(SYS_READ_FILE), "b"(&args) : "ecx", "edx", "memory");
    if (result >= 0 && out_size == 24 &&
        buf[0] == ':' && buf[1] == ' ' && buf[2] == 'G' && buf[3] == 'R') {
        __asm__ volatile("int $0x80" : : "a"(SYS_EXIT), "b"(0) : "ecx", "edx", "memory");
        __asm__ volatile("cli");  /* deliberate: CPL0-only from CPL3 -- same proof tail as (B) */
    }
    for (;;) { }
}
```

The success check is deliberately loose (`out_size` plus four leading
bytes, not a full 24-byte comparison) -- enough to prove real content
crossed the boundary intact without hand-transcribing the whole
literal a second time into a comparison the plan would have to keep
byte-for-byte in sync with `kernel.c`'s own seed string. Reaching the
deliberate `cli` still requires the real read to have succeeded;
`out_size` uninitialized-or-wrong and a garbage `buf` are exactly what
a broken syscall path would produce, and either falls into the same
distinguishable infinite-loop/black-screen fallback (B) already
established. As in (B), this payload and its call site are added,
screendump-verified, then fully removed before this task ships --
`SYS_READ_FILE`, the struct, `syscall_dispatch_core()`'s rename, and
`syscall_fs.c`'s `syscall_dispatch()` are the only things that stay
permanent.

## Testing

**Host-buildable:** `kernel/tests/test_syscall.c` is updated to call
`syscall_dispatch_core()` instead of `syscall_dispatch()` (the rename
this spec makes) -- its `SYS_TEST`/`SYS_EXIT`/default-case assertions
are otherwise byte-identical to what (B) left them, and its build
command is unchanged (`gcc -m32 -Wall -Wextra -o /tmp/test_syscall
kernel/tests/test_syscall.c kernel/arch/syscall.c && /tmp/test_syscall`
-- still linking only `syscall.c`, never `syscall_fs.c`). No new host
test is added for `SYS_READ_FILE`/`syscall_fs.c` -- there is nothing
pure to test in isolation there (the struct itself is a plain data
layout with no packing/logic of its own, unlike `gdt_pack_entry()`,
and its one real line of logic is a direct pass-through to
`fs_read_file()`). Confirmed: no `kernel/tests/test_fs.c` exists, and
no existing host test references `fs_read_file()` -- `fs.c` has never
been host-tested in this codebase (it depends on `ata.c`'s real disk
I/O throughout), only ever verified live via `fs_selftest()` at boot.
This spec adds no new fs-layer logic, only a pass-through, so
`syscall_fs.c` inherits that same live-only verification bar rather
than establishing a new one.

**Headless QEMU, one-time and throwaway:** boot with the temporary
payload from the Design section in place, screendump, and confirm the
same red `#GP` banner (B) verified (`PANIC: GENERAL PROTECTION FAULT`
/ `CODE=0x00000000`) -- reaching it is proof `/BIN/HELLO`'s real,
on-disk content came back through `SYS_READ_FILE` correctly. Then
remove the temporary payload, rebuild, and confirm a final regression
screendump matches the ordinary desktop -- proving `syscall_fs.c`'s
real dispatcher being wired in (via `ring3.asm`'s already-existing,
unchanged `extern`) is a true no-op for the existing boot path, the
same bar every prior sub-project in this series has held its own
additions to.
