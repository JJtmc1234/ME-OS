/* A software pixel surface with local, clipped drawing coordinates. */
#ifndef ME_SURFACE_H
#define ME_SURFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct surface {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    size_t stride;
};

/* The caller owns the backing memory. `capacity` is measured in pixels. */
bool surface_init(struct surface *surface, uint32_t *pixels, size_t capacity,
                  uint32_t width, uint32_t height);
bool surface_valid(const struct surface *surface);

void surface_clear(struct surface *surface, uint32_t colour);
bool surface_put_pixel(struct surface *surface, int64_t x, int64_t y,
                       uint32_t colour);
uint32_t surface_pixel(const struct surface *surface, int64_t x, int64_t y);
void surface_fill_rect(struct surface *surface, int64_t x, int64_t y,
                       uint32_t width, uint32_t height, uint32_t colour);
void surface_draw_line(struct surface *surface,
                       int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                       uint32_t colour);
void surface_draw_string(struct surface *surface, const char *text,
                         int64_t x, int64_t y, uint32_t colour, uint32_t scale);

/* Copies source pixels opaquely at a signed destination position. Both sides
 * are clipped; no coordinate outside either surface is ever dereferenced. */
void surface_blit(struct surface *destination, const struct surface *source,
                  int64_t destination_x, int64_t destination_y);

#endif /* ME_SURFACE_H */
