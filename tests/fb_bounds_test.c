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

/* Channel masks are a parameter, not a constant. fb_rgb exists so the kernel
 * does not assume 0xRRGGBB, and hard coding 0xRRGGBB here was proving the one
 * case the function did not need writing for. QEMU always hands over
 * 0xRRGGBB, so a regression would have passed every test and every boot
 * capture on this machine and only shown up on real hardware. */
static struct limine_framebuffer make_fb_masks(uint8_t rs, uint8_t rsh,
                                               uint8_t gs, uint8_t gsh,
                                               uint8_t bs, uint8_t bsh)
{
    struct limine_framebuffer fb;
    memset(&fb, 0, sizeof fb);
    fb.address = visible;
    fb.width = WIDTH;
    fb.height = HEIGHT;
    fb.pitch = PITCH;
    fb.bpp = 32;
    fb.red_mask_size = rs;   fb.red_mask_shift = rsh;
    fb.green_mask_size = gs; fb.green_mask_shift = gsh;
    fb.blue_mask_size = bs;  fb.blue_mask_shift = bsh;
    return fb;
}

static struct limine_framebuffer make_fb(void)
{
    return make_fb_masks(8, 16, 8, 8, 8, 0);
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

/* Re inits fb against one layout so the packing checks below can move the
 * masks. reset() puts the ordinary layout back afterwards. */
static void init_with(uint8_t rs, uint8_t rsh, uint8_t gs, uint8_t gsh,
                      uint8_t bs, uint8_t bsh)
{
    struct limine_framebuffer fb = make_fb_masks(rs, rsh, gs, gsh, bs, bsh);
    if (!fb_init(&fb)) {
        printf("  FAIL  fb_init rejected a framebuffer it should accept\n");
        exit(1);
    }
}

static void test_colour_packing(void)
{
    printf("fb_rgb packs to the framebuffer's own channel masks\n");
    reset();
    check(fb_rgb(255, 0, 0) == 0x00FF0000u, "red goes to the red mask");
    check(fb_rgb(0, 255, 0) == 0x0000FF00u, "green goes to the green mask");
    check(fb_rgb(0, 0, 255) == 0x000000FFu, "blue goes to the blue mask");
    check(fb_rgb(0, 0, 0) == 0u, "black is zero");

    printf("and to a layout that is not 0xRRGGBB\n");
    /* BGR, the channels swapped end for end. */
    init_with(8, 0, 8, 8, 8, 16);
    check(fb_rgb(255, 0, 0) == 0x000000FFu, "red follows its mask down to bit 0");
    check(fb_rgb(0, 0, 255) == 0x00FF0000u, "blue follows its mask up to bit 16");
    check(fb_rgb(0, 255, 0) == 0x0000FF00u, "green stays where it was");
    check(fb_rgb(255, 255, 255) == 0x00FFFFFFu, "white still fills all three");

    /* Channels in the top word, which is a layout no byte order describes. */
    init_with(8, 24, 8, 16, 8, 8);
    check(fb_rgb(255, 0, 0) == 0xFF000000u, "a shift past 23 is not truncated");
    check(fb_rgb(0, 0, 255) == 0x0000FF00u, "and the others move with it");

    printf("and to channels narrower than eight bits\n");
    /* 5-6-5, the common 16 bit layout. This is the only thing that exercises
     * the narrowing shift in pack, which every 8 bit mask leaves as a no op. */
    init_with(5, 11, 6, 5, 5, 0);
    check(fb_rgb(255, 255, 255) == 0xFFFFu, "white fills all sixteen bits");
    check(fb_rgb(255, 0, 0) == 0xF800u, "red keeps its top five bits");
    check(fb_rgb(0, 255, 0) == 0x07E0u, "green keeps its top six");
    check(fb_rgb(0, 0, 255) == 0x001Fu, "blue keeps its top five");
    check(fb_rgb(7, 3, 7) == 0u, "and the low bits that do not fit are dropped");
    check(fb_rgb(0, 0, 0) == 0u, "black is still zero");

    reset();
}

/* fb_pixel had no test at all, so the guard that makes an out of bounds read
 * safe was held up by nothing. A negative coordinate arrives here as a very
 * large unsigned value, which is the case that has to come back as 0 rather
 * than as a read off the end of the mapping. The guard bytes prove the read
 * did not stray, since a stray read of the sentinel would not return 0. */
static void test_pixel_read_guard(void)
{
    printf("fb_pixel reads inside the screen and refuses outside it\n");
    reset();
    const uint32_t colour = 0x00ABCDEFu;
    fb_put_pixel(0, 0, colour);
    fb_put_pixel(WIDTH - 1, HEIGHT - 1, colour);
    check(fb_pixel(0, 0) == colour, "the first pixel reads back");
    check(fb_pixel(WIDTH - 1, HEIGHT - 1) == colour, "and so does the last");

    check(fb_pixel(WIDTH, 0) == 0, "one column past the right edge is 0");
    check(fb_pixel(0, HEIGHT) == 0, "one row past the bottom edge is 0");
    check(fb_pixel(WIDTH, HEIGHT) == 0, "past both edges is 0");

    /* What a signed -1 becomes on the way in, which is how the cursor's
     * save loop reaches for the row above the top of the screen. */
    check(fb_pixel((uint64_t)-1, 0) == 0, "a wrapped negative column is 0");
    check(fb_pixel(0, (uint64_t)-1) == 0, "a wrapped negative row is 0");
    check(fb_pixel(UINT64_MAX, UINT64_MAX) == 0, "and the largest pair is 0");

    check(guards_intact() && padding_intact(), "no read strayed out of bounds");
}

static void test_surface_present(void)
{
    printf("fb_present copies a composited surface without touching padding\n");
    uint32_t large_pixels[(WIDTH + 10) * (HEIGHT + 10)];
    struct surface large;
    surface_init(&large, large_pixels,
                 (WIDTH + 10) * (HEIGHT + 10), WIDTH + 10, HEIGHT + 10);
    const unsigned int colour = 0x00123456u;
    surface_clear(&large, colour);
    reset();
    fb_present(&large);
    check(count_pixels(colour) == WIDTH * HEIGHT, "a larger source fills the visible screen");
    check(guards_intact(), "presentation keeps framebuffer guards intact");
    check(padding_intact(), "presentation does not write row padding");

    uint32_t small_pixels[4 * 3];
    struct surface small;
    surface_init(&small, small_pixels, 12, 4, 3);
    surface_clear(&small, colour);
    reset();
    fb_present(&small);
    check(count_pixels(colour) == 12, "a smaller source copies only its own pixels");
    check(pixel_at(3, 2) == colour, "the smaller source reaches its bottom-right pixel");
    check(guards_intact() && padding_intact(), "small presentation stays bounded");

    reset();
    fb_present(NULL);
    check(guards_intact() && padding_intact(), "presenting no surface is safe");
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
    test_pixel_read_guard();
    test_surface_present();

    free(block);
    if (failures > 0) {
        printf("\n%d framebuffer check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nframebuffer bounds checks passed\n");
    return 0;
}
