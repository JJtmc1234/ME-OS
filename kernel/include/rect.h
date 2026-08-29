/* A rectangle that moves.
 *
 * The movement is a pure function of elapsed time, kept apart from drawing so
 * it can be tested without a framebuffer. Speed is in pixels per second, and
 * the leftover time between whole pixels is carried, so moving in many small
 * steps ends up in the same place as moving in one large one.
 */
#ifndef ME_RECT_H
#define ME_RECT_H

#include <stdbool.h>
#include <stdint.h>

struct moving_rect {
    int64_t x;
    int64_t y;
    uint64_t width;
    uint64_t height;
    uint64_t speed;        /* pixels per second */
    int32_t direction;     /* +1 rightwards, -1 leftwards */
    uint64_t carried;      /* timer counts not yet worth a whole pixel */
};

/* Advances the rectangle by however far `elapsed` counts of a `hz` timer are
 * worth. Reflects off the left and right edges so the whole rectangle stays on
 * screen. Returns true if it ended up somewhere new.
 */
bool rect_advance(struct moving_rect *rect, uint64_t elapsed, uint64_t hz,
                  uint64_t screen_width);

/* Moves the rectangle by hand. Horizontally it may go anywhere the screen
 * allows; vertically it stays between `min_y` and `max_y`, which is how the
 * caller keeps it out of the text and away from anything else it would paint
 * over. Crossing either end wraps to the other end while preserving the
 * remainder of the step. Returns true if it ended up somewhere new.
 */
bool rect_nudge(struct moving_rect *rect, int64_t dx, int64_t dy,
                uint64_t screen_width, int64_t min_y, int64_t max_y);

#endif /* ME_RECT_H */
