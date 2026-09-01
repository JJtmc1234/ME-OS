#include "compositor.h"

bool compositor_compose(const struct window_manager *manager,
                        struct surface *target, uint32_t background)
{
    if (!surface_valid(target)) {
        return false;
    }
    return compositor_compose_region(
        manager, target, background,
        region_make(0, 0, (int64_t)target->width, (int64_t)target->height));
}

bool compositor_compose_region(const struct window_manager *manager,
                               struct surface *target, uint32_t background,
                               struct region region)
{
    if (manager == NULL || !surface_valid(target)) {
        return false;
    }

    region = region_clip(region, (int64_t)target->width, (int64_t)target->height);
    /* Not a failure. A caller asking about a part of the screen that is not
     * there has nothing to compose, and saying so as an error would make every
     * caller check something it cannot act on. */
    if (region_empty(&region)) {
        return true;
    }

    surface_fill_rect(target, region.x, region.y,
                      (uint32_t)region.width, (uint32_t)region.height, background);

    for (size_t z = 0; z < window_count(manager); z++) {
        const struct window *window = window_at_z(manager, z);
        if (window == NULL || window->minimized || window->surface == NULL) {
            continue;
        }
        /* Bottom to top, and every window still goes through the same call, so
         * a window that does not reach the region copies nothing rather than
         * being skipped by a second overlap rule that could disagree with the
         * clipping one. */
        surface_blit_clipped(target, window->surface,
                             window->geometry.x, window->geometry.y, region);
    }
    return true;
}
