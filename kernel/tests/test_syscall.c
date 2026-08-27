/* kernel/tests/test_syscall.c -- host-side only, never linked into
 * kernel.bin. syscall_dispatch_core() has no freestanding-only
 * dependencies (pure switch/return logic, no asm, no other kernel
 * module) -- same convention as test_paging.c/test_gdt.c. The real
 * int 0x80 entry path (ring3.asm's syscall_entry, and syscall_fs.c's
 * syscall_dispatch(), which SYS_READ_FILE touches) can only be
 * verified by actually booting (see this feature's BUILD_LOG entry
 * for the one-time headless-QEMU verification). */
#include <stdio.h>
#include "../arch/syscall.h"

static int failures = 0;

static void check(int cond, const char *msg) {
    if (!cond) {
        printf("FAIL: %s\n", msg);
        failures++;
    }
}

int main(void) {
    check(syscall_dispatch_core(SYS_TEST, 0) == 0x1234, "SYS_TEST should return the fixed value 0x1234");
    check(syscall_dispatch_core(SYS_TEST, 999) == 0x1234, "SYS_TEST should ignore its argument and still return 0x1234");
    check(syscall_dispatch_core(SYS_EXIT, 0) == 0, "SYS_EXIT should return 0");
    check(syscall_dispatch_core(SYS_EXIT, 42) == 0, "SYS_EXIT should return 0 regardless of its argument");
    check(syscall_dispatch_core(42, 0) == -1, "an unknown syscall number should return -1");
    check(syscall_dispatch_core(-1, 0) == -1, "a negative syscall number should return -1");

    if (failures == 0) {
        printf("PASS: all syscall tests passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
