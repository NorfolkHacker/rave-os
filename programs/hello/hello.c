/* The first ring-3 program that exists as a genuinely separate,
 * on-disk file -- every prior syscall proof was a C function compiled
 * directly into kernel.c. Freestanding, no libc, and deliberately does
 * NOT include kernel/arch/syscall.h (that header is kernel-internal,
 * never meant to be linked into a program built this way) -- SYS_TEST/
 * SYS_EXIT are re-declared locally as the same fixed numbers
 * ring3.asm's ABI already documents, matching how every kernel-side
 * proof payload's own inline asm already does this.
 *
 * void _start(void), not int main(void): there is no C runtime here to
 * call main() or do anything with a return value -- program_load_and_run()
 * (kernel/kernel.c) enters this function directly via enter_ring3(). */
void _start(void) {
    int result;

    __asm__ volatile("int $0x80" : "=a"(result) : "a"(0) /* SYS_TEST */, "b"(0) : "ecx", "edx", "memory");

    if (result == 0x1234) { /* SYS_TEST's documented fixed return value */
        __asm__ volatile("int $0x80" : : "a"(1) /* SYS_EXIT */, "b"(0) : "ecx", "edx", "memory");
        __asm__ volatile("cli");  /* deliberate: CPL0-only from CPL3 -- proves this really ran in ring 3 */
    }

    for (;;) { }
}
