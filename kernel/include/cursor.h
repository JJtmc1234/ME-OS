/* Drawing the compositor-owned pointer into a software surface. */
#ifndef ME_CURSOR_H
#define ME_CURSOR_H

#include <stdint.h>

#include "surface.h"

#define CURSOR_WIDTH  8
#define CURSOR_HEIGHT 12

/* Draws a clipped arrow in surface-local coordinates. The cursor is an
 * overlay on the composed desktop, never an app-owned framebuffer patch. */
void cursor_draw(struct surface *surface, int64_t x, int64_t y,
                 uint32_t fill, uint32_t outline);

#endif /* ME_CURSOR_H */
