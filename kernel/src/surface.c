#include "surface.h"

#include "font.h"

#include <stddef.h>

struct clipped_axis {
    uint32_t source_start;
    uint32_t target_start;
    uint32_t count;
};

static bool clip_axis(int64_t target_origin, uint32_t source_extent,
                      uint32_t target_extent, struct clipped_axis *result)
{
    if (source_extent == 0 || target_extent == 0 || result == NULL) {
        return false;
    }

    if (target_origin >= 0) {
        if ((uint64_t)target_origin >= target_extent) {
            return false;
        }
        result->source_start = 0;
        result->target_start = (uint32_t)target_origin;
    } else {
        const uint64_t skipped = (uint64_t)(-(target_origin + 1)) + 1;
        if (skipped >= source_extent) {
            return false;
        }
        result->source_start = (uint32_t)skipped;
        result->target_start = 0;
    }

    const uint32_t source_remaining = source_extent - result->source_start;
    const uint32_t target_remaining = target_extent - result->target_start;
    result->count = source_remaining < target_remaining
        ? source_remaining : target_remaining;
    return result->count != 0;
}

/* Bounds the work of primitives whose algorithms otherwise walk from one
 * endpoint to another. Pathological coordinates are safely refused. */
static bool coordinate_in_work_range(const struct surface *surface,
                                     int64_t x, int64_t y)
{
    const int64_t margin = (int64_t)surface->width + (int64_t)surface->height;
    return x >= -margin && y >= -margin &&
           x <= (int64_t)surface->width + margin &&
           y <= (int64_t)surface->height + margin;
}

bool surface_init(struct surface *surface, uint32_t *pixels, size_t capacity,
                  uint32_t width, uint32_t height)
{
    if (surface == NULL) {
        return false;
    }
    *surface = (struct surface){0};
    if (pixels == NULL || width == 0 || height == 0 ||
        (size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > capacity) {
        return false;
    }
    surface->pixels = pixels;
    surface->width = width;
    surface->height = height;
    surface->stride = width;
    return true;
}

bool surface_valid(const struct surface *surface)
{
    return surface != NULL && surface->pixels != NULL &&
           surface->width > 0 && surface->height > 0 &&
           surface->stride >= surface->width;
}

void surface_clear(struct surface *surface, uint32_t colour)
{
    if (!surface_valid(surface)) {
        return;
    }
    for (uint32_t y = 0; y < surface->height; y++) {
        for (uint32_t x = 0; x < surface->width; x++) {
            surface->pixels[(size_t)y * surface->stride + x] = colour;
        }
    }
}

bool surface_put_pixel(struct surface *surface, int64_t x, int64_t y,
                       uint32_t colour)
{
    if (!surface_valid(surface) || x < 0 || y < 0 ||
        (uint64_t)x >= surface->width || (uint64_t)y >= surface->height) {
        return false;
    }
    surface->pixels[(size_t)y * surface->stride + (size_t)x] = colour;
    return true;
}

uint32_t surface_pixel(const struct surface *surface, int64_t x, int64_t y)
{
    if (!surface_valid(surface) || x < 0 || y < 0 ||
        (uint64_t)x >= surface->width || (uint64_t)y >= surface->height) {
        return 0;
    }
    return surface->pixels[(size_t)y * surface->stride + (size_t)x];
}

void surface_fill_rect(struct surface *surface, int64_t x, int64_t y,
                       uint32_t width, uint32_t height, uint32_t colour)
{
    if (!surface_valid(surface) || width == 0 || height == 0) {
        return;
    }
    struct clipped_axis horizontal;
    struct clipped_axis vertical;
    if (!clip_axis(x, width, surface->width, &horizontal) ||
        !clip_axis(y, height, surface->height, &vertical)) {
        return;
    }
    for (uint32_t row = 0; row < vertical.count; row++) {
        const uint32_t at_y = vertical.target_start + row;
        for (uint32_t column = 0; column < horizontal.count; column++) {
            const uint32_t at_x = horizontal.target_start + column;
            surface->pixels[(size_t)at_y * surface->stride + at_x] = colour;
        }
    }
}

void surface_draw_line(struct surface *surface,
                       int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                       uint32_t colour)
{
    if (!surface_valid(surface) ||
        !coordinate_in_work_range(surface, x0, y0) ||
        !coordinate_in_work_range(surface, x1, y1)) {
        return;
    }
    const int64_t step_x = x0 < x1 ? 1 : -1;
    const int64_t step_y = y0 < y1 ? 1 : -1;
    int64_t dx = x1 - x0;
    int64_t dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    dy = -dy;
    int64_t error = dx + dy;

    for (;;) {
        surface_put_pixel(surface, x0, y0, colour);
        if (x0 == x1 && y0 == y1) {
            return;
        }
        const int64_t doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x0 += step_x;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += step_y;
        }
    }
}

