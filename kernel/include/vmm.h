/* Address spaces.
 *
 * Up to M29 there was one universe. Every address meant the same thing to
 * every part of the machine, because the bootloader's page tables were still
 * in use and nothing had ever built another set. That is fine while the only
 * code running is the kernel's own, and impossible the moment two programs are
 * meant to be unable to read each other.
 *
 * An address space here is a page table tree. Map, unmap and look up are the
 * whole interface. What makes them useful is that two of these can describe
 * different memory at the same address, which is what "separate address space"
 * means and what every process model is built on.
 *
 * The direct map offset is a field rather than a global. That is not
 * generality for its own sake: it is what lets the entire walk run as a host
 * program in the test suite, over an arena that stands in for physical memory,
 * where a wrong entry prints a failure instead of triple faulting a virtual
 * machine three milestones later.
 *
 * See M30 in docs/milestones.md.
 */
#ifndef ME_VMM_H
#define ME_VMM_H

#include <stdbool.h>
#include <stdint.h>

#include "paging.h"
#include "pmm.h"

/* One address space, and the two things needed to build one.
 *
 * `pmm` supplies the pages the tables themselves are made of. `hhdm` is the
 * offset at which physical memory can be read and written, so a table at
 * physical address p is reached at p + hhdm. */
struct addrspace {
    struct pmm *pmm;
    uint64_t hhdm;
    /* Physical address of the top level table. Zero when there is none. */
    uint64_t root;
    /* Tables allocated for this space, so tearing it down can be checked. */
    uint64_t tables;
};

/* Why a mapping was refused. A boolean would hide the difference between
 * asking for something impossible and running out of memory, and those need
 * different responses from the caller. */
enum vmm_result {
    VMM_OK = 0,
    /* The address is not canonical, or is not page aligned. */
    VMM_BAD_ADDRESS,
    /* No pages left to build a table out of. */
    VMM_NO_MEMORY,
    /* Nothing is mapped there. */
    VMM_NOT_MAPPED,
    /* Something is already mapped there. Overwriting silently is how one
     * program ends up with a page another one still believes it owns. */
    VMM_ALREADY_MAPPED,
    /* The walk met a large page the bootloader made. This kernel does not
     * create them and refuses to take one apart, because splitting one while
     * something is using it changes what an address means underneath it. */
    VMM_HUGE_PAGE,
    /* No address space, or no root table. */
    VMM_NO_SPACE,
};

/* Builds an empty address space with a freshly allocated top level table. */
enum vmm_result vmm_create(struct addrspace *space, struct pmm *pmm, uint64_t hhdm);

/* Points an address space at a page table tree that already exists.
 *
 * Used for the one the bootloader built, which the kernel is running on and
 * did not make. Nothing is allocated and nothing is owned, so destroying it
 * would be wrong and is refused. */
void vmm_adopt(struct addrspace *space, struct pmm *pmm, uint64_t hhdm, uint64_t root);

/* Maps one page. Refuses to replace an existing mapping. */
enum vmm_result vmm_map(struct addrspace *space, uint64_t virt, uint64_t phys,
                        uint64_t flags);

/* Removes one page's mapping. The physical page itself is not freed, because
 * the same page may be mapped in more than one address space and this layer
 * does not know who else is holding it. */
enum vmm_result vmm_unmap(struct addrspace *space, uint64_t virt);

/* What an address currently means. Either output may be NULL. */
enum vmm_result vmm_translate(const struct addrspace *space, uint64_t virt,
                              uint64_t *phys_out, uint64_t *flags_out);

/* Copies the upper half of another space's top level table into this one.
 *
 * The kernel lives in the top half of the address space and every process
 * needs it mapped, or a system call would land in an address space where the
 * handler does not exist. Sharing the top level entries rather than copying
 * the tables beneath them means one kernel, mapped once, seen identically by
 * everybody, and a change to a kernel mapping does not have to be repeated
 * into every process.
 *
 * The upper half entries never carry the user bit, so a process can see that
 * the kernel is there and cannot read it. */
enum vmm_result vmm_share_kernel(struct addrspace *space, const struct addrspace *from);

/* Frees the tables this space allocated, and nothing else.
 *
 * Only the lower half is walked, because the upper half is the kernel's and is
 * shared with every other space. Pages that were mapped are left alone: this
 * layer never knew whether it was the only holder of one. */
void vmm_destroy(struct addrspace *space);

/* Installs this address space on the processor. */
void vmm_activate(const struct addrspace *space);

/* Turns on the no-execute bit if the processor has it. Returns whether the
 * bit may now be used: until this succeeds, setting PTE_NX faults. */
bool vmm_enable_nx(void);

/* Whether PTE_NX is safe to set. False until vmm_enable_nx has succeeded. */
bool vmm_nx_available(void);

/* Forgets one address's cached translation. The processor caches translations
 * and does not notice a table changing underneath it. */
void vmm_flush(uint64_t virt);

/* The page table tree the processor is using right now. */
uint64_t vmm_current_root(void);

const char *vmm_result_text(enum vmm_result result);

#endif /* ME_VMM_H */
