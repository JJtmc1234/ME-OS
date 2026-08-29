/* Opaque software composition from window-local surfaces into one target. */
#ifndef ME_COMPOSITOR_H
#define ME_COMPOSITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "surface.h"
#include "window.h"

bool compositor_compose(const struct window_manager *manager,
                        struct surface *target, uint32_t background);

#endif /* ME_COMPOSITOR_H */
