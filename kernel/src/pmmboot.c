/* Bringing the page allocator up from what the bootloader found.
 *
 * The whole safety argument of this file rests on one decision made in
 * `pmm_reset`: every page starts marked in use, and memory becomes available
 * only by being named as usable. That inverts the usual danger. Forgetting to
 * reserve something does not hand out memory somebody else owns, it just
 * leaves a page unused. To hand out the kernel's own image, or the
 * framebuffer, or the bootloader's tables, this file would have to actively
 * say they were usable, and it never does.
 *
 * So there is exactly one explicit reservation here, for the bitmap itself,
 * which lives inside memory the map did call usable.
 *
 * See M29 in docs/milestones.md.
 */
#include "pmmboot.h"

#include "limine.h"
#include "log.h"
#include "mem.h"

static struct pmm kernel_pmm;
static uint64_t hhdm_base;
static bool ready;

struct pmm *pmm_kernel(void)
{
    return &kernel_pmm;
}

bool pmmboot_ready(void)
{
    return ready;
}

uint64_t hhdm_offset(void)
{
    return hhdm_base;
}

void *phys_to_virt(uint64_t phys)
{
    return (void *)(uintptr_t)(phys + hhdm_base);
}

/* The highest address any usable region reaches.
 *
 * The bitmap covers physical zero up to this, rather than starting at the
 * lowest usable address. Starting at zero wastes one bit per page of the hole
 * below the first usable region, which on a PC is a few hundred bits, and in
 * exchange every frame number is its own bitmap index with no offset to get
 * wrong. The allocator supports a non zero start and is tested for it, but
 * nothing here needs one. */
static uint64_t highest_usable_end(const struct limine_memmap_response *map)
{
    uint64_t top = 0;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        const struct limine_memmap_entry *entry = map->entries[i];
        if (entry == NULL || entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        uint64_t end = entry->base + entry->length;
        if (end > top) {
            top = end;
        }
    }
    return top;
}

/* A usable region with room for the bitmap, preferring one above the first
 * megabyte. The low megabyte holds enough legacy oddities that a kernel which
 * puts its own bookkeeping there is asking for a hard bug later. */
static uint64_t place_bitmap(const struct limine_memmap_response *map,
                             uint64_t bytes)
{
    const uint64_t low_limit = 0x100000;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        const struct limine_memmap_entry *entry = map->entries[i];
        if (entry == NULL || entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        if (entry->base >= low_limit && entry->length >= bytes) {
            return entry->base;
        }
    }
    /* Nothing above a megabyte was big enough. Take anything that fits rather
     * than refusing to boot on a small machine. */
    for (uint64_t i = 0; i < map->entry_count; i++) {
        const struct limine_memmap_entry *entry = map->entries[i];
        if (entry != NULL && entry->type == LIMINE_MEMMAP_USABLE &&
            entry->length >= bytes) {
            return entry->base;
        }
    }
    return 0;
}

bool pmmboot_init(const struct limine_memmap_response *map, uint64_t hhdm)
{
    ready = false;
    hhdm_base = hhdm;

    if (map == NULL || map->entry_count == 0 || hhdm == 0) {
        log_line("pmm: no memory map or no direct map, allocator stays empty");
        return false;
    }

    uint64_t top = highest_usable_end(map);
    if (top == 0) {
        log_line("pmm: the memory map named nothing usable");
        return false;
    }

    uint64_t pages = top / PAGE_SIZE;
    uint64_t bytes = pmm_bitmap_bytes_for(pages);
    uint64_t at = place_bitmap(map, bytes);
    if (at == 0) {
        log_line("pmm: no usable region large enough to hold the bitmap");
        return false;
    }

    uint8_t *bitmap = (uint8_t *)(uintptr_t)(at + hhdm);
    pmm_reset(&kernel_pmm, bitmap, bytes, 0, pages);

    /* Only LIMINE_MEMMAP_USABLE becomes available.
     *
     * Bootloader reclaimable memory is deliberately left out even though it
     * really is free memory. The memory map itself, the HHDM response and the
     * framebuffer description are all sitting in it, and this kernel reads
     * them after boot. Handing those pages out would work perfectly until
     * something overwrote the structure describing the screen. Reclaiming it
     * is a real thing to do later, once everything Limine said has been copied
     * somewhere the kernel owns. */
    for (uint64_t i = 0; i < map->entry_count; i++) {
        const struct limine_memmap_entry *entry = map->entries[i];
        if (entry != NULL && entry->type == LIMINE_MEMMAP_USABLE) {
            pmm_add_free(&kernel_pmm, entry->base, entry->length);
        }
    }

    /* The one thing inside usable memory that is already spoken for. */
    pmm_reserve(&kernel_pmm, at, bytes);

    ready = true;
    log_line("pmm: ready");
    log_named_dec("pmm: usable pages", kernel_pmm.free_pages);
    log_named_hex("pmm: bitmap at", at);
    log_named_dec("pmm: bitmap bytes", bytes);
    return true;
}

void *pmm_alloc_zeroed(uint64_t *phys_out)
{
    uint64_t phys = pmm_alloc(&kernel_pmm);
    if (phys == PMM_NONE) {
        if (phys_out != NULL) {
            *phys_out = PMM_NONE;
        }
        return NULL;
    }
    void *page = phys_to_virt(phys);
    memset(page, 0, PAGE_SIZE);
    if (phys_out != NULL) {
        *phys_out = phys;
    }
    return page;
}

void pmmboot_selfcheck(void)
{
    if (!ready) {
        log_line("pmm: selfcheck skipped, no allocator");
        return;
    }

    /* Enough pages to see the allocator move, few enough that a machine with
     * very little memory still passes. */
    enum { TRIED = 8 };
    uint64_t phys[TRIED];
    uint64_t before = kernel_pmm.free_pages;
    unsigned taken = 0;

    for (unsigned i = 0; i < TRIED; i++) {
        uint64_t *page = (uint64_t *)pmm_alloc_zeroed(&phys[i]);
        if (page == NULL) {
            break;
        }
        /* Its own physical address written into it, so a page handed out twice
         * shows up as the wrong value rather than as an identical one. */
        page[0] = phys[i];
        page[PAGE_SIZE / sizeof(uint64_t) - 1] = ~phys[i];
        taken++;
    }

    unsigned wrong = 0;
    for (unsigned i = 0; i < taken; i++) {
        const uint64_t *page = (const uint64_t *)phys_to_virt(phys[i]);
        if (page[0] != phys[i] || page[PAGE_SIZE / sizeof(uint64_t) - 1] != ~phys[i]) {
            wrong++;
        }
        for (unsigned j = 0; j < i; j++) {
            if (phys[j] == phys[i]) {
                wrong++;
            }
        }
    }

    for (unsigned i = 0; i < taken; i++) {
        pmm_free(&kernel_pmm, phys[i]);
    }

    if (taken == TRIED && wrong == 0 && kernel_pmm.free_pages == before &&
        kernel_pmm.bad_frees == 0) {
        log_named_dec("pmm: selfcheck passed, pages still free", kernel_pmm.free_pages);
    } else {
        log_line("pmm: SELFCHECK FAILED");
        log_named_dec("pmm:   pages taken", taken);
        log_named_dec("pmm:   readback wrong", wrong);
        log_named_dec("pmm:   free before", before);
        log_named_dec("pmm:   free after", kernel_pmm.free_pages);
        log_named_dec("pmm:   bad frees", kernel_pmm.bad_frees);
    }
}
