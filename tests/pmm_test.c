/* Host tests for the M29 physical page allocator.
 *
 * Everything after this milestone gets its memory from here: page tables,
 * process address spaces, kernel stacks, the pages a program is loaded into.
 * So the failures worth writing tests against are the ones that would not look
 * like a failure. Handing the same page to two callers does not crash at the
 * moment it happens, it crashes later somewhere unrelated, and the bug looks
 * like whatever was unlucky enough to be using that page.
 *
 * The whole allocator runs here because it never dereferences a physical
 * address. The addresses below are invented and the bitmap is a local array.
 */
#include <stdio.h>
#include <string.h>

#include "pmm.h"

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

/* A megabyte of pages starting at physical zero, unless said otherwise. */
#define TEST_PAGES 256u
static uint8_t bits[TEST_PAGES / 8u];
static struct pmm pmm;

static void fresh(void)
{
    pmm_reset(&pmm, bits, sizeof bits, 0, TEST_PAGES);
}

int main(void)
{
    printf("a bitmap covers the pages it was sized for\n");
    check(pmm_bitmap_bytes_for(8) == 1, "eight pages need one byte");
    check(pmm_bitmap_bytes_for(9) == 2, "nine need two, not one and an eighth");
    check(pmm_bitmap_bytes_for(0) == 0, "no pages need no bytes");

    printf("memory starts unavailable and becomes available by being named\n");
    fresh();
    check(pmm.free_pages == 0, "nothing is free before the memory map is read");
    check(pmm_alloc(&pmm) == PMM_NONE, "so allocating gets nothing");
    check(!pmm_is_free(&pmm, 0), "and no page reports itself free");

    printf("page zero is never available, whatever it is told\n");
    /* PMM_NONE is zero, so handing out physical page zero and failing to
     * allocate would be the same answer. This is the guard that makes the
     * sentinel safe, and it has to hold even when the caller says otherwise. */
    fresh();
    pmm_add_free(&pmm, 0, 16 * PAGE_SIZE);
    check(!pmm_is_free(&pmm, 0), "a range starting at zero does not free page zero");
    check(pmm_alloc(&pmm) != PMM_NONE, "the range is otherwise usable");
    fresh();
    pmm_add_free(&pmm, 0, TEST_PAGES * PAGE_SIZE);
    for (uint64_t taken_page = pmm_alloc(&pmm); taken_page != PMM_NONE;
         taken_page = pmm_alloc(&pmm)) {
        if (taken_page == 0) {
            check(0, "an allocation returned page zero, which means failure");
            break;
        }
    }
    check(1, "no allocation ever returned zero");

    printf("adding a usable range makes exactly that range available\n");
    fresh();
    pmm_add_free(&pmm, 0, 16 * PAGE_SIZE);
    check(pmm.free_pages == 15, "sixteen pages of range, less the reserved page zero");
    check(pmm_is_free(&pmm, PAGE_SIZE), "the first usable one is free");
    check(pmm_is_free(&pmm, 15 * PAGE_SIZE), "and so is the last");
    check(!pmm_is_free(&pmm, 16 * PAGE_SIZE), "the one past the end is not");

    printf("a range that does not start on a page boundary loses the partial page\n");
    fresh();
    /* Starts half way through page 0 and ends half way through page 4. Only
     * pages 1, 2 and 3 are wholly inside it. */
    pmm_add_free(&pmm, PAGE_SIZE / 2, 4 * PAGE_SIZE);
    check(pmm.free_pages == 3, "three whole pages out of a four page range");
    check(!pmm_is_free(&pmm, 0), "the page it starts inside stays unavailable");
    check(pmm_is_free(&pmm, PAGE_SIZE), "the first whole page is available");
    check(!pmm_is_free(&pmm, 4 * PAGE_SIZE), "so does the one it ends inside");

    printf("a range smaller than a page makes nothing available\n");
    fresh();
    pmm_add_free(&pmm, 0, PAGE_SIZE - 1);
    check(pmm.free_pages == 0, "half a page is not a page");

    printf("reserving takes every page the range touches\n");
    fresh();
    pmm_add_free(&pmm, 0, 16 * PAGE_SIZE);
    /* The opposite rounding to add_free, and deliberately so. */
    pmm_reserve(&pmm, PAGE_SIZE + 8, 2 * PAGE_SIZE);
    check(!pmm_is_free(&pmm, PAGE_SIZE), "the page it starts inside is taken");
    check(!pmm_is_free(&pmm, 2 * PAGE_SIZE), "the whole page inside is taken");
    check(!pmm_is_free(&pmm, 3 * PAGE_SIZE), "the page it ends inside is taken");
    check(pmm_is_free(&pmm, 4 * PAGE_SIZE), "and the next one is left alone");
    check(pmm.free_pages == 12, "three pages went out of the fifteen usable");

    printf("reserving twice does not lose the page twice\n");
    fresh();
    pmm_add_free(&pmm, 0, 16 * PAGE_SIZE);
    pmm_reserve(&pmm, PAGE_SIZE, PAGE_SIZE);
    pmm_reserve(&pmm, PAGE_SIZE, PAGE_SIZE);
    check(pmm.free_pages == 14, "the count moved once, not twice");

    printf("a reserved page is never handed out\n");
    fresh();
    pmm_add_free(&pmm, 0, 4 * PAGE_SIZE);
    pmm_reserve(&pmm, 0, PAGE_SIZE);
    pmm_reserve(&pmm, 2 * PAGE_SIZE, PAGE_SIZE);
    uint64_t first = pmm_alloc(&pmm);
    uint64_t second = pmm_alloc(&pmm);
    check(first == PAGE_SIZE, "the first free page is page one");
    check(second == 3 * PAGE_SIZE, "and the next skips the reserved one");
    check(pmm_alloc(&pmm) == PMM_NONE, "then there are none left");

    printf("no page is ever handed out twice\n");
    fresh();
    pmm_add_free(&pmm, 0, TEST_PAGES * PAGE_SIZE);
    static uint8_t seen[TEST_PAGES];
    memset(seen, 0, sizeof seen);
    uint64_t handed = 0;
    for (;;) {
        uint64_t page = pmm_alloc(&pmm);
        if (page == PMM_NONE) {
            break;
        }
        uint64_t frame = page / PAGE_SIZE;
        if (frame >= TEST_PAGES || seen[frame] != 0) {
            check(0, "a page came back twice, or out of range");
            break;
        }
        seen[frame] = 1;
        handed++;
    }
    check(handed == TEST_PAGES - 1, "every usable page was handed out exactly once");
    check(pmm.free_pages == 0, "and the count agrees that none are left");

    printf("running out says so instead of returning something wrong\n");
    check(pmm_alloc(&pmm) == PMM_NONE, "an empty allocator gives PMM_NONE");
    check(pmm_alloc(&pmm) == PMM_NONE, "and keeps saying so rather than wrapping");

    printf("freeing gives a page back, once\n");
    fresh();
    pmm_add_free(&pmm, 0, 4 * PAGE_SIZE);
    uint64_t page = pmm_alloc(&pmm);
    check(pmm.free_pages == 2, "two left after taking one of the three");
    pmm_free(&pmm, page);
    check(pmm.free_pages == 3, "and three again after giving it back");
    check(pmm.bad_frees == 0, "which was not a bad free");
    check(pmm_alloc(&pmm) == page, "the freed page is the next one out");

    printf("a double free is refused rather than counted\n");
    fresh();
    pmm_add_free(&pmm, 0, 4 * PAGE_SIZE);
    uint64_t taken = pmm_alloc(&pmm);
    pmm_free(&pmm, taken);
    pmm_free(&pmm, taken);
    check(pmm.free_pages == 3, "the count did not gain a page that was not lost");
    check(pmm.bad_frees == 1, "and the refusal was recorded");

    printf("freeing something never allocated is refused\n");
    fresh();
    pmm_add_free(&pmm, 0, 4 * PAGE_SIZE);
    pmm_free(&pmm, 2 * PAGE_SIZE);
    check(pmm.bad_frees == 1, "a page that was already free is a bad free");
    check(pmm.free_pages == 3, "and the count is unchanged");

    printf("freeing outside the bitmap is refused rather than writing there\n");
    fresh();
    pmm_add_free(&pmm, 0, 4 * PAGE_SIZE);
    pmm_free(&pmm, (uint64_t)TEST_PAGES * PAGE_SIZE);
    check(pmm.bad_frees == 1, "past the end is refused");
    pmm_free(&pmm, 0xFFFFFFFFFFFFF000ull);
    check(pmm.bad_frees == 2, "and so is an address near the top of memory");
    check(pmm.free_pages == 3, "neither changed what is available");

    printf("a bitmap that starts high does not cover the space below it\n");
    fresh();
    /* Memory beginning at 1 MiB, which is where a PC's usable RAM often
     * starts once the BIOS areas below are accounted for. */
    pmm_reset(&pmm, bits, sizeof bits, 256, TEST_PAGES);
    pmm_add_free(&pmm, 256 * PAGE_SIZE, 8 * PAGE_SIZE);
    check(pmm.free_pages == 8, "eight pages above the line are available");
    check(pmm_alloc(&pmm) == 256 * PAGE_SIZE, "and the first is at the line");
    pmm_add_free(&pmm, 0, 16 * PAGE_SIZE);
    check(pmm.free_pages == 7, "memory below the bitmap is ignored, not wrapped");
    check(!pmm_is_free(&pmm, 0), "and page zero is still not free");

    printf("a bitmap too small for the page count covers what it can hold\n");
    /* The guard that stops a caller's arithmetic mistake becoming a write past
     * the end of the buffer. */
    pmm_reset(&pmm, bits, 2, 0, TEST_PAGES);
    check(pmm.pages == 16, "two bytes cover sixteen pages, not two hundred");
    pmm_add_free(&pmm, 0, TEST_PAGES * PAGE_SIZE);
    check(pmm.free_pages == 15, "and only those sixteen, less page zero, become available");

    printf(failures == 0 ? "\nphysical page allocator checks passed\n"
                         : "\nphysical page allocator checks FAILED\n");
    return failures == 0 ? 0 : 1;
}
