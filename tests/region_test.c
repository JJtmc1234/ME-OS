/* Host tests for the M16 dirty region arithmetic.
 *
 * Every rectangle the compositor clears and the framebuffer writes is built out
 * of these, so a mistake here paints outside a window rather than merely drawing
 * the wrong thing. The negative size cases matter most: two rectangles that miss
 * each other produce one, and an unsigned width would turn it into an enormous
 * positive one, which is the exact shape of an out of bounds write.
 */
#include <stdio.h>

#include "region.h"

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

static int same(struct region a, int64_t x, int64_t y, int64_t w, int64_t h)
{
    return a.x == x && a.y == y && a.width == w && a.height == h;
}

int main(void)
{
    printf("a region with no size is empty however it was spelled\n");
    struct region none = region_none();
    check(region_empty(&none), "region_none is empty");
    check(region_empty(NULL), "no region at all is empty rather than a crash");
    struct region zero_wide = region_make(10, 10, 0, 5);
    check(region_empty(&zero_wide), "zero width is empty");
    struct region negative = region_make(10, 10, -4, 5);
    check(region_empty(&negative), "a negative width is empty, not enormous");
    check(same(negative, 0, 0, 0, 0), "and it is normalised rather than kept");
    check(region_area(negative) == 0, "an empty region covers no pixels");
    check(region_area(region_make(0, 0, 8, 12)) == 96, "and a real one covers its own");

    printf("intersection keeps only the pixels in both\n");
    struct region a = region_make(0, 0, 100, 100);
    struct region b = region_make(50, 50, 100, 100);
    check(same(region_intersect(a, b), 50, 50, 50, 50), "overlapping corners");
    check(region_overlaps(a, b), "and they are reported as overlapping");

    struct region far = region_make(500, 500, 10, 10);
    struct region missed = region_intersect(a, far);
    check(region_empty(&missed), "rectangles that miss intersect to nothing");
    check(!region_overlaps(a, far), "and are not reported as overlapping");

    /* The case that would be a buffer overrun. Touching edges share no pixel,
     * because a region starting at x covers x to x+width-1. */
    struct region left = region_make(0, 0, 10, 10);
    struct region right = region_make(10, 0, 10, 10);
    struct region touching = region_intersect(left, right);
    check(region_empty(&touching), "regions that only touch share no pixel");
    check(!region_overlaps(left, right), "so they do not overlap");

    struct region inside = region_make(20, 20, 5, 5);
    check(same(region_intersect(a, inside), 20, 20, 5, 5),
          "one wholly inside another is itself");

    struct region with_nothing = region_intersect(a, region_none());
    check(region_empty(&with_nothing), "intersecting with nothing gives nothing");

    printf("union covers both, and an empty side contributes nothing\n");
    check(same(region_union(left, right), 0, 0, 20, 10), "two side by side");
    check(same(region_union(a, far), 0, 0, 510, 510), "and two far apart, loosely");

    /* The bug this guards. An empty region has zeroed coordinates, so treating
     * it as a real rectangle would drag every union back to the origin and make
     * the cursor repaint a corridor to the top left corner on its first move. */
    struct region only_far = region_union(region_none(), far);
    check(same(only_far, 500, 500, 10, 10),
          "an empty side does not drag the union to the origin");
    check(same(region_union(far, region_none()), 500, 500, 10, 10),
          "whichever side it is on");
    struct region neither = region_union(region_none(), region_none());
    check(region_empty(&neither), "two empty sides stay empty");

    printf("clipping trims to a surface and never past its edges\n");
    check(same(region_clip(region_make(-10, -10, 40, 40), 100, 100), 0, 0, 30, 30),
          "a region starting off the top left");
    check(same(region_clip(region_make(90, 90, 40, 40), 100, 100), 90, 90, 10, 10),
          "and one running off the bottom right");
    struct region outside = region_clip(region_make(200, 200, 10, 10), 100, 100);
    check(region_empty(&outside), "one entirely off the surface clips to nothing");
    struct region behind = region_clip(region_make(-50, 0, 10, 10), 100, 100);
    check(region_empty(&behind), "and so does one entirely before it");

    printf("expanding covers an outline drawn around a shape\n");
    check(same(region_expand(region_make(10, 10, 8, 12), 1), 9, 9, 10, 14),
          "one pixel on every side");
    struct region grown_nothing = region_expand(region_none(), 4);
    check(region_empty(&grown_nothing), "growing nothing still gives nothing");

    if (failures == 0) {
        printf("\ndirty region checks passed\n");
        return 0;
    }
    printf("\n%d dirty region check(s) failed\n", failures);
    return 1;
}
