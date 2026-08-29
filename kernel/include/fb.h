/* Checked low-level linear framebuffer access and surface presentation. */
#ifndef ME_FB_H
#define ME_FB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "limine.h"
#include "surface.h"

/* Returns false if the framebuffer uses a layout this code cannot drive,
 * in which case nothing has been written and no pointer is retained. */
bool fb_init(struct limine_framebuffer *fb);

uint64_t fb_width(void);
uint64_t fb_height(void);

/* Packs a colour for the framebuffer's actual channel masks rather than
 * assuming 0xRRGGBB byte order. */
uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);

void fb_clear(uint32_t colour);

/* Single pixel access, both clipped. fb_pixel returns 0 outside the screen. */
uint32_t fb_pixel(uint64_t x, uint64_t y);
void fb_put_pixel(uint64_t x, uint64_t y, uint32_t colour);

/* Fills an axis aligned rectangle, clipped to the framebuffer. */
void fb_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t colour);

/* Draws a one pixel line between two points, clipped to the framebuffer.
 * Coordinates are signed because a rotating shape can put a corner off screen,
 * and the line still has to draw the part that is on it. */
void fb_draw_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1, uint32_t colour);

/* Draws `s` with each font pixel expanded to a `scale` by `scale` block.
 * Anything falling outside the framebuffer is clipped. */
void fb_draw_string(const char *s, uint64_t x, uint64_t y,
                    uint32_t colour, uint64_t scale);

/* Presents a software-composited surface at the framebuffer origin. Both the
 * source dimensions and framebuffer writes are clipped. */
void fb_present(const struct surface *surface);

#endif /* ME_FB_H */
