#include "pointer.h"

#include <stddef.h>

static int64_t clamp(int64_t value, int64_t high)
{
    if (value < 0) {
        return 0;
    }
    if (value > high) {
        return high;
    }
    return value;
}

void pointer_init(struct pointer *p, int64_t x, int64_t y,
                  uint64_t width, uint64_t height)
{
    if (p == NULL || width == 0 || height == 0) {
        return;
    }
    p->x = clamp(x, (int64_t)width - 1);
    p->y = clamp(y, (int64_t)height - 1);
}

bool pointer_move(struct pointer *p, int32_t dx, int32_t dy,
                  uint64_t width, uint64_t height)
{
    if (p == NULL || width == 0 || height == 0) {
        return false;
    }

    const int64_t before_x = p->x;
    const int64_t before_y = p->y;

    p->x = clamp(p->x + dx, (int64_t)width - 1);
    p->y = clamp(p->y + dy, (int64_t)height - 1);

    return p->x != before_x || p->y != before_y;
}
