#ifndef RAVEOS_SYSCALL_H
#define RAVEOS_SYSCALL_H

/* Returns a fixed, recognizable value -- pure plumbing, proves a
 * syscall's argument and return value both cross the ring3/ring0
 * boundary intact. Ignores arg. */
#define SYS_TEST 0

/* Reserved syscall number for "the caller is done"; currently just
 * returns 0 and does nothing else -- this minimal ABI has no
 * process/address-space/scheduler-slot concept to actually tear down
 * (see docs/superpowers/specs/2026-08-27-ring3-syscall-design.md's
 * Out of Scope), so this is not real process termination. Ignores arg. */
#define SYS_EXIT 1

/* Reads an existing file's contents into a ring-3-owned buffer,
 * wrapping fs_read_file() (kernel/fs/fs.h) with zero changes to its
 * behavior. arg is the address of a struct sys_read_file_args built
 * by the caller -- ring3.asm's ABI still passes exactly one value in
 * ebx; for this syscall, that value is a pointer instead of a plain
 * integer. */
#define SYS_READ_FILE 2

/* Carries fs_read_file()'s four arguments across the syscall boundary
 * as a single pointer. Field types and order match fs_read_file()'s
 * own signature exactly (kernel/fs/fs.h) -- this struct exists only
 * to fit four arguments through one register, not to add or
 * reinterpret any of them. */
struct sys_read_file_args {
    const char *path;
    void *buf;
    unsigned int buf_size;
    unsigned int *out_size;
};

/* Creates a new file with the given contents, wrapping fs_create_file()
 * (kernel/fs/fs.h) with zero changes to its behavior -- including its
 * write-once semantics (fails if the path already exists). arg is the
 * address of a struct sys_create_file_args built by the caller. */
#define SYS_CREATE_FILE 3

/* Lists a directory's entries, wrapping fs_list_dir()
 * (kernel/fs/fs.h) with zero changes to its behavior. arg is the
 * address of a struct sys_list_dir_args built by the caller. */
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

/* Pure -- exactly what sub-project (B) shipped as syscall_dispatch(),
 * renamed. SYS_TEST/SYS_EXIT/default only, zero dependency on fs.h or
 * any other kernel module. This is what kernel/tests/test_syscall.c
 * links and calls directly. */
int syscall_dispatch_core(int num, int arg);

/* Real -- defined in syscall_fs.c, not syscall.c. This is the exact
 * name ring3.asm's syscall_entry already calls; giving the real
 * dispatcher this name in a different file means ring3.asm needs no
 * changes at all. Handles the fs.h-backed syscalls (SYS_READ_FILE,
 * SYS_CREATE_FILE, SYS_LIST_DIR), falls through to
 * syscall_dispatch_core() for everything else. eax carries the
 * syscall number (num) and ebx the argument (arg) across int 0x80;
 * the return value here is what eax holds when it returns (see
 * ring3.asm's syscall_entry). */
int syscall_dispatch(int num, int arg);

#endif
