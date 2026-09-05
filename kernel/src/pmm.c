/* The physical page allocator. See kernel/include/pmm.h for why it is a bitmap.
 *
 * Every address here is physical. Nothing in this file dereferences one, which
 * is what lets the whole allocator run as an ordinary host program under test:
 * on the development machine the addresses are made up, and the bitmap is a
 * buffer malloc handed over.
 */
#include "pmm.h"

#include "mem.h"

/* Rounds up without overflowing on a length near the top of the address
 * space, which `(x + PAGE_SIZE - 1) / PAGE_SIZE` would do. */
static uint64_t frames_covering(uint64_t bytes)
{
    uint64_t whole = bytes / PAGE_SIZE;
    return (bytes % PAGE_SIZE != 0) ? whole + 1 : whole;
}

uint64_t pmm_bitmap_bytes_for(uint64_t pages)
{
    return (pages + 7u) / 8u;
}

/* Bitmap index of a frame, or `pages` when the frame is outside the bitmap.
 *
 * Returning the count rather than a boolean out parameter keeps every caller
 * to one bounds check, and `>= pmm->pages` is the same test each time. */
static uint64_t slot_of(const struct pmm *pmm, uint64_t frame)
{
    if (frame < pmm->first_frame) {
        return pmm->pages;
    }
    uint64_t slot = frame - pmm->first_frame;
    return (slot < pmm->pages) ? slot : pmm->pages;
}

static bool slot_in_use(const struct pmm *pmm, uint64_t slot)
{
    return (pmm->bitmap[slot / 8u] & (uint8_t)(1u << (slot % 8u))) != 0;
}

static void mark_used(struct pmm *pmm, uint64_t slot)
{
    if (slot_in_use(pmm, slot)) {
        return;
    }
    pmm->bitmap[slot / 8u] |= (uint8_t)(1u << (slot % 8u));
    pmm->free_pages--;
}

static void mark_free(struct pmm *pmm, uint64_t slot)
{
    if (!slot_in_use(pmm, slot)) {
        return;
    }
    pmm->bitmap[slot / 8u] &= (uint8_t)~(1u << (slot % 8u));
    pmm->free_pages++;
}

void pmm_reset(struct pmm *pmm, uint8_t *bitmap, uint64_t bitmap_bytes,
               uint64_t first_frame, uint64_t pages)
{
    /* A bitmap smaller than the page count it is asked to cover would be
     * written past its end by the first reservation. Shrink the coverage to
     * what the buffer can actually hold rather than trusting the caller. */
    uint64_t fits = bitmap_bytes * 8u;
    if (pages > fits) {
        pages = fits;
    }

    pmm->bitmap = bitmap;
    pmm->bitmap_bytes = bitmap_bytes;
    pmm->first_frame = first_frame;
    pmm->pages = pages;
    pmm->free_pages = 0;
    pmm->hint = 0;
    pmm->bad_frees = 0;

    /* Everything starts in use. See the header: memory becomes available by
     * being named in the memory map, never by being left out of it. */
    memset(bitmap, 0xFF, (size_t)bitmap_bytes);
}

void pmm_add_free(struct pmm *pmm, uint64_t base, uint64_t length)
{
    if (length < PAGE_SIZE) {
        return;
    }
    /* Only whole pages inside the range. A range starting mid page does not
     * make that page available, because the rest of it belongs to somebody. */
    uint64_t first = frames_covering(base);
    uint64_t end = (base + length) / PAGE_SIZE;

    /* Frame zero is never available, whatever the memory map says.
     *
     * PMM_NONE is zero, so a successful allocation of physical page zero and a
     * failure are the same value, and the caller would treat a real page as an
     * out of memory error. Enforced here rather than trusted of the caller,
     * because it is the one place every page has to pass through to become
     * available. No real memory map offers page zero anyway: it holds the real
     * mode interrupt vectors and the BIOS data area. */
    if (first == 0) {
        first = 1;
    }

    for (uint64_t frame = first; frame < end; frame++) {
        uint64_t slot = slot_of(pmm, frame);
        if (slot < pmm->pages) {
            mark_free(pmm, slot);
        }
    }
}

void pmm_reserve(struct pmm *pmm, uint64_t base, uint64_t length)
{
    if (length == 0) {
        return;
    }
    /* Every page the range touches, including both partial ends. Reserving too
     * much loses at most two pages. Reserving too little hands out memory
     * something else is already using. */
    uint64_t first = base / PAGE_SIZE;
    uint64_t end = frames_covering(base + length);

    for (uint64_t frame = first; frame < end; frame++) {
        uint64_t slot = slot_of(pmm, frame);
        if (slot < pmm->pages) {
            mark_used(pmm, slot);
        }
    }
}

uint64_t pmm_alloc(struct pmm *pmm)
{
    if (pmm->free_pages == 0) {
        return PMM_NONE;
    }
    /* From the hint to the end, then from the beginning to the hint. Two
     * bounded passes rather than a wrapping index, so the loop cannot run
     * forever if the free count and the bitmap ever disagree. */
    for (uint64_t slot = pmm->hint; slot < pmm->pages; slot++) {
        if (!slot_in_use(pmm, slot)) {
            mark_used(pmm, slot);
            pmm->hint = slot + 1;
            return (pmm->first_frame + slot) * PAGE_SIZE;
        }
    }
    for (uint64_t slot = 0; slot < pmm->hint && slot < pmm->pages; slot++) {
        if (!slot_in_use(pmm, slot)) {
            mark_used(pmm, slot);
            pmm->hint = slot + 1;
            return (pmm->first_frame + slot) * PAGE_SIZE;
        }
    }
    /* free_pages said there was one and there was not. Report empty rather
     * than looping: the count is corrected on the next free. */
    return PMM_NONE;
}

void pmm_free(struct pmm *pmm, uint64_t phys)
{
    uint64_t slot = slot_of(pmm, phys / PAGE_SIZE);
    if (slot >= pmm->pages) {
        pmm->bad_frees++;
        return;
    }
    if (!slot_in_use(pmm, slot)) {
        /* Freeing twice would add a page to the free count that is already in
         * it, and the same page would then be handed to two callers. */
        pmm->bad_frees++;
        return;
    }
    mark_free(pmm, slot);
    /* Reuse it next, which keeps a recently touched page warm and makes an
     * alloc after a free cheap to find. */
    if (slot < pmm->hint) {
        pmm->hint = slot;
    }
}

bool pmm_is_free(const struct pmm *pmm, uint64_t phys)
{
    uint64_t slot = slot_of(pmm, phys / PAGE_SIZE);
    if (slot >= pmm->pages) {
        return false;
    }
    return !slot_in_use(pmm, slot);
}
