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

bool rect_contains(const struct moving_rect *rect, int64_t x, int64_t y)
{
    if (rect == NULL || rect->width == 0 || rect->height == 0 ||
        x < rect->x || y < rect->y) {
        return false;
    }

    const uint64_t across = (uint64_t)x - (uint64_t)rect->x;
    const uint64_t down = (uint64_t)y - (uint64_t)rect->y;
    return across < rect->width && down < rect->height;
}

void rect_drag_reset(struct rect_drag *drag)
{
    if (drag != NULL) {
        drag->active = false;
        drag->offset_x = 0;
        drag->offset_y = 0;
    }
}

bool rect_drag_begin(struct rect_drag *drag, const struct moving_rect *rect,
                     int64_t pointer_x, int64_t pointer_y)
{
    if (drag == NULL || drag->active ||
        !rect_contains(rect, pointer_x, pointer_y)) {
        return false;
    }

    const uint64_t across = (uint64_t)pointer_x - (uint64_t)rect->x;
    const uint64_t down = (uint64_t)pointer_y - (uint64_t)rect->y;
    if (across > (uint64_t)INT64_MAX || down > (uint64_t)INT64_MAX) {
        return false;
    }

    drag->active = true;
    drag->offset_x = (int64_t)across;
    drag->offset_y = (int64_t)down;
    return true;
}

static int64_t subtract_saturated(int64_t value, int64_t amount)
{
    if (amount > 0 && value < INT64_MIN + amount) {
        return INT64_MIN;
    }
    if (amount < 0 && value > INT64_MAX + amount) {
        return INT64_MAX;
    }
    return value - amount;
}

bool rect_drag_move(const struct rect_drag *drag, struct moving_rect *rect,
                    int64_t pointer_x, int64_t pointer_y,
                    uint64_t screen_width, int64_t min_y, int64_t max_y)
{
    if (drag == NULL || !drag->active || rect == NULL ||
        rect->width == 0 || rect->width > screen_width ||
        screen_width > (uint64_t)INT64_MAX) {
        return false;
    }

    const int64_t before_x = rect->x;
    const int64_t before_y = rect->y;
    const int64_t max_x = (int64_t)(screen_width - rect->width);
    rect->x = clamp(subtract_saturated(pointer_x, drag->offset_x), 0, max_x);
    rect->y = clamp(subtract_saturated(pointer_y, drag->offset_y), min_y, max_y);
    return rect->x != before_x || rect->y != before_y;
}

bool rect_drag_end(struct rect_drag *drag)
{
    if (drag == NULL || !drag->active) {
        return false;
    }
    rect_drag_reset(drag);
    return true;
}
