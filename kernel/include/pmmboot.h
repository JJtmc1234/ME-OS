/* Turning the bootloader's memory map into a working page allocator.
 *
 * `pmm.c` is the allocator and knows nothing about hardware, which is what
 * lets it be tested on the development machine. This is the other half: the
 * part that reads what Limine handed over, decides where the bitmap lives, and
 * hands the allocator the real numbers.
 *
 * It also owns the one global allocator. There is exactly one physical memory
 * on the machine, so there is exactly one of these, and passing it to every
 * caller would be a pointer that could only ever have one value.
 *
 * See M29 in docs/milestones.md.
 */
#ifndef ME_PMMBOOT_H
#define ME_PMMBOOT_H

#include <stdbool.h>
#include <stdint.h>

#include "pmm.h"

struct limine_memmap_response;

/* Reads the memory map and brings the allocator up.
 *
 * `hhdm` is the offset Limine mapped all of physical memory at, so physical
 * address p can be read and written at virtual address p + hhdm. The bitmap
 * lives in physical memory and is reached that way.
 *
 * Returns false when there is no map, no HHDM, or nowhere to put the bitmap.
 * The kernel can still run without an allocator, it just cannot start a
 * program, so this reports rather than halting. */
bool pmmboot_init(const struct limine_memmap_response *map, uint64_t hhdm);

/* The one allocator. Never NULL, but has no free pages until init succeeds. */
struct pmm *pmm_kernel(void);

/* Whether init actually found memory. */
bool pmmboot_ready(void);

/* Physical to virtual, through the bootloader's map of all memory.
 *
 * Only valid for physical addresses. Passing a virtual address here produces a
 * second, wrong virtual address rather than an error, which is the one sharp
 * edge of a direct map and the reason every caller of this is short. */
void *phys_to_virt(uint64_t phys);

/* The offset above, for code that has to do the arithmetic itself. */
uint64_t hhdm_offset(void);

/* A page of memory, already zeroed, or NULL when there is none.
 *
 * Page tables and process structures all need clean pages, and a page that
 * still holds whatever the last owner left in it is how one program reads
 * another's data. Zeroing here rather than at each call site means it cannot
 * be forgotten at one of them. */
void *pmm_alloc_zeroed(uint64_t *phys_out);

/* Proves at boot that the allocator and the direct map agree.
 *
 * The host tests cover the bitmap arithmetic completely, and none of them can
 * catch the one thing that only goes wrong on a real machine: a page that the
 * bitmap says is free but that cannot actually be written, because the direct
 * map offset is wrong or the memory map claimed something that is not there.
 * So this takes a few pages, writes a pattern, reads it back, and gives them
 * up again. It logs one line either way. */
void pmmboot_selfcheck(void);

#endif /* ME_PMMBOOT_H */
