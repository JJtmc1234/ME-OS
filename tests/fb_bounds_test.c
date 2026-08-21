/* Host unit test for the framebuffer's bounds handling.
 *
 * Compiled and run on the development machine, not in the kernel. The
 * framebuffer is a plain malloc'd buffer with guard regions on both sides,
 * filled with a sentinel byte. Any write outside the visible area, or into a
 * row's padding, changes a sentinel and fails the test.
 *
 * This is the check that a clipping mistake in fb.c cannot reach hardware.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fb.h"

#define WIDTH   64
#define HEIGHT  32
#define PADDING 16           /* bytes of unused space at the end of each row */
#define PITCH   (WIDTH * 4 + PADDING)
#define GUARD   256
#define SENTINEL 0xAA

static unsigned char *block;
static unsigned char *visible;
static int failures;

static void check(int condition, const char *what)
{
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

static int guards_intact(void)
{
    for (int i = 0; i < GUARD; i++) {
        if (block[i] != SENTINEL) {
            return 0;
        }
        if (block[GUARD + PITCH * HEIGHT + i] != SENTINEL) {
            return 0;
        }
    }
    return 1;
}

static int padding_intact(void)
{
    for (int row = 0; row < HEIGHT; row++) {
        const unsigned char *tail = visible + row * PITCH + WIDTH * 4;
        for (int i = 0; i < PADDING; i++) {
            if (tail[i] != SENTINEL) {
                return 0;
            }
        }
    }
    return 1;
}

static unsigned int pixel_at(int x, int y)
{
    unsigned int value;
    memcpy(&value, visible + (size_t)y * PITCH + (size_t)x * 4, 4);
    return value;
}

static int count_pixels(unsigned int colour)
{
    int total = 0;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            if (pixel_at(x, y) == colour) {
                total++;
            }
        }
    }
    return total;
}

static struct limine_framebuffer make_fb(void)
{
    struct limine_framebuffer fb;
    memset(&fb, 0, sizeof fb);
    fb.address = visible;
    fb.width = WIDTH;
    fb.height = HEIGHT;
    fb.pitch = PITCH;
    fb.bpp = 32;
    fb.red_mask_size = 8;   fb.red_mask_shift = 16;
    fb.green_mask_size = 8; fb.green_mask_shift = 8;
    fb.blue_mask_size = 8;  fb.blue_mask_shift = 0;
    return fb;
}

static void reset(void)
{
    memset(block, SENTINEL, GUARD * 2 + PITCH * HEIGHT);
    struct limine_framebuffer fb = make_fb();
    if (!fb_init(&fb)) {
        printf("  FAIL  fb_init rejected a valid framebuffer\n");
        exit(1);
    }
}

static void test_rejects_bad_framebuffers(void)
{
    struct limine_framebuffer fb;

    printf("fb_init refuses layouts it cannot drive\n");
    check(!fb_init(NULL), "null framebuffer");

    fb = make_fb(); fb.address = NULL;
    check(!fb_init(&fb), "null address");

    fb = make_fb(); fb.bpp = 24;
    check(!fb_init(&fb), "24 bits per pixel");

    fb = make_fb(); fb.width = 0;
    check(!fb_init(&fb), "zero width");

    fb = make_fb(); fb.pitch = WIDTH * 2;
    check(!fb_init(&fb), "pitch shorter than a row");

    fb = make_fb(); fb.pitch = WIDTH * 4 + 2;
    check(!fb_init(&fb), "pitch not a multiple of four");
}

static void test_clear_stays_inside(void)
{
    printf("fb_clear fills exactly the visible area\n");
    reset();
    unsigned int colour = fb_rgb(1, 2, 3);
    fb_clear(colour);
    check(guards_intact(), "guard regions untouched");
    check(padding_intact(), "row padding untouched");
    check(count_pixels(colour) == WIDTH * HEIGHT, "every visible pixel written");
}

static void test_rect_clipping(void)
{
    unsigned int colour = fb_rgb(255, 0, 0);

    printf("fb_fill_rect clips instead of writing out of bounds\n");

    reset();
    fb_fill_rect(WIDTH + 10, 4, 8, 8, colour);
    check(guards_intact() && count_pixels(colour) == 0, "fully off the right edge");

    reset();
    fb_fill_rect(4, HEIGHT + 10, 8, 8, colour);
    check(guards_intact() && count_pixels(colour) == 0, "fully below the bottom edge");

    reset();
    fb_fill_rect(WIDTH - 4, HEIGHT - 4, 16, 16, colour);
    check(guards_intact(), "straddling the bottom right corner: guards intact");
    check(padding_intact(), "straddling the corner: padding intact");
    check(count_pixels(colour) == 16, "straddling the corner: only the visible 4x4 written");

    reset();
    fb_fill_rect(0, 0, WIDTH * 4, HEIGHT * 4, colour);
    check(guards_intact() && padding_intact(), "rectangle far larger than the screen");
    check(count_pixels(colour) == WIDTH * HEIGHT, "oversized rectangle fills the screen exactly");

    reset();
    fb_fill_rect(10, 10, 0, 0, colour);
    check(count_pixels(colour) == 0, "zero sized rectangle draws nothing");
}

