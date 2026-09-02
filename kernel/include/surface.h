/* A software pixel surface with local, clipped drawing coordinates. */
#ifndef ME_SURFACE_H
#define ME_SURFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "region.h"

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

/* A surface over part of another one, sharing its pixels.
 *
 * The stride is what makes this work: rows of the view are rows of the parent,
 * just narrower and starting further along. Drawing into the view draws into the
 * parent, which is how a window frame and the app content inside it live in one
 * buffer and reach the compositor in one blit.
 *
 * Refuses a rectangle that is not wholly inside the parent, so a view can never
 * be the thing that writes outside a window. The view does not own the memory
 * and must not outlive the parent. */
bool surface_view(const struct surface *parent, int64_t x, int64_t y,
                  uint32_t width, uint32_t height, struct surface *out);

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

/* The same, restricted to `clip` in destination coordinates.
 *
 * `surface_blit` is this with a clip covering the whole destination, so there is
 * one copying loop rather than two that could come to disagree about an edge.
 * An empty clip copies nothing, which is what lets a compositor skip a window
 * that does not touch the part of the screen that changed. See M16. */
void surface_blit_clipped(struct surface *destination, const struct surface *source,
                          int64_t destination_x, int64_t destination_y,
                          struct region clip);

#endif /* ME_SURFACE_H */
