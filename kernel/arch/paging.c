#include "paging.h"

void paging_build_directory(uint32_t *pd, uint32_t pde_count) {
    uint32_t i;
    for (i = 0; i < pde_count; i++) {
        pd[i] = (i << 22) | PDE_IDENTITY_FLAGS;
    }
    for (; i < PAGE_DIRECTORY_ENTRIES; i++) {
        pd[i] = 0;
    }
}

void paging_set_user_entry(uint32_t *pd, uint32_t pde_index, int user) {
    if (user) {
        pd[pde_index] |= PDE_USER_FLAG;
    } else {
        pd[pde_index] &= ~(uint32_t)PDE_USER_FLAG;
    }
}

static uint32_t page_directory[PAGE_DIRECTORY_ENTRIES] __attribute__((aligned(4096)));

void paging_set_user(uint32_t pde_index, int user) {
    paging_set_user_entry(page_directory, pde_index, user);
    __asm__ volatile(
        "mov %%cr3, %%eax\n\t"
        "mov %%eax, %%cr3\n\t"
        :
        :
        : "eax", "memory"
    );
}

void paging_enable(void) {
    paging_build_directory(page_directory, PAGING_IDENTITY_PDE_COUNT);
    __asm__ volatile(
        "mov %%cr4, %%eax\n\t"
        "or $0x10, %%eax\n\t"       /* CR4.PSE */
        "mov %%eax, %%cr4\n\t"
        "mov %0, %%cr3\n\t"
        "mov %%cr0, %%eax\n\t"
        "or $0x80000000, %%eax\n\t" /* CR0.PG */
        "mov %%eax, %%cr0\n\t"
        "jmp 1f\n\t"                 /* flush prefetch queue */
        "1:\n\t"
        :
        : "r" (page_directory)
        : "eax", "memory"
    );
}
