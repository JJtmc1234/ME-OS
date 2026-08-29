#include "rect.h"

#include <stddef.h>

static int64_t clamp(int64_t value, int64_t low, int64_t high)
{
    if (high < low) {
        return low;
    }
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

/* Adds one signed step on an inclusive interval and wraps at either end.
 * `span` is kept within INT64_MAX by rect_nudge, so taking the remainder is
 * well-defined even when delta is INT64_MIN. Splitting forward and backward
 * movement avoids overflowing while doing the addition itself. */
static int64_t wrap_step(int64_t value, int64_t delta,
                         int64_t low, int64_t high)
{
    if (high <= low) {
        return low;
    }

    value = clamp(value, low, high);
    const int64_t span = high - low + 1;
    const int64_t step = delta % span;

    if (step > 0) {
        const int64_t room = high - value;
        return step <= room ? value + step : low + (step - room - 1);
    }
    if (step < 0) {
        /* Written this way so INT64_MIN is never negated. */
        const uint64_t back = (uint64_t)(-(step + 1)) + 1u;
        const uint64_t room = (uint64_t)(value - low);
        return back <= room
            ? value - (int64_t)back
            : high - (int64_t)(back - room - 1u);
    }
    return value;
}

bool rect_nudge(struct moving_rect *rect, int64_t dx, int64_t dy,
                uint64_t screen_width, int64_t min_y, int64_t max_y)
{
    if (rect == NULL || rect->width == 0 || rect->width > screen_width ||
        screen_width > (uint64_t)INT64_MAX) {
        return false;
    }

    const int64_t before_x = rect->x;
    const int64_t before_y = rect->y;

    const int64_t max_x = (int64_t)(screen_width - rect->width);
    /* Inclusive spans must fit in a signed value before wrap_step can take a
     * remainder. Real framebuffer and corridor sizes are many orders smaller. */
    if (max_x == INT64_MAX ||
        (max_y >= min_y &&
         (uint64_t)max_y - (uint64_t)min_y >= (uint64_t)INT64_MAX)) {
        return false;
    }

    rect->x = wrap_step(rect->x, dx, 0, max_x);
    rect->y = wrap_step(rect->y, dy, min_y, max_y);

    return rect->x != before_x || rect->y != before_y;
}

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
