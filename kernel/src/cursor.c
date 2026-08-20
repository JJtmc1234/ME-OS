#include "cursor.h"

#include "fb.h"

/* An arrow, one bit per pixel, bit 7 leftmost. The outline is worked out from
 * the shape at draw time rather than stored, so the two can never disagree. */
static const uint8_t arrow[CURSOR_HEIGHT] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC,
    0xFE, 0xFF, 0xF8, 0xD8, 0x8C, 0x0C,
};

/* The saved patch is one pixel larger on every side, because the outline is
 * drawn just outside the arrow. */
#define PATCH_WIDTH  (CURSOR_WIDTH + 2)
#define PATCH_HEIGHT (CURSOR_HEIGHT + 2)

static uint32_t saved[PATCH_WIDTH * PATCH_HEIGHT];
static uint64_t saved_x, saved_y;
static bool drawn;

static bool arrow_pixel(int64_t col, int64_t row)
{
    if (row < 0 || row >= CURSOR_HEIGHT || col < 0 || col >= CURSOR_WIDTH) {
        return false;
    }
    return (arrow[row] & (0x80u >> col)) != 0;
}

bool cursor_visible(void)
{
    return drawn;
}

void cursor_show(uint64_t x, uint64_t y, uint32_t fill, uint32_t outline)
{
    if (drawn) {
        return;
    }

    saved_x = x;
    saved_y = y;

    /* Save first, including the one pixel border the outline will use. */
    for (int64_t row = 0; row < PATCH_HEIGHT; row++) {
        for (int64_t col = 0; col < PATCH_WIDTH; col++) {
            saved[row * PATCH_WIDTH + col] =
                fb_pixel((uint64_t)((int64_t)x + col - 1), (uint64_t)((int64_t)y + row - 1));
        }
    }

    /* Outline: any pixel next to the arrow that is not part of it. Drawn
     * first, so the arrow paints over anything the two disagree about. */
    for (int64_t row = -1; row <= CURSOR_HEIGHT; row++) {
        for (int64_t col = -1; col <= CURSOR_WIDTH; col++) {
            if (arrow_pixel(col, row)) {
                continue;
            }
            const bool beside =
                arrow_pixel(col - 1, row) || arrow_pixel(col + 1, row) ||
                arrow_pixel(col, row - 1) || arrow_pixel(col, row + 1);
            if (beside) {
                fb_put_pixel((uint64_t)((int64_t)x + col), (uint64_t)((int64_t)y + row),
                             outline);
            }
        }
    }

    for (int64_t row = 0; row < CURSOR_HEIGHT; row++) {
        for (int64_t col = 0; col < CURSOR_WIDTH; col++) {
            if (arrow_pixel(col, row)) {
                fb_put_pixel((uint64_t)((int64_t)x + col), (uint64_t)((int64_t)y + row), fill);
            }
        }
    }

    drawn = true;
}

void cursor_hide(void)
{
    if (!drawn) {
        return;
    }
    for (int64_t row = 0; row < PATCH_HEIGHT; row++) {
        for (int64_t col = 0; col < PATCH_WIDTH; col++) {
            fb_put_pixel((uint64_t)((int64_t)saved_x + col - 1),
                         (uint64_t)((int64_t)saved_y + row - 1),
                         saved[row * PATCH_WIDTH + col]);
        }
    }
    drawn = false;
}