static void test_text_clipping(void)
{
    printf("fb_draw_string clips at the edges\n");
    unsigned int colour = fb_rgb(0, 255, 0);

    reset();
    fb_draw_string("ME OS", WIDTH - 8, HEIGHT - 4, colour, 4);
    check(guards_intact() && padding_intact(), "text running off the corner");

    reset();
    fb_draw_string("ME OS", WIDTH + 40, 0, colour, 1);
    check(count_pixels(colour) == 0, "text entirely off the screen");

    reset();
    fb_draw_string("I", 0, 0, colour, 0);
    check(guards_intact() && count_pixels(colour) > 0, "scale zero is treated as one");
}

static void test_line_clipping(void)
{
    unsigned int colour = fb_rgb(0, 128, 255);

    printf("fb_draw_line clips instead of writing out of bounds\n");

    reset();
    fb_draw_line(0, 0, WIDTH - 1, HEIGHT - 1, colour);
    check(guards_intact() && padding_intact(), "a diagonal across the whole screen");
    check(pixel_at(0, 0) == colour && pixel_at(WIDTH - 1, HEIGHT - 1) == colour,
          "and it reaches both corners");

    reset();
    fb_draw_line(-500, -500, WIDTH + 500, HEIGHT + 500, colour);
    check(guards_intact() && padding_intact(), "a line starting and ending far off screen");
    check(count_pixels(colour) > 0, "and the part that crosses the screen is drawn");

    reset();
    fb_draw_line(-100, -100, -50, -50, colour);
    check(guards_intact() && count_pixels(colour) == 0, "a line entirely off the top left");

    reset();
    fb_draw_line(WIDTH + 10, 0, WIDTH + 40, HEIGHT, colour);
    check(guards_intact() && count_pixels(colour) == 0, "a line entirely off the right");

    reset();
    fb_draw_line(5, 5, 5, 5, colour);
    check(count_pixels(colour) == 1, "a line with no length is one pixel");

    reset();
    fb_draw_line(0, HEIGHT / 2, WIDTH - 1, HEIGHT / 2, colour);
    check(count_pixels(colour) == WIDTH, "a horizontal line is exactly as wide as the screen");
    check(padding_intact(), "and does not run into the row padding");

    reset();
    fb_draw_line(WIDTH / 2, 0, WIDTH / 2, HEIGHT - 1, colour);
    check(count_pixels(colour) == HEIGHT, "a vertical line is exactly as tall");

    printf("a triangle drawn near the corner stays inside\n");
    reset();
    fb_draw_line(WIDTH - 3, HEIGHT - 3, WIDTH + 20, HEIGHT - 3, colour);
    fb_draw_line(WIDTH + 20, HEIGHT - 3, WIDTH - 3, HEIGHT + 20, colour);
    fb_draw_line(WIDTH - 3, HEIGHT + 20, WIDTH - 3, HEIGHT - 3, colour);
    check(guards_intact() && padding_intact(), "guards and padding untouched");
}

static void test_colour_packing(void)
{
    printf("fb_rgb packs to the framebuffer's own channel masks\n");
    reset();
    check(fb_rgb(255, 0, 0) == 0x00FF0000u, "red goes to the red mask");
    check(fb_rgb(0, 255, 0) == 0x0000FF00u, "green goes to the green mask");
    check(fb_rgb(0, 0, 255) == 0x000000FFu, "blue goes to the blue mask");
    check(fb_rgb(0, 0, 0) == 0u, "black is zero");
}

int main(void)
{
    block = malloc(GUARD * 2 + PITCH * HEIGHT);
    if (block == NULL) {
        return 1;
    }
    visible = block + GUARD;

    test_rejects_bad_framebuffers();
    test_clear_stays_inside();
    test_rect_clipping();
    test_text_clipping();
    test_line_clipping();
    test_colour_packing();

    free(block);
    if (failures > 0) {
        printf("\n%d framebuffer check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nframebuffer bounds checks passed\n");
    return 0;
}
