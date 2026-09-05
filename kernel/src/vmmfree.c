/* Taking an address space apart.
 *
 * Separate from vmm.c because the recursion is the part most likely to be
 * wrong in a way that costs memory quietly, and it is worth reading on its own.
 * A leak here does not crash anything. It just means a machine that has run a
 * few thousand programs has no memory left and nobody knows why.
 *
 * Two rules decide everything below.
 *
 * Only the lower half is freed. Entries 256 to 511 of the top level table are
 * the kernel, shared with every other address space by `vmm_share_kernel`.
 * Freeing those would tear the kernel out from under every process on the
 * machine, including the one doing the freeing.
 *
 * Only tables are freed, never mapped pages. This layer maps a physical page
 * at an address and never learns whether anybody else mapped the same page
 * somewhere else. Freeing it here would take a page away from whoever else
 * holds it. Whatever allocated the page frees it.
 *
 * See M30 in docs/milestones.md.
 */
#include "vmm.h"

#include <stddef.h>

static uint64_t *table_at(const struct addrspace *space, uint64_t phys)
{
    return (uint64_t *)(uintptr_t)(phys + space->hhdm);
}

/* Frees the tables below one entry, then the table it points at.
 *
 * `level` is the level of the table this entry lives in, so its children are
 * at `level - 1`. Level 1 entries point at page tables whose entries are
 * mapped pages rather than tables, which is where the recursion stops. */
static void free_below(struct addrspace *space, uint64_t entry, int level)
{
    if (!paging_present(entry) || (entry & PTE_HUGE) != 0) {
        return;
    }
    uint64_t phys = paging_entry_addr(entry);

    if (level > 1) {
        uint64_t *table = table_at(space, phys);
        for (uint64_t i = 0; i < PAGING_ENTRIES; i++) {
            free_below(space, table[i], level - 1);
        }
    }
    /* At level 1 nothing below is a table, so only this page table is freed
     * and the pages it maps are left to whoever allocated them. */
    pmm_free(space->pmm, phys);
    if (space->tables > 0) {
        space->tables--;
    }
}

void vmm_destroy(struct addrspace *space)
{
    if (space == NULL || space->root == 0) {
        return;
    }
    /* An adopted space allocated nothing, so it owns nothing to free. The
     * bootloader's tables are the case this exists for: the kernel runs on
     * them and did not make them. */
    if (space->tables == 0) {
        space->root = 0;
        return;
    }

    uint64_t *root = table_at(space, space->root);
    for (uint64_t i = 0; i < PAGING_ENTRIES / 2; i++) {
        free_below(space, root[i], PAGING_LEVELS - 1);
        root[i] = 0;
    }
    /* The top half entries are the kernel's and are only borrowed, so they are
     * cleared rather than followed. */
    for (uint64_t i = PAGING_ENTRIES / 2; i < PAGING_ENTRIES; i++) {
        root[i] = 0;
    }

    pmm_free(space->pmm, space->root);
    if (space->tables > 0) {
        space->tables--;
    }
    space->root = 0;
}