static void draw_char(struct surface *surface, char character,
                      int64_t x, int64_t y, uint32_t colour, uint32_t scale)
{
    const uint8_t *glyph = font_glyph(character);
    for (uint32_t row = 0; row < FONT_HEIGHT; row++) {
        const uint8_t bits = glyph[row];
        for (uint32_t column = 0; column < FONT_WIDTH; column++) {
            if ((bits & (0x80u >> column)) == 0) {
                continue;
            }
            for (uint32_t sy = 0; sy < scale; sy++) {
                for (uint32_t sx = 0; sx < scale; sx++) {
                    surface_put_pixel(surface,
                        x + (int64_t)column * scale + sx,
                        y + (int64_t)row * scale + sy, colour);
                }
            }
        }
    }
}

void surface_draw_string(struct surface *surface, const char *text,
                         int64_t x, int64_t y, uint32_t colour, uint32_t scale)
{
    if (!surface_valid(surface) || text == NULL) {
        return;
    }
    if (scale == 0) {
        scale = 1;
    }
    if (scale > surface->width && scale > surface->height) {
        return;
    }
    const int64_t advance = (int64_t)FONT_WIDTH * scale;
    int64_t cursor_x = x;
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (!coordinate_in_work_range(surface, cursor_x, y)) {
            return;
        }
        draw_char(surface, text[i], cursor_x, y, colour, scale);
        if (cursor_x > INT64_MAX - advance) {
            return;
        }
        cursor_x += advance;
    }
}

void surface_blit(struct surface *destination, const struct surface *source,
                  int64_t destination_x, int64_t destination_y)
{
    if (!surface_valid(destination)) {
        return;
    }
    surface_blit_clipped(destination, source, destination_x, destination_y,
                         region_make(0, 0, (int64_t)destination->width,
                                     (int64_t)destination->height));
}

void surface_blit_clipped(struct surface *destination, const struct surface *source,
                          int64_t destination_x, int64_t destination_y,
                          struct region clip)
{
    if (!surface_valid(destination) || !surface_valid(source)) {
        return;
    }
    /* Trimmed to the destination first, so a caller passing a clip larger than
     * the surface cannot walk off the end of it. */
    clip = region_clip(clip, (int64_t)destination->width, (int64_t)destination->height);
    if (region_empty(&clip)) {
        return;
    }

    struct clipped_axis horizontal;
    struct clipped_axis vertical;
    if (!clip_axis(destination_x, source->width, destination->width, &horizontal) ||
        !clip_axis(destination_y, source->height, destination->height, &vertical)) {
        return;
    }

    /* What the blit would have covered, narrowed to what the caller asked for.
     * Both starts move together, because dropping a destination column means
     * dropping the source column that would have landed on it. */
    const struct region covered =
        region_make((int64_t)horizontal.target_start, (int64_t)vertical.target_start,
                    (int64_t)horizontal.count, (int64_t)vertical.count);
    const struct region wanted = region_intersect(covered, clip);
    if (region_empty(&wanted)) {
        return;
    }

    const uint32_t skip_x = (uint32_t)(wanted.x - covered.x);
    const uint32_t skip_y = (uint32_t)(wanted.y - covered.y);

    for (int64_t row = 0; row < wanted.height; row++) {
        const uint32_t source_y = vertical.source_start + skip_y + (uint32_t)row;
        const uint32_t target_y = vertical.target_start + skip_y + (uint32_t)row;
        for (int64_t column = 0; column < wanted.width; column++) {
            const uint32_t source_x = horizontal.source_start + skip_x + (uint32_t)column;
            const uint32_t target_x = horizontal.target_start + skip_x + (uint32_t)column;
            destination->pixels[(size_t)target_y * destination->stride + target_x] =
                source->pixels[(size_t)source_y * source->stride + source_x];
        }
    }
}
