/* Host tests for M30 address spaces.
 *
 * The whole page table walk runs here, not just the arithmetic around it. That
 * is possible because an address space reaches its tables through a direct map
 * offset it was handed, rather than a constant, so an arena of ordinary host
 * memory can stand in for physical memory: the "physical" addresses below are
 * made up, and `hhdm` is the distance between them and the real buffer.
 *
 * It matters because a wrong page table entry on real hardware does not print
 * anything. It triple faults a virtual machine, usually somewhere unrelated
 * and often several milestones after the mistake was made.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmm.h"

static int failures;

static void check(int condition, const char *what)
{
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

/* vmmcpu.c is not linked here: it is nothing but privileged instructions. The
 * walk calls this after removing a mapping, and on a host there is no
 * translation cache to invalidate. Counted so the test can prove it happened. */
static uint64_t flushes;

void vmm_flush(uint64_t virt)
{
    (void)virt;
    flushes++;
}

/* Where the pretend physical memory starts. Not zero, so that page zero stays
 * out of it exactly as it does on a real machine. */
#define FAKE_BASE 0x200000ull
#define ARENA_PAGES 512u
#define ARENA_BYTES ((uint64_t)ARENA_PAGES * PAGE_SIZE)

static uint8_t *arena;
static uint8_t bitmap[ARENA_PAGES / 8u];
static struct pmm pmm;
static uint64_t hhdm;

static void fresh_memory(void)
{
    memset(arena, 0, (size_t)ARENA_BYTES);
    pmm_reset(&pmm, bitmap, sizeof bitmap, FAKE_BASE / PAGE_SIZE, ARENA_PAGES);
    pmm_add_free(&pmm, FAKE_BASE, ARENA_BYTES);
}

/* A page of pretend physical memory that is not part of any table. */
static uint64_t some_page(void)
{
    return pmm_alloc(&pmm);
}

