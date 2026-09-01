/* A rectangle of screen that something has changed.
 *
 * The compositor and the framebuffer both need to say "only this part moved".
 * Before this existed they had no way to, so every cursor step cleared the whole
 * desktop, blitted every window over it, and wrote the entire screen out through
 * the framebuffer again. See M16 in docs/milestones.md.
 *
 * Width and height are signed on purpose. Clipping two rectangles against each
 * other naturally produces a negative size when they miss, and an unsigned type
 * would turn that into an enormous positive one, which is the exact shape of an
 * out of bounds write.
 */
#ifndef ME_REGION_H
#define ME_REGION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct region {
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
};

/* Nothing at all. Every operation below treats an empty region as covering no
 * pixel, whichever way it came to be empty. */
struct region region_none(void);
struct region region_make(int64_t x, int64_t y, int64_t width, int64_t height);
bool region_empty(const struct region *region);

/* The pixels in both. Empty when they do not touch. */
struct region region_intersect(struct region a, struct region b);

/* The smallest rectangle holding both. Covers pixels in neither when they are
 * far apart, which is why a caller with two distant regions should present them
 * separately rather than joining them. */
struct region region_union(struct region a, struct region b);

bool region_overlaps(struct region a, struct region b);

/* Trimmed to a surface or screen of this size, starting at the origin. */
struct region region_clip(struct region region, int64_t width, int64_t height);

/* Grown by `by` pixels on every side, so a shape drawn with a one pixel outline
 * is covered by the region computed from the shape itself. */
struct region region_expand(struct region region, int64_t by);

/* How many pixels the region covers. Zero when empty. Used by the counters that
 * answer whether the mouse actually got cheaper. */
uint64_t region_area(struct region region);

#endif /* ME_REGION_H */
