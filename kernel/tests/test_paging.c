/* kernel/tests/test_paging.c -- host-side only, never linked into
 * kernel.bin. paging_build_directory() has no freestanding-only
 * dependencies (pure array-filling logic, no asm), so it compiles
 * and runs natively here exactly as it will inside kernel.bin --
 * same convention as test_font.c/test_boot_splash.c. paging_enable()
 * itself (real CR0/CR3/CR4 access) is NOT exercised here; it can
 * only be verified by actually booting (see this feature's
 * BUILD_LOG entry for the one-time headless-QEMU verification). */
#include <stdio.h>
#include "../arch/paging.h"

static int failures = 0;

static void check(int cond, const char *msg) {
    if (!cond) {
        printf("FAIL: %s\n", msg);
        failures++;
    }
}

static void check_identity_entry(uint32_t entry, uint32_t index, const char *ctx) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: entry %u should be present", ctx, index);
    check((entry & 0x1) != 0, buf);
    snprintf(buf, sizeof(buf), "%s: entry %u should be read-write", ctx, index);
    check((entry & 0x2) != 0, buf);
    snprintf(buf, sizeof(buf), "%s: entry %u should be a 4MB page (PS set)", ctx, index);
    check((entry & 0x80) != 0, buf);
    snprintf(buf, sizeof(buf), "%s: entry %u should have no User bit set", ctx, index);
    check((entry & 0x4) == 0, buf);
    snprintf(buf, sizeof(buf), "%s: entry %u should map physical base index<<22", ctx, index);
    check((entry & 0xFFC00000u) == (index << 22), buf);
}

int main(void) {
    static uint32_t pd[PAGE_DIRECTORY_ENTRIES];
    unsigned int i;

    /* Typical case: a mid-range pde_count, matching the real
     * PAGING_IDENTITY_PDE_COUNT's shape (some entries mapped, some
     * not) without using the real (large) constant, so a failure's
     * printed index is easy to reason about by hand. */
    for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        pd[i] = 0xDEADBEEF; /* poison -- prove the function actually writes every slot */
    }
    paging_build_directory(pd, 10);
    for (i = 0; i < 10; i++) {
        check_identity_entry(pd[i], i, "pde_count=10");
    }
    for (i = 10; i < PAGE_DIRECTORY_ENTRIES; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "pde_count=10: entry %u should be zero (not-present)", i);
        check(pd[i] == 0, buf);
    }

    /* Boundary: pde_count = 0 -- every entry zero. */
    for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        pd[i] = 0xDEADBEEF;
    }
    paging_build_directory(pd, 0);
    for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        check(pd[i] == 0, "pde_count=0: every entry should be zero");
    }

    /* Boundary: pde_count = PAGE_DIRECTORY_ENTRIES -- nothing left unmapped. */
    for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        pd[i] = 0xDEADBEEF;
    }
    paging_build_directory(pd, PAGE_DIRECTORY_ENTRIES);
    for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        check_identity_entry(pd[i], i, "pde_count=PAGE_DIRECTORY_ENTRIES");
    }

    /* The real constant this kernel actually uses -- spot-check the
     * boundary around it specifically. */
    for (i = 0; i < PAGE_DIRECTORY_ENTRIES; i++) {
        pd[i] = 0xDEADBEEF;
    }
    paging_build_directory(pd, PAGING_IDENTITY_PDE_COUNT);
    check_identity_entry(pd[PAGING_IDENTITY_PDE_COUNT - 1], PAGING_IDENTITY_PDE_COUNT - 1, "real PAGING_IDENTITY_PDE_COUNT");
    check(pd[PAGING_IDENTITY_PDE_COUNT] == 0, "real PAGING_IDENTITY_PDE_COUNT: first unmapped entry should be zero");
    check(pd[PAGE_DIRECTORY_ENTRIES - 1] == 0, "real PAGING_IDENTITY_PDE_COUNT: last entry should be zero");
    /* The specific PDE the VBE framebuffer (0xFD000000) lives in must
     * be mapped -- this is the concrete requirement the whole feature
     * exists for, not just an arbitrary index. */
    check(PAGING_IDENTITY_PDE_COUNT > 1012, "PAGING_IDENTITY_PDE_COUNT must cover PDE 1012 (0xFD000000, the confirmed framebuffer address)");

    if (failures == 0) {
        printf("PASS: all paging tests passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", failures);
    return 1;
}
