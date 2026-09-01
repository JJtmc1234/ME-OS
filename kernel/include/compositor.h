/* Opaque software composition from window-local surfaces into one target. */
#ifndef ME_COMPOSITOR_H
#define ME_COMPOSITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "region.h"
#include "surface.h"
#include "window.h"

bool compositor_compose(const struct window_manager *manager,
                        struct surface *target, uint32_t background);

/* The same, for one rectangle of the target in target coordinates.
 *
 * `compositor_compose` is this over the whole target, so there is one
 * composition rule rather than two. The point of the narrow form is that moving
 * the cursor eight pixels does not have to clear a million of them and blit
 * every window back over the top. See M16 in docs/milestones.md.
 *
 * Composed from the window surfaces every time rather than from saved pixels.
 * Saving what was under the cursor and putting it back is faster still and is
 * wrong the moment a window repaints underneath it, which would leave a stamp of
 * stale content wherever the cursor had been. */
bool compositor_compose_region(const struct window_manager *manager,
                               struct surface *target, uint32_t background,
                               struct region region);

#endif /* ME_COMPOSITOR_H */
