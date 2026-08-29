#include "cursor.h"

/* An arrow, one bit per pixel, bit 7 leftmost. The outline is worked out from
 * the shape at draw time rather than stored, so the two can never disagree. */
static const uint8_t arrow[CURSOR_HEIGHT] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC,
    0xFE, 0xFF, 0xF8, 0xD8, 0x8C, 0x0C,
};

static bool arrow_pixel(int64_t col, int64_t row)
{
    if (row < 0 || row >= CURSOR_HEIGHT || col < 0 || col >= CURSOR_WIDTH) {
        return false;
    }
    return (arrow[row] & (0x80u >> col)) != 0;
}

void cursor_draw(struct surface *surface, int64_t x, int64_t y,
                 uint32_t fill, uint32_t outline)
{
    if (!surface_valid(surface)) {
        return;
    }

    /* Outline first, so the arrow paints over any shared pixels. */
    for (int64_t row = -1; row <= CURSOR_HEIGHT; row++) {
        for (int64_t col = -1; col <= CURSOR_WIDTH; col++) {
            if (arrow_pixel(col, row)) {
                continue;
            }
            const bool beside =
                arrow_pixel(col - 1, row) || arrow_pixel(col + 1, row) ||
                arrow_pixel(col, row - 1) || arrow_pixel(col, row + 1);
            if (beside) {
                surface_put_pixel(surface, x + col, y + row, outline);
            }
        }
    }

    for (int64_t row = 0; row < CURSOR_HEIGHT; row++) {
        for (int64_t col = 0; col < CURSOR_WIDTH; col++) {
            if (arrow_pixel(col, row)) {
                surface_put_pixel(surface, x + col, y + row, fill);
            }
        }
    }
}
