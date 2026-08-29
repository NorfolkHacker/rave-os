# A loadable, fixed-address flat-binary program format (userspace sub-project D)

## Purpose

`docs/IDEAS.md`'s "Real userspace" entry names sub-project (D) as the
last remaining piece: "some real notion of a loadable/relocatable
program image separate from being baked into the kernel binary."
Every ring-3 proof so far -- sub-projects (B) and (C)'s nine syscalls
across fs/gfx/audio/window -- has run as a C function compiled
directly into `kernel.c` itself, entered via `enter_ring3()`. This
spec is the first ring-3 code that exists as a genuinely separate,
on-disk file: compiled independently, read from the filesystem at
runtime via the fs syscalls' own machinery, and only then executed.

Investigated and rejected up front: a *relocatable* format (a header +
relocation table a loader parses and patches, closer to a minimal ELF
or a.out) so a program could load at any free address. RaveOS has no
malloc/heap anywhere -- the scheduler uses fixed static stacks, there
is exactly one static page directory -- every existing subsystem
favors simple, static, fully-understood mechanisms over general
infrastructure a real relocatable loader would need (a free-address
allocator, at minimum). This spec instead picks one fixed load
address once, matching that same philosophy. True relocation stays a
named, deferred idea if this codebase ever needs more than one loaded
program alive at a time -- not needed for a first slice.

## Scope

**In scope:**
- A fixed load address for ring-3 programs, `0x00200000` (2MB),
  chosen by checking (via `nm` on a real build) that the kernel's own
  `.bss` ends at `0x440b0` -- comfortably below it, well inside the
  one 4MB page directory entry (`PDE 0`) `paging_set_user()` has ever
  made user-accessible.
- `programs/hello/`: a new top-level directory holding the first real
  standalone ring-3 program -- `hello.c`, a dedicated linker script
  fixing its origin to the load address above, and a small `Makefile`
  using the existing `i686-elf-gcc`/`i686-elf-ld`/`i686-elf-objcopy`
  cross-toolchain (same flags `kernel/Makefile` already uses) to
  produce a raw flat binary via `objcopy -O binary`, the identical
  technique `kernel.bin` itself already uses.
- `program_load_and_run(const char *path)`, a new kernel-side function
  (not a syscall -- loading a program is something ring 0 does, not
  something a ring-3 program calls on itself): reads `path` via the
  already-shipped `fs_read_file()` straight into the fixed load
  address, then `enter_ring3()`s into it.
- Seeding the compiled binary onto `fs.img` as `/BIN/USERPROG.BIN` via
  `fs_create_file()` in `kmain()`, the same way `/BIN/HELLO`'s Forth
  script is already seeded -- this repo has no host-side disk-image
  tooling, so a compiled-in byte array is how the demo binary gets
  onto disk at all. `program_load_and_run()` itself is generic and
  reads whatever real file is at the path it's given.
- One-time, throwaway verification: `hello.c` calls `SYS_TEST` (proving
  it is really executing as loaded, independently-compiled code, not
  code linked into the kernel binary -- a return-value check that
  would pass just as well from inlined kernel code would prove
  nothing new), then falls into the same deliberate `#GP` proof tail
  every prior sub-project has used.

**Out of scope, deliberately:**
- **True relocation, position-independent loading, or more than one
  loaded program.** See Purpose above -- explicitly deferred.
- **Any change to `enter_ring3()`/`ring3.asm`.** The loader calls
  `enter_ring3()` exactly the way every prior proof already has;
  nothing here needs a new ABI primitive.
- **Wiring loading into a real user-facing command** (e.g. a `SHELL`
  "RUN" verb for binaries, mirroring the existing Forth-script `RUN`).
  `program_load_and_run()` ships as permanent, reusable plumbing, but
  this slice proves it end to end the same throwaway-payload way every
  prior slice has -- a real SHELL integration is separate future work,
  matching how none of the fs/gfx/audio/window syscalls got UI wiring
  either.
- **Any pointer/bounds validation, or a maximum-size check on the
  loaded file.** Same stance every prior syscall sub-project has
  taken; `fs_read_file()`'s own `buf_size` argument is passed generously
  (matching the free space actually available below `0x400000`), so
  an oversized file fails cleanly via `fs_read_file()`'s own existing
  "too big for the caller's buffer" contract rather than corrupting
  memory.
- **Any change to `fs.h` itself.** `program_load_and_run()` is a pure
  consumer of `fs_read_file()`'s existing, unchanged contract.

## Design

### `programs/hello/hello.ld`

