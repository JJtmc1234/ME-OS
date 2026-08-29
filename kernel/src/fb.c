#include "fb.h"
#include "font.h"

/* volatile: the compiler must not coalesce or drop stores to video memory,
 * which it cannot prove anyone ever reads back. */
static volatile uint8_t *fb_base;
static uint64_t fb_w, fb_h, fb_pitch;
static uint8_t r_size, r_shift, g_size, g_shift, b_size, b_shift;

bool fb_init(struct limine_framebuffer *fb)
{
    /* 32 bits per pixel is what Limine hands us on UEFI. Refusing anything
     * else is better than writing garbage through a mis-sized pointer. */
    if (fb == NULL || fb->address == NULL || fb->bpp != 32) {
        return false;
    }
    if (fb->width == 0 || fb->height == 0 || fb->pitch < fb->width * 4) {
        return false;
    }
    /* Rows are addressed as uint32_t, so a pitch that is not a multiple of
     * four would misalign every row after the first. */
    if (fb->pitch % 4 != 0) {
        return false;
    }

    fb_base  = (volatile uint8_t *)fb->address;
    fb_w     = fb->width;
    fb_h     = fb->height;
    fb_pitch = fb->pitch;

    r_size = fb->red_mask_size;   r_shift = fb->red_mask_shift;
    g_size = fb->green_mask_size; g_shift = fb->green_mask_shift;
    b_size = fb->blue_mask_size;  b_shift = fb->blue_mask_shift;

    return true;
}

uint64_t fb_width(void)  { return fb_w; }
uint64_t fb_height(void) { return fb_h; }

/* Narrows an 8 bit component down to however many bits this channel has. */
static uint32_t pack(uint8_t value, uint8_t size, uint8_t shift)
{
    if (size == 0 || size > 8) {
        return 0;
    }
    return (uint32_t)(value >> (8 - size)) << shift;
}

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return pack(r, r_size, r_shift)
         | pack(g, g_size, g_shift)
         | pack(b, b_size, b_shift);
}

void fb_put_pixel(uint64_t x, uint64_t y, uint32_t colour)
{
    if (x >= fb_w || y >= fb_h) {
        return;
    }
    *(volatile uint32_t *)(fb_base + y * fb_pitch + x * 4) = colour;
}

uint32_t fb_pixel(uint64_t x, uint64_t y)
{
    if (x >= fb_w || y >= fb_h) {
        return 0;
    }
    return *(volatile uint32_t *)(fb_base + y * fb_pitch + x * 4);
}

static void put_pixel(uint64_t x, uint64_t y, uint32_t colour)
{
    fb_put_pixel(x, y, colour);
}

void fb_clear(uint32_t colour)
{
    for (uint64_t y = 0; y < fb_h; y++) {
        volatile uint32_t *row = (volatile uint32_t *)(fb_base + y * fb_pitch);
        for (uint64_t x = 0; x < fb_w; x++) {
            row[x] = colour;
        }
    }
}

void fb_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t colour)
{
    for (uint64_t row = 0; row < h; row++) {
        for (uint64_t col = 0; col < w; col++) {
            put_pixel(x + col, y + row, colour);
        }
    }
}

/* Bresenham. Integer only: it decides each step by comparing errors rather
 * than by dividing, which is why it belongs here rather than in the module
 * that does the rotation. Every pixel goes through the clipped write, so a
 * line running off the screen simply draws less of itself. */
void fb_draw_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1, uint32_t colour)
{
    const int64_t step_x = x0 < x1 ? 1 : -1;
    const int64_t step_y = y0 < y1 ? 1 : -1;
    int64_t dx = x1 - x0;
    int64_t dy = y1 - y0;

    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    dy = -dy;

    int64_t error = dx + dy;

    for (;;) {
        if (x0 >= 0 && y0 >= 0) {
            fb_put_pixel((uint64_t)x0, (uint64_t)y0, colour);
        }
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

static void draw_char(char c, uint64_t x, uint64_t y,
                      uint32_t colour, uint64_t scale)
{
    const uint8_t *glyph = font_glyph(c);

    for (uint64_t row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (uint64_t col = 0; col < FONT_WIDTH; col++) {
            if ((bits & (0x80u >> col)) == 0) {
                continue;
            }
            for (uint64_t sy = 0; sy < scale; sy++) {
                for (uint64_t sx = 0; sx < scale; sx++) {
                    put_pixel(x + col * scale + sx, y + row * scale + sy, colour);
                }
            }
        }
    }
}

void fb_draw_string(const char *s, uint64_t x, uint64_t y,
                    uint32_t colour, uint64_t scale)
{
    if (scale == 0) {
        scale = 1;
    }
    for (uint64_t i = 0; s[i] != '\0'; i++) {
        draw_char(s[i], x + i * FONT_WIDTH * scale, y, colour, scale);
    }
}

void fb_present(const struct surface *surface)
{
    if (!surface_valid(surface)) {
        return;
    }
    const uint64_t height = surface->height < fb_h ? surface->height : fb_h;
    const uint64_t width = surface->width < fb_w ? surface->width : fb_w;
    for (uint64_t y = 0; y < height; y++) {
        volatile uint32_t *destination =
            (volatile uint32_t *)(fb_base + y * fb_pitch);
        const uint32_t *source = surface->pixels + (size_t)y * surface->stride;
        for (uint64_t x = 0; x < width; x++) {
            destination[x] = source[x];
        }
    }
}
