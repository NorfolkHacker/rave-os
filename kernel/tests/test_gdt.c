/* kernel/tests/test_gdt.c -- host-side only, never linked into
 * kernel.bin. gdt_pack_entry() has no freestanding-only dependencies
 * (pure struct-packing logic, no asm), so it compiles and runs
 * natively here exactly as it will inside kernel.bin -- same
 * convention as test_paging.c. gdt_init() itself (real lgdt/ltr/
 * segment-register access) is NOT exercised here; it can only be
 * verified by actually booting (see this feature's BUILD_LOG entry
 * for the one-time headless-QEMU verification).
 *
 * Compile with: gcc -m32 -fno-pie -no-pie -Wall -Wextra
 * The -fno-pie -no-pie flags are necessary: gdt_init()'s ljmp $0x08, $1f
 * (a far jump to reload CS via the new GDT) requires an absolute-address
 * relocation. This is correct x86 code, but when compiling gdt.c into a
 * hosted PIE executable, the linker warns about DT_TEXTREL. The real
 * kernel build avoids this via -fno-pie -nostdlib; this test must match. */
#include <stdio.h>
#include <string.h>
#include "../arch/gdt.h"

static int failures = 0;

static void check(int cond, const char *msg) {
    if (!cond) {
        printf("FAIL: %s\n", msg);
        failures++;
    }
}

int main(void) {
    struct gdt_entry e;

    /* Flat kernel-code-shaped entry: base 0, limit 4GB, 4KB-granular. */
    memset(&e, 0xAA, sizeof(e));
    gdt_pack_entry(&e, 0, 0xFFFFFFFF, 0x9A, 0xC0);
    check(e.base_low == 0 && e.base_mid == 0 && e.base_high == 0, "base 0 should pack to all-zero base fields");
    check(e.limit_low == 0xFFFF, "limit 0xFFFFFFFF should pack limit_low = 0xFFFF");
    check(e.granularity == 0xCF, "limit 0xFFFFFFFF + gran 0xC0 should pack granularity byte to 0xCF");
    check(e.access == 0x9A, "access byte should pass through unchanged");

    /* Non-zero base, small byte-granular limit -- the TSS descriptor's shape. */
    memset(&e, 0xAA, sizeof(e));
    gdt_pack_entry(&e, 0x12345678, 0x67, 0x89, 0x00);
    check(e.base_low == 0x5678, "base 0x12345678: base_low should be 0x5678");
    check(e.base_mid == 0x34, "base 0x12345678: base_mid should be 0x34");
    check(e.base_high == 0x12, "base 0x12345678: base_high should be 0x12");
    check(e.limit_low == 0x67, "limit 0x67: limit_low should be 0x67");
    check(e.granularity == 0x00, "limit 0x67 (bits 19:16 = 0) + gran 0x00: granularity byte should be 0x00");
    check(e.access == 0x89, "access byte should pass through unchanged");

    /* gran's low nibble must be discarded, not merged in. */
    memset(&e, 0xAA, sizeof(e));
    gdt_pack_entry(&e, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    check(e.granularity == 0xCF, "gran low nibble is discarded -- 0xCF and 0xC0 must pack identically here");

    if (failures == 0) {
        printf("PASS: all gdt tests passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
