#include "region.h"

static int64_t max_of(int64_t a, int64_t b) { return a > b ? a : b; }
static int64_t min_of(int64_t a, int64_t b) { return a < b ? a : b; }

struct region region_none(void)
{
    const struct region none = { 0, 0, 0, 0 };
    return none;
}

struct region region_make(int64_t x, int64_t y, int64_t width, int64_t height)
{
    if (width <= 0 || height <= 0) {
        return region_none();
    }
    const struct region region = { x, y, width, height };
    return region;
}

bool region_empty(const struct region *region)
{
    return region == NULL || region->width <= 0 || region->height <= 0;
}

struct region region_intersect(struct region a, struct region b)
{
    if (region_empty(&a) || region_empty(&b)) {
        return region_none();
    }
    const int64_t left = max_of(a.x, b.x);
    const int64_t top = max_of(a.y, b.y);
    const int64_t right = min_of(a.x + a.width, b.x + b.width);
    const int64_t bottom = min_of(a.y + a.height, b.y + b.height);
    return region_make(left, top, right - left, bottom - top);
}

struct region region_union(struct region a, struct region b)
{
    /* An empty side contributes nothing rather than dragging the answer to the
     * origin, which is what using its zeroed coordinates would do. */
    if (region_empty(&a)) {
        return region_empty(&b) ? region_none() : b;
    }
    if (region_empty(&b)) {
        return a;
    }
    const int64_t left = min_of(a.x, b.x);
    const int64_t top = min_of(a.y, b.y);
    const int64_t right = max_of(a.x + a.width, b.x + b.width);
    const int64_t bottom = max_of(a.y + a.height, b.y + b.height);
    return region_make(left, top, right - left, bottom - top);
}

bool region_overlaps(struct region a, struct region b)
{
    const struct region both = region_intersect(a, b);
    return !region_empty(&both);
}

struct region region_clip(struct region region, int64_t width, int64_t height)
{
    return region_intersect(region, region_make(0, 0, width, height));
}

struct region region_expand(struct region region, int64_t by)
{
    if (region_empty(&region)) {
        return region_none();
    }
    return region_make(region.x - by, region.y - by,
                       region.width + 2 * by, region.height + 2 * by);
}

uint64_t region_area(struct region region)
{
    if (region_empty(&region)) {
        return 0;
    }
    return (uint64_t)region.width * (uint64_t)region.height;
}
