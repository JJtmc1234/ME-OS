/* The physical page allocator.
 *
 * Every milestone up to M28 got its memory from a static array chosen when the
 * kernel was compiled. That works until something has to hand out memory whose
 * size nobody knew in advance, which is every part of running a program: an
 * address space, a page table, a stack, the pages a program is loaded into.
 *
 * So this hands out physical pages, one at a time, out of what the bootloader
 * said is really there.
 *
 * The allocator is a bitmap: one bit per page of physical memory, set when the
 * page is in use. A bitmap rather than a free list because a free list stores
 * its links inside the free pages themselves, and the first thing a wrong
 * pointer does there is corrupt the allocator instead of the caller. A bitmap
 * lives in one place, can be checked against the memory map at any time, and
 * costs one bit per four kilobytes, which is a thirty-two thousandth of RAM.
 *
 * Deliberately not here: no slab allocator, no buddy system, no swapping, no
 * NUMA. Those solve fragmentation and locality problems ME OS does not have
 * yet, and a page allocator that is easy to reason about is worth more now
 * than one that is fast.
 *
 * Nothing in this file knows about Limine, the boot process, or any hardware.
 * It operates on a bitmap the caller supplies, which is what lets the whole of
 * it be tested on the development machine. `pmmboot.c` is the part that reads
 * the real memory map.
 *
 * See M29 in docs/milestones.md.
 */
#ifndef ME_PMM_H
#define ME_PMM_H

#include <stdbool.h>
#include <stdint.h>

#define PAGE_SIZE 4096u

/* Returned by pmm_alloc when there is nothing left.
 *
 * Zero is safe as the failure value because physical page zero holds the real
 * mode interrupt vector table and the BIOS data area. No memory map calls it
 * usable, so it is never a legitimate result, and using it as the sentinel
 * avoids a separate out parameter on every allocation. */
#define PMM_NONE 0u

/* One bit per page, covering `pages` pages starting at `lowest`.
 *
 * `lowest` is a page frame number rather than an address so that a machine
 * whose usable memory starts high does not need a bitmap covering the empty
 * space below it. */
struct pmm {
    uint8_t *bitmap;
    uint64_t bitmap_bytes;
    /* Page frame number of the first page the bitmap covers. */
    uint64_t first_frame;
    uint64_t pages;
    uint64_t free_pages;
    /* Where the next search starts, so allocating N pages is not N scans from
     * the beginning. Only ever a hint: a free page below it is still found. */
    uint64_t hint;
    /* Refused operations, kept because a kernel that silently ignores a bad
     * free is a kernel that cannot be debugged later. */
    uint64_t bad_frees;
};

/* Points the allocator at a bitmap and marks everything as in use.
 *
 * Starting fully used rather than fully free is deliberate. Memory has to be
 * proven usable before it is handed out, so a region the memory map never
 * mentioned stays unavailable instead of becoming available by omission. */
void pmm_reset(struct pmm *pmm, uint8_t *bitmap, uint64_t bitmap_bytes,
               uint64_t first_frame, uint64_t pages);

/* Marks a byte range usable. Partial pages at either end are not handed out. */
void pmm_add_free(struct pmm *pmm, uint64_t base, uint64_t length);

/* Marks a byte range in use, so it is never handed out. Any page the range
 * touches at all is reserved, including the two partial ones at the ends. */
void pmm_reserve(struct pmm *pmm, uint64_t base, uint64_t length);

/* One page, or PMM_NONE when there is none. The page is not cleared. */
uint64_t pmm_alloc(struct pmm *pmm);

/* Gives a page back. Refuses a page outside the bitmap and refuses one that is
 * already free, counting both in `bad_frees` rather than corrupting the count
 * of what is available. */
void pmm_free(struct pmm *pmm, uint64_t phys);

/* Whether the page containing this address is available. Out of range is not
 * free, because memory the allocator does not cover is memory it must not
 * hand out. */
bool pmm_is_free(const struct pmm *pmm, uint64_t phys);

/* How many bytes of bitmap are needed to cover this many pages. */
uint64_t pmm_bitmap_bytes_for(uint64_t pages);

#endif /* ME_PMM_H */
