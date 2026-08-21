#include "rect.h"

#include <stddef.h>

bool rect_advance(struct moving_rect *rect, uint64_t elapsed, uint64_t hz,
                  uint64_t screen_width)
{
    if (rect == NULL || hz == 0 || rect->speed == 0 || elapsed == 0) {
        return false;
    }
    if (rect->width == 0 || rect->width > screen_width) {
        return false;  /* it could never fit, so refuse to move it */
    }

    rect->carried += elapsed * rect->speed;
    const int64_t steps = (int64_t)(rect->carried / hz);
    rect->carried -= (uint64_t)steps * hz;
    if (steps == 0) {
        return false;
    }

    const int64_t before = rect->x;
    const int64_t limit = (int64_t)(screen_width - rect->width);
    int64_t moved = rect->x + steps * (rect->direction < 0 ? -1 : 1);

    /* Reflect off each wall, however many times the step crosses one. */
    while (moved < 0 || moved > limit) {
        if (moved < 0) {
            moved = -moved;
            rect->direction = 1;
        }
        if (moved > limit) {
            moved = 2 * limit - moved;
            rect->direction = -1;
        }
        if (limit == 0) {
            moved = 0;  /* no room to move, do not spin here */
            break;
        }
    }

    rect->x = moved;
    return rect->x != before;
}