```ld
ENTRY(_start)
SECTIONS {
    . = 0x00200000;
    .text   : { *(.text) }
    .rodata : { *(.rodata) }
    .data   : { *(.data) }
    .bss    : { *(.bss) *(COMMON) }
    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note.*) }
}
```

### `programs/hello/hello.c`

```c
/* Freestanding, no libc, no syscall.h include (that header is
 * kernel-internal) -- SYS_TEST/SYS_EXIT are re-declared locally as
 * the same fixed numbers ring3.asm's ABI already documents, the same
 * way every kernel-side proof payload's own inline asm already does.
 * void _start(void), not int main(void): there is no C runtime here
 * to call main() or do anything with a return value. */
void _start(void) {
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(0) /* SYS_TEST */, "b"(0) : "ecx", "edx", "memory");
    if (result == 0x1234) { /* SYS_TEST's documented fixed return value */
        __asm__ volatile("int $0x80" : : "a"(1) /* SYS_EXIT */, "b"(0) : "ecx", "edx", "memory");
        __asm__ volatile("cli");  /* deliberate: CPL0-only from CPL3 */
    }
    for (;;) { }
}
```

### `programs/hello/Makefile`

Mirrors `kernel/Makefile`'s own flags (`-ffreestanding -fno-pie
-fno-stack-protector -mgeneral-regs-only -nostdlib`), links against
`hello.ld` instead of `kernel/arch/linker.ld`, and runs `objcopy -O
binary` to produce `hello.bin` -- a raw flat binary with no ELF
headers, loadable exactly as-is at the fixed address `hello.ld`
already committed it to.

### `kernel/kernel.c` additions

```c
/* Reads path's real, on-disk content via fs_read_file() straight into
 * the fixed ring-3 load address, then enters it. Not a syscall --
 * ring 0 calls this directly (see this slice's own temporary proof
 * payload for the only current caller); there is no notion yet of a
 * ring-3 program loading another one. */
#define PROGRAM_LOAD_ADDR 0x00200000
#define PROGRAM_LOAD_MAX_SIZE (0x00400000 - PROGRAM_LOAD_ADDR) /* everything free below PDE 0's 4MB end */

void program_load_and_run(const char *path) {
    unsigned int out_size;
    static uint8_t program_stack[4096] __attribute__((aligned(16)));
    extern void enter_ring3(void (*entry)(void), void *user_stack_top);

    if (fs_read_file(path, (void *)PROGRAM_LOAD_ADDR, PROGRAM_LOAD_MAX_SIZE, &out_size) != 0) {
        return;
    }
    paging_set_user(0, 1);
    enter_ring3((void (*)(void))PROGRAM_LOAD_ADDR,
                program_stack + sizeof(program_stack));
}
```

`hello.bin`'s compiled bytes are embedded as a `static const unsigned
char hello_bin[] = { ... };` byte array (generated from the real build
output, not hand-written) and seeded via `fs_create_file("/BIN/USERPROG.BIN",
hello_bin, sizeof(hello_bin))` in `kmain()`, alongside the existing
`/BIN/HELLO` seeding -- both write-once, both silent no-ops on every
boot after the first.

## Testing

**Host-buildable:** no new host test -- `program_load_and_run()` is a
thin sequence of two existing, already-tested primitives
(`fs_read_file()`, `enter_ring3()`), the same reasoning every prior
syscall slice used for its own thin wrappers.

**`programs/hello/` builds standalone**, verified with the same
cross-toolchain command used for every host/cross build in this repo
(`export PATH="$HOME/opt/cross/bin:$PATH"`), producing a flat
`hello.bin` with a fixed entry point at `0x00200000` (confirmed via
`objdump`/`nm` on `hello.elf` before the final `objcopy` strips it to
raw bytes).

**Headless QEMU, one-time and throwaway**, same technique every prior
entry has used: `program_load_and_run("/BIN/USERPROG.BIN")` called
from a temporary `kmain()` call site (after `fs_bootstrap_dirs()` and
the `/BIN/USERPROG.BIN` seeding, same ordering lesson every prior
proof has already learned), screendump, confirm the same `#GP` banner
(`PANIC: GENERAL PROTECTION FAULT` / `CODE=0x00000000`) every prior
sub-project has used as proof -- reaching it here requires
`hello.bin`'s own compiled code, loaded fresh from a real on-disk
file at runtime, to have actually executed and gotten `SYS_TEST`'s
correct return value, not just that the loader's own plumbing ran.
Then remove the temporary call site, rebuild, and confirm a final
regression screendump matches the ordinary desktop -- confirming
`program_load_and_run()`/the `/BIN/USERPROG.BIN` seeding being wired
in but uncalled are a true no-op.
