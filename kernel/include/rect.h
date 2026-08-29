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

/* The state retained between a left-button press and release. The offset is
 * where inside the rectangle it was picked up, so beginning a drag does not
 * snap the rectangle's top-left corner to the pointer. */
struct rect_drag {
    bool active;
    int64_t offset_x;
    int64_t offset_y;
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

/* Pure M11 hit testing and drag state. A drag begins only inside the filled
 * rectangle, follows the pointer while keeping the press offset, clamps the
 * whole rectangle to the screen/corridor, and ends explicitly on release. */
bool rect_contains(const struct moving_rect *rect, int64_t x, int64_t y);
void rect_drag_reset(struct rect_drag *drag);
bool rect_drag_begin(struct rect_drag *drag, const struct moving_rect *rect,
                     int64_t pointer_x, int64_t pointer_y);
bool rect_drag_move(const struct rect_drag *drag, struct moving_rect *rect,
                    int64_t pointer_x, int64_t pointer_y,
                    uint64_t screen_width, int64_t min_y, int64_t max_y);
bool rect_drag_end(struct rect_drag *drag);

#endif /* ME_RECT_H */
