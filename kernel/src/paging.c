/* The x86-64 page table format, as arithmetic. See kernel/include/paging.h.
 *
 * Every function here is total: no level, address or flag combination makes
 * one of them do something undefined. That is the point of the file. The walk
 * in vmm.c is allowed to be careful; this is where it gets to stop worrying.
 */
#include "paging.h"

uint64_t paging_index(uint64_t virt, int level)
{
    if (level < 0 || level >= PAGING_LEVELS) {
        /* A shift by a bad amount is undefined behaviour, and on x86 it is
         * silently a shift by the low six bits of the count, which produces a
         * plausible looking index into the wrong table. Refuse instead. */
        return 0;
    }
    unsigned shift = 12u + 9u * (unsigned)level;
    return (virt >> shift) & (PAGING_ENTRIES - 1u);
}

bool paging_canonical(uint64_t virt)
{
    /* Sign extend bit 47 and see whether the address survives unchanged.
     * Shifting left then arithmetic right is the shortest way to say that. */
    int64_t signed_virt = (int64_t)virt;
    int64_t extended = (signed_virt << 16) >> 16;
    return (uint64_t)extended == virt;
}

uint64_t paging_entry(uint64_t phys, uint64_t flags)
{
    /* Masking rather than trusting. An address with a low bit set would
     * otherwise land on top of the permission bits and turn a read only
     * mapping into a writable one, or a kernel page into a user page. */
    return (phys & PTE_ADDR_MASK) | flags;
}

uint64_t paging_entry_addr(uint64_t entry)
{
    return entry & PTE_ADDR_MASK;
}

bool paging_aligned(uint64_t address)
{
    return (address & 0xFFFull) == 0;
}

uint64_t paging_parent_flags(uint64_t leaf_flags)
{
    uint64_t flags = PTE_PRESENT | PTE_WRITE;
    if ((leaf_flags & PTE_USER) != 0) {
        flags |= PTE_USER;
    }
    /* Never NX on the way down. Setting no-execute on a table entry forbids
     * execution of everything beneath it, so one non executable page would
     * make its neighbours non executable too. */
    return flags;
}
