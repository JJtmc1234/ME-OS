/* Address spaces: building, changing and reading page table trees.
 *
 * Nothing here executes a privileged instruction. Every table is reached
 * through the direct map offset held in the address space, so the whole of
 * this file runs unchanged as a host program over an arena that stands in for
 * physical memory. The instructions that load a table into the processor and
 * invalidate its cache live in vmmcpu.c, which the host tests do not link.
 *
 * See M30 in docs/milestones.md.
 */
#include "vmm.h"

#include "mem.h"

static uint64_t *table_at(const struct addrspace *space, uint64_t phys)
{
    return (uint64_t *)(uintptr_t)(phys + space->hhdm);
}

/* A zeroed page for a new table, or zero. Zeroed because an entry left over
 * from a previous owner is a present bit pointing at somebody else's memory. */
static uint64_t fresh_table(struct addrspace *space)
{
    uint64_t phys = pmm_alloc(space->pmm);
    if (phys == PMM_NONE) {
        return PMM_NONE;
    }
    memset(table_at(space, phys), 0, PAGE_SIZE);
    space->tables++;
    return phys;
}

enum vmm_result vmm_create(struct addrspace *space, struct pmm *pmm, uint64_t hhdm)
{
    space->pmm = pmm;
    space->hhdm = hhdm;
    space->root = 0;
    space->tables = 0;

    uint64_t root = fresh_table(space);
    if (root == PMM_NONE) {
        return VMM_NO_MEMORY;
    }
    space->root = root;
    return VMM_OK;
}

void vmm_adopt(struct addrspace *space, struct pmm *pmm, uint64_t hhdm, uint64_t root)
{
    space->pmm = pmm;
    space->hhdm = hhdm;
    space->root = root;
    /* Zero tables allocated, which is what stops vmm_destroy freeing memory
     * this address space never owned. */
    space->tables = 0;
}

/* Finds the page table entry for an address, optionally building the tables on
 * the way down. `leaf_flags` only matters when creating: it decides whether the
 * intermediate entries need the user bit. */
static enum vmm_result walk(struct addrspace *space, uint64_t virt,
                            bool create, uint64_t leaf_flags, uint64_t **entry_out)
{
    if (space == NULL || space->root == 0) {
        return VMM_NO_SPACE;
    }
    if (!paging_canonical(virt) || !paging_aligned(virt)) {
        return VMM_BAD_ADDRESS;
    }

    uint64_t *table = table_at(space, space->root);
    uint64_t want = paging_parent_flags(leaf_flags);

    for (int level = PAGING_LEVELS - 1; level > 0; level--) {
        uint64_t index = paging_index(virt, level);
        uint64_t entry = table[index];

        if (!paging_present(entry)) {
            if (!create) {
                return VMM_NOT_MAPPED;
            }
            uint64_t next = fresh_table(space);
            if (next == PMM_NONE) {
                return VMM_NO_MEMORY;
            }
            entry = paging_entry(next, want);
            table[index] = entry;
        } else if ((entry & PTE_HUGE) != 0) {
            /* The bootloader maps memory with large pages. Walking into one as
             * though it pointed at a table would read the middle of a mapped
             * page as though it were entries, and write there. */
            return VMM_HUGE_PAGE;
        } else if (create) {
            /* The processor takes the permission of a mapping to be the most
             * restrictive level on the path. A user page under a table entry
             * with no user bit is unreachable from user mode, so an existing
             * parent has to be widened. Safe, because every leaf still carries
             * its own restriction and a kernel page below stays kernel only. */
            uint64_t missing = want & ~entry;
            if (missing != 0) {
                entry |= missing;
                table[index] = entry;
            }
        }
        table = table_at(space, paging_entry_addr(entry));
    }

    *entry_out = &table[paging_index(virt, 0)];
    return VMM_OK;
}

enum vmm_result vmm_map(struct addrspace *space, uint64_t virt, uint64_t phys,
                        uint64_t flags)
{
    if (!paging_aligned(phys)) {
        return VMM_BAD_ADDRESS;
    }

    uint64_t *entry = NULL;
    enum vmm_result result = walk(space, virt, true, flags, &entry);
    if (result != VMM_OK) {
        return result;
    }
    if (paging_present(*entry)) {
        /* Replacing a mapping silently is how one program ends up owning a
         * page another still believes is its own. The caller has to unmap. */
        return VMM_ALREADY_MAPPED;
    }
    *entry = paging_entry(phys, flags | PTE_PRESENT);
    return VMM_OK;
}

enum vmm_result vmm_unmap(struct addrspace *space, uint64_t virt)
{
    uint64_t *entry = NULL;
    enum vmm_result result = walk(space, virt, false, 0, &entry);
    if (result != VMM_OK) {
        return result;
    }
    if (!paging_present(*entry)) {
        return VMM_NOT_MAPPED;
    }
    *entry = 0;
    /* The processor caches translations and does not watch the tables, so an
     * address that has just stopped meaning anything still works until the
     * cached entry is thrown away. */
    vmm_flush(virt);
    return VMM_OK;
}

enum vmm_result vmm_translate(const struct addrspace *space, uint64_t virt,
                              uint64_t *phys_out, uint64_t *flags_out)
{
    /* Const on the way in, and the walk cannot create anything with
     * `create` false, so the cast adds no capability the caller did not have. */
    struct addrspace *mutable_space = (struct addrspace *)space;
    uint64_t offset = virt & 0xFFFull;

    uint64_t *entry = NULL;
    enum vmm_result result = walk(mutable_space, virt & ~0xFFFull, false, 0, &entry);
    if (result != VMM_OK) {
        return result;
    }
    if (!paging_present(*entry)) {
        return VMM_NOT_MAPPED;
    }
    if (phys_out != NULL) {
        *phys_out = paging_entry_addr(*entry) + offset;
    }
    if (flags_out != NULL) {
        *flags_out = *entry & ~PTE_ADDR_MASK;
    }
    return VMM_OK;
}

enum vmm_result vmm_share_kernel(struct addrspace *space, const struct addrspace *from)
{
    if (space == NULL || from == NULL || space->root == 0 || from->root == 0) {
        return VMM_NO_SPACE;
    }
    uint64_t *mine = table_at(space, space->root);
    const uint64_t *theirs = table_at(from, from->root);

    /* Entries 256 to 511 are the top half of the address space, which is where
     * every kernel mapping lives. Sharing the top level entry shares every
     * table beneath it, so there is one kernel mapped once and seen the same
     * way from every process. */
    for (uint64_t i = PAGING_ENTRIES / 2; i < PAGING_ENTRIES; i++) {
        mine[i] = theirs[i];
    }
    return VMM_OK;
}

const char *vmm_result_text(enum vmm_result result)
{
    switch (result) {
    case VMM_OK:              return "ok";
    case VMM_BAD_ADDRESS:     return "address is not canonical or not page aligned";
    case VMM_NO_MEMORY:       return "no free pages to build a table from";
    case VMM_NOT_MAPPED:      return "nothing is mapped there";
    case VMM_ALREADY_MAPPED:  return "something is already mapped there";
    case VMM_HUGE_PAGE:       return "a large page is in the way";
    case VMM_NO_SPACE:        return "no address space";
    }
    return "unknown";
}
