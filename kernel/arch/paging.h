#ifndef RAVEOS_PAGING_H
#define RAVEOS_PAGING_H

#include <stdint.h>

#define PAGE_DIRECTORY_ENTRIES 1024

/* Covers PDE 0 through PAGING_IDENTITY_PDE_COUNT-1 as 4MB identity
 * pages. Chosen to comfortably clear the VBE linear framebuffer's
 * physical address -- confirmed empirically at 0xFD000000 (PDE 1012)
 * for this build -- with headroom: PDEs 1013-1015 (12MB) past the
 * framebuffer's own PDE for any resolution's actual pixel data plus
 * BytesPerScanLine padding. PDEs 1016-1023 (the top 32MB of the
 * 32-bit address space) are deliberately left unmapped -- free, since
 * an unset entry already reads as not-present, and exactly the range
 * this feature's own throwaway verification pokes at to prove the
 * page-fault handler fires on a real unmapped address. */
#define PAGING_IDENTITY_PDE_COUNT 1016

/* Present | Read-Write | Page Size (4MB). No User/Supervisor bit --
 * every mapped page is supervisor-only, since nothing runs at ring 3
 * yet. */
#define PDE_IDENTITY_FLAGS 0x83

/* User/Supervisor bit (bit 2). paging_build_directory() never sets
 * this -- every PDE it builds is supervisor-only by construction (see
 * PDE_IDENTITY_FLAGS). paging_set_user_entry()/paging_set_user() are
 * the only way any PDE ever becomes ring-3-accessible, and only ever
 * one entry at a time, in place. */
#define PDE_USER_FLAG 0x4

/* Fills pd[0..pde_count) as present, read-write, 4MB identity pages
 * (pd[i] maps physical/virtual region [i*4MB, (i+1)*4MB) to itself).
 * Zeroes pd[pde_count..PAGE_DIRECTORY_ENTRIES) (not-present) --
 * explicit rather than relying on the caller having zeroed the
 * buffer first. Pure function: no asm, no hardware access, every
 * output byte is a function of the inputs alone. pd must point to at
 * least PAGE_DIRECTORY_ENTRIES uint32_t entries; pde_count must be
 * <= PAGE_DIRECTORY_ENTRIES (callers in this codebase only ever pass
 * PAGING_IDENTITY_PDE_COUNT, but the function itself doesn't assume
 * that constant -- see test_paging.c's boundary cases). */
void paging_build_directory(uint32_t *pd, uint32_t pde_count);

/* Sets (user=1) or clears (user=0) PDE_USER_FLAG on pd[pde_index] in
 * place -- no other bit of that entry, and no other entry, changes.
 * pde_index must be < PAGE_DIRECTORY_ENTRIES. Pure function: no asm,
 * no hardware access. */
void paging_set_user_entry(uint32_t *pd, uint32_t pde_index, int user);

/* Real: applies paging_set_user_entry() to the live page directory
 * paging_enable() already built and switched CR3 to. Ring 3 code
 * cannot fetch its own first instruction without this -- the U/S bit
 * gates all access, not just data, and every PDE paging_enable()
 * builds starts supervisor-only. Also flushes the entire TLB (a CR3
 * reload with its own current value) after modifying the entry --
 * this identity map uses 4MB PSE pages, and the CPU may already hold
 * a cached PSE translation for the modified region carrying the old
 * permission; modifying the in-memory PDE alone doesn't retroactively
 * invalidate that cached entry, so without the flush a stale
 * supervisor-only translation could keep faulting ring-3 accesses
 * even after this call returns. */
void paging_set_user(uint32_t pde_index, int user);

/* Builds the identity map into a static page directory and switches
 * the CPU into paging mode (CR4.PSE, CR3, CR0.PG). Call once, after
 * interrupts_init() (so the #PF IDT gate is already live) and before
 * mouse_init()/interrupts_enable(). Never returns early/on error --
 * see docs/superpowers/specs/2026-08-26-paging-design.md's Design
 * section for why an unsupported-CPU fallback isn't warranted here. */
void paging_enable(void);

#endif
