#include "compositor.h"

bool compositor_compose(const struct window_manager *manager,
                        struct surface *target, uint32_t background)
{
    if (manager == NULL || !surface_valid(target)) {
        return false;
    }

    surface_clear(target, background);
    for (size_t z = 0; z < window_count(manager); z++) {
        const struct window *window = window_at_z(manager, z);
        if (window == NULL || window->minimized || window->surface == NULL) {
            continue;
        }
        surface_blit(target, window->surface,
                     window->geometry.x, window->geometry.y);
    }
    return true;
}