int main(void)
{
    arena = aligned_alloc(PAGE_SIZE, (size_t)ARENA_BYTES);
    if (arena == NULL) {
        printf("could not allocate the test arena\n");
        return 1;
    }
    /* Physical address p is read at p + hhdm, which is where the arena is. */
    hhdm = (uint64_t)(uintptr_t)arena - FAKE_BASE;

    printf("a new address space has a root table and nothing in it\n");
    fresh_memory();
    struct addrspace space;
    check(vmm_create(&space, &pmm, hhdm) == VMM_OK, "creating one succeeds");
    check(space.root != 0, "and it has a top level table");
    check(space.tables == 1, "which is the only table so far");
    check(vmm_translate(&space, 0x1000, NULL, NULL) == VMM_NOT_MAPPED,
          "nothing is mapped in it yet");

    printf("a mapping can be made and read back\n");
    uint64_t page = some_page();
    check(vmm_map(&space, 0x400000, page, PTE_WRITE | PTE_USER) == VMM_OK,
          "mapping a page succeeds");
    uint64_t got = 0;
    uint64_t flags = 0;
    check(vmm_translate(&space, 0x400000, &got, &flags) == VMM_OK, "and it translates");
    check(got == page, "to the page it was given");
    check((flags & PTE_PRESENT) != 0, "the entry is present");
    check((flags & PTE_WRITE) != 0, "and writable");
    check((flags & PTE_USER) != 0, "and reachable from user mode");
    check(space.tables == 4, "three tables were built on the way down");

    printf("an offset inside the page is kept\n");
    check(vmm_translate(&space, 0x400ABC, &got, NULL) == VMM_OK, "a mid page address translates");
    check(got == page + 0xABC, "to the same page plus the offset");

    printf("mapping over something already there is refused\n");
    uint64_t other = some_page();
    check(vmm_map(&space, 0x400000, other, PTE_WRITE) == VMM_ALREADY_MAPPED,
          "the second mapping is refused");
    check(vmm_translate(&space, 0x400000, &got, NULL) == VMM_OK && got == page,
          "and the first one is untouched");

    printf("unmapping removes it, and says so when there is nothing there\n");
    uint64_t before_flushes = flushes;
    check(vmm_unmap(&space, 0x400000) == VMM_OK, "unmapping succeeds");
    check(flushes == before_flushes + 1, "and the stale translation was thrown away");
    check(vmm_translate(&space, 0x400000, NULL, NULL) == VMM_NOT_MAPPED,
          "the address means nothing now");
    check(vmm_unmap(&space, 0x400000) == VMM_NOT_MAPPED, "unmapping again is refused");
    check(vmm_map(&space, 0x400000, other, PTE_WRITE) == VMM_OK,
          "and the address can be mapped again");

    printf("an address the processor would refuse is refused here first\n");
    /* Bits 63 to 48 have to be a copy of bit 47. This one is not. */
    check(vmm_map(&space, 0x0000800000000000ull, page, PTE_WRITE) == VMM_BAD_ADDRESS,
          "a non canonical address is refused");
    check(vmm_map(&space, 0x400123, page, PTE_WRITE) == VMM_BAD_ADDRESS,
          "an unaligned virtual address is refused");
    check(vmm_map(&space, 0x500000, page + 8, PTE_WRITE) == VMM_BAD_ADDRESS,
          "an unaligned physical address is refused");
    check(vmm_translate(&space, 0xFFFF800000000000ull, NULL, NULL) == VMM_NOT_MAPPED,
          "a canonical high address is walked rather than refused");

    printf("the top of the address space is canonical and the middle is not\n");
    check(paging_canonical(0xFFFFFFFF80000000ull), "the kernel's own base is canonical");
    check(paging_canonical(0x00007FFFFFFFF000ull), "the top of the low half is canonical");
    check(!paging_canonical(0x0000800000000000ull), "one page above it is not");
    check(!paging_canonical(0xFFFF7FFFFFFFF000ull), "and nor is one below the high half");

    printf("an intermediate table is widened rather than left unreachable\n");
    /* A kernel page first, so the tables above it are made without the user
     * bit. Then a user page underneath the same tables. The processor takes
     * the most restrictive level on the path, so unless the parents gain the
     * user bit the second page is unreachable from user mode. */
    fresh_memory();
    vmm_create(&space, &pmm, hhdm);
    uint64_t kernel_page = some_page();
    uint64_t user_page = some_page();
    check(vmm_map(&space, 0x800000, kernel_page, PTE_WRITE) == VMM_OK, "a kernel page maps");
    check(vmm_map(&space, 0x801000, user_page, PTE_WRITE | PTE_USER) == VMM_OK,
          "and a user page beside it maps");

    uint64_t *root = (uint64_t *)(uintptr_t)(space.root + hhdm);
    uint64_t pdpt_entry = root[paging_index(0x800000, 3)];
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(paging_entry_addr(pdpt_entry) + hhdm);
    uint64_t pd_entry = pdpt[paging_index(0x800000, 2)];
    uint64_t *pd = (uint64_t *)(uintptr_t)(paging_entry_addr(pd_entry) + hhdm);
    uint64_t pt_entry = pd[paging_index(0x800000, 1)];
    check((pdpt_entry & PTE_USER) != 0, "the top level entry gained the user bit");
    check((pd_entry & PTE_USER) != 0, "so did the one below it");
    check((pt_entry & PTE_USER) != 0, "and the one below that");

    uint64_t kernel_flags = 0;
    check(vmm_translate(&space, 0x800000, NULL, &kernel_flags) == VMM_OK, "the kernel page still translates");
    check((kernel_flags & PTE_USER) == 0,
          "and is still not reachable from user mode, because the leaf decides");

    printf("no-execute is never set on the way down\n");
    fresh_memory();
    vmm_create(&space, &pmm, hhdm);
    check(vmm_map(&space, 0x900000, some_page(), PTE_WRITE | PTE_NX) == VMM_OK,
          "a non executable page maps");
    root = (uint64_t *)(uintptr_t)(space.root + hhdm);
    check((root[paging_index(0x900000, 3)] & PTE_NX) == 0,
          "the table above it stays executable, or its neighbours would not be");

    printf("two address spaces mean different things by the same address\n");
    fresh_memory();
    struct addrspace one;
    struct addrspace two;
    vmm_create(&one, &pmm, hhdm);
    vmm_create(&two, &pmm, hhdm);
    uint64_t page_one = some_page();
    uint64_t page_two = some_page();
    check(page_one != page_two, "the two pages really are different");
    check(vmm_map(&one, 0x10000000, page_one, PTE_WRITE | PTE_USER) == VMM_OK, "one maps its page");
    check(vmm_map(&two, 0x10000000, page_two, PTE_WRITE | PTE_USER) == VMM_OK,
          "the other maps a different page at the same address");
    vmm_translate(&one, 0x10000000, &got, NULL);
    check(got == page_one, "the first space sees its own page");
    vmm_translate(&two, 0x10000000, &got, NULL);
    check(got == page_two, "and the second sees its own");
    check(vmm_translate(&two, 0x10000000, &got, NULL) == VMM_OK && got != page_one,
          "neither can reach the other's through that address");

    printf("the kernel half is shared, and only the kernel half\n");
    struct addrspace kernel;
    fresh_memory();
    vmm_create(&kernel, &pmm, hhdm);
    check(vmm_map(&kernel, 0xFFFFFFFF80000000ull, some_page(), PTE_WRITE) == VMM_OK,
          "the kernel maps a page in the high half");
    check(vmm_map(&kernel, 0x2000, some_page(), PTE_WRITE) == VMM_OK,
          "and one in the low half");
    struct addrspace process;
    vmm_create(&process, &pmm, hhdm);
    check(vmm_share_kernel(&process, &kernel) == VMM_OK, "a process borrows the kernel half");
    check(vmm_translate(&process, 0xFFFFFFFF80000000ull, NULL, NULL) == VMM_OK,
          "and can see the kernel page");
    check(vmm_translate(&process, 0x2000, NULL, NULL) == VMM_NOT_MAPPED,
          "but not the kernel's low half page, which was not shared");

    printf("a large page is refused rather than walked into\n");
    fresh_memory();
    vmm_create(&space, &pmm, hhdm);
    vmm_map(&space, 0xA00000, some_page(), PTE_WRITE);
    root = (uint64_t *)(uintptr_t)(space.root + hhdm);
    pdpt = (uint64_t *)(uintptr_t)(paging_entry_addr(root[paging_index(0xA00000, 3)]) + hhdm);
    pd = (uint64_t *)(uintptr_t)(paging_entry_addr(pdpt[paging_index(0xA00000, 2)]) + hhdm);
    /* Turn the page directory entry into a two megabyte page, the way a
     * bootloader does when it maps memory cheaply. */
    pd[paging_index(0xA00000, 1)] |= PTE_HUGE;
    check(vmm_translate(&space, 0xA00000, NULL, NULL) == VMM_HUGE_PAGE,
          "translating through it says so");
    check(vmm_map(&space, 0xA01000, some_page(), PTE_WRITE) == VMM_HUGE_PAGE,
          "and mapping under it is refused rather than corrupting the page");

    printf("running out of memory is reported, not ignored\n");
    fresh_memory();
    vmm_create(&space, &pmm, hhdm);
    /* Every mapping in its own top level slot, so each one needs three new
     * tables and the arena runs out quickly and predictably. */
    enum vmm_result last = VMM_OK;
    int mapped = 0;
    for (uint64_t slot = 0; slot < 200; slot++) {
        uint64_t at = slot << 39;
        if (!paging_canonical(at)) {
            continue;
        }
        uint64_t frame = pmm_alloc(&pmm);
        if (frame == PMM_NONE) {
            last = VMM_NO_MEMORY;
            break;
        }
        last = vmm_map(&space, at, frame, PTE_WRITE);
        if (last != VMM_OK) {
            break;
        }
        mapped++;
    }
    check(last == VMM_NO_MEMORY, "the arena ran out and the answer said so");
    check(mapped > 0, "after really mapping some pages first");
    check(vmm_translate(&space, 0, NULL, NULL) == VMM_OK,
          "and what was mapped before it ran out still works");

    printf("destroying a space gives back its tables and no more\n");
    fresh_memory();
    /* Before creating anything, so the root table is inside what has to come
     * back. Taking the count afterwards was the first version of this test and
     * it was off by exactly one page, which is the size of a root table. */
    uint64_t free_at_start = pmm.free_pages;
    vmm_create(&space, &pmm, hhdm);
    uint64_t mapped_page = some_page();
    vmm_map(&space, 0x400000, mapped_page, PTE_WRITE | PTE_USER);
    vmm_map(&space, 0x401000, some_page(), PTE_WRITE | PTE_USER);
    /* A second top level slot, so the recursion has more than one branch. */
    vmm_map(&space, 0x40000000, some_page(), PTE_WRITE | PTE_USER);
    uint64_t tables_built = space.tables;
    check(tables_built > 1, "several tables were built");
    vmm_destroy(&space);
    check(space.root == 0, "the space has no root afterwards");
    check(space.tables == 0, "and no tables outstanding");
    check(pmm.bad_frees == 0, "nothing was freed twice");
    /* The three mapped pages were allocated by the caller and stay allocated,
     * because this layer never knew whether anybody else held them. */
    check(pmm.free_pages == free_at_start - 3,
          "the tables came back and the mapped pages did not");
    check(pmm_is_free(&pmm, mapped_page) == false, "a mapped page is still the caller's");

    printf("an adopted space is never freed, because it was never owned\n");
    fresh_memory();
    struct addrspace borrowed;
    uint64_t someone_elses_root = some_page();
    memset((void *)(uintptr_t)(someone_elses_root + hhdm), 0, PAGE_SIZE);
    vmm_adopt(&borrowed, &pmm, hhdm, someone_elses_root);
    uint64_t free_before = pmm.free_pages;
    vmm_destroy(&borrowed);
    check(pmm.free_pages == free_before, "destroying it freed nothing");
    check(!pmm_is_free(&pmm, someone_elses_root), "the table it borrowed is still allocated");

    printf("nothing works without a space\n");
    struct addrspace empty = { .pmm = &pmm, .hhdm = hhdm, .root = 0, .tables = 0 };
    check(vmm_map(&empty, 0x1000, 0x1000, PTE_WRITE) == VMM_NO_SPACE, "mapping refuses");
    check(vmm_unmap(&empty, 0x1000) == VMM_NO_SPACE, "unmapping refuses");
    check(vmm_translate(&empty, 0x1000, NULL, NULL) == VMM_NO_SPACE, "translating refuses");
    check(vmm_share_kernel(&empty, &empty) == VMM_NO_SPACE, "sharing refuses");

    printf("every reason has words\n");
    check(vmm_result_text(VMM_OK)[0] != '\0', "ok has text");
    check(vmm_result_text(VMM_HUGE_PAGE)[0] != '\0', "so does the large page case");
    check(vmm_result_text((enum vmm_result)99)[0] != '\0', "and so does a value that is not one");

    free(arena);
    printf(failures == 0 ? "\naddress space checks passed\n"
                         : "\naddress space checks FAILED\n");
    return failures == 0 ? 0 : 1;
}
