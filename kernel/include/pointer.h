/* Where the pointer is.
 *
 * Deliberately separate from both the mouse and the cursor drawing: this is
 * the state, the mouse is one way to change it, and the cursor is one way to
 * show it. A later milestone can add a second input without touching either.
 */
#ifndef ME_POINTER_H
#define ME_POINTER_H

#include <stdbool.h>
#include <stdint.h>

struct pointer {
    int64_t x;
    int64_t y;
};

/* Starts the pointer at a given point, clamped to the screen. */
void pointer_init(struct pointer *p, int64_t x, int64_t y,
                  uint64_t width, uint64_t height);

/* Applies a movement and clamps to the screen. Returns true if the pointer
 * ended up somewhere new, so a caller can avoid redrawing for nothing. */
bool pointer_move(struct pointer *p, int32_t dx, int32_t dy,
                  uint64_t width, uint64_t height);

#endif /* ME_POINTER_H */
