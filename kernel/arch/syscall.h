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

/* The C-side half of the syscall ABI. num is the requested syscall
 * (SYS_TEST/SYS_EXIT/anything else -- an unrecognized num returns
 * -1); arg is its one integer argument. Returns the value int 0x80
 * hands back in eax (see ring3.asm's syscall_entry). Pure function:
 * no asm, no hardware access, no other kernel module. */
int syscall_dispatch(int num, int arg);

#endif
