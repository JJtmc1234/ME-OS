/* Host tests for M14 software surfaces and composition. */
#include <stdint.h>
#include <stdio.h>

#include "compositor.h"
#include "region.h"
#include "cursor.h"
#include "surface.h"
#include "window.h"

#define GUARD 8
#define SENTINEL 0xA55AA55Au

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

static bool guards_intact(const uint32_t *storage, size_t pixels)
{
    for (size_t i = 0; i < GUARD; i++) {
        if (storage[i] != SENTINEL || storage[GUARD + pixels + i] != SENTINEL) {
            return false;
        }
    }
    return true;
}

static void prepare(uint32_t *storage, size_t pixels)
{
    for (size_t i = 0; i < pixels + GUARD * 2; i++) {
        storage[i] = SENTINEL;
    }
}

static void test_surface_creation_and_local_drawing(void)
{
    printf("surface creation validates its external backing store\n");
    uint32_t storage[GUARD + 48 + GUARD];
    prepare(storage, 48);
    struct surface surface;
    check(surface_init(&surface, storage + GUARD, 48, 8, 6), "create an 8 by 6 surface");
    check(surface_valid(&surface), "the created surface is valid");
    check(!surface_init(NULL, storage, 48, 8, 6), "no surface object is refused");
    check(!surface_init(&surface, NULL, 48, 8, 6), "no pixels are refused");
    check(!surface_init(&surface, storage, 47, 8, 6), "too little backing memory is refused");
    check(!surface_init(&surface, storage, 48, 0, 6), "zero width is refused");
    check(!surface_init(&surface, storage, 48, 8, 0), "zero height is refused");
    check(!surface_valid(&surface), "a failed init leaves an invalid surface");
    check(surface_init(&surface, storage + GUARD, 48, 8, 6), "reinitialize for drawing");

    printf("drawing uses window-local coordinates and clips every edge\n");
    surface_clear(&surface, 1);
    check(surface_put_pixel(&surface, 2, 3, 9), "write one local pixel");
    check(surface_pixel(&surface, 2, 3) == 9, "read that local pixel");
    check(!surface_put_pixel(&surface, -1, 0, 9), "a negative x is clipped");
    check(!surface_put_pixel(&surface, 8, 0, 9), "the right edge is clipped");
    check(!surface_put_pixel(&surface, 0, 6, 9), "the bottom edge is clipped");
    check(surface_pixel(&surface, -1, 0) == 0, "an outside read returns zero");

    surface_clear(&surface, 1);
    surface_fill_rect(&surface, -2, -1, 5, 4, 7);
    size_t filled = 0;
    for (int64_t y = 0; y < 6; y++) {
        for (int64_t x = 0; x < 8; x++) {
            if (surface_pixel(&surface, x, y) == 7) filled++;
        }
    }
    check(filled == 9, "a rectangle off the top-left draws only its visible 3 by 3");
    surface_fill_rect(&surface, 7, 5, 100, 100, 8);
    check(surface_pixel(&surface, 7, 5) == 8, "an oversized corner fill writes one pixel");
    surface_fill_rect(&surface, INT64_MIN, INT64_MIN, UINT32_MAX, UINT32_MAX, 6);
    surface_fill_rect(&surface, INT64_MAX, INT64_MAX, UINT32_MAX, UINT32_MAX, 6);
    check(surface_pixel(&surface, 7, 5) == 8,
          "extreme off-screen fills are safe no-ops");
    check(guards_intact(storage, 48), "local drawing never crossed the surface guards");

    surface_clear(&surface, 0);
    surface_draw_line(&surface, -5, -5, 12, 12, 3);
    check(surface_pixel(&surface, 0, 0) == 3 && surface_pixel(&surface, 5, 5) == 3,
          "a clipped line draws the portion crossing the surface");
    surface_draw_line(&surface, INT64_MIN, INT64_MIN, INT64_MAX, INT64_MAX, 6);
    surface_draw_string(&surface, "A", INT64_MAX, INT64_MAX, 6, UINT32_MAX);
    surface_draw_string(&surface, "A", 6, 4, 4, 1);
    check(guards_intact(storage, 48),
          "clipped and extreme text and lines keep the guards intact");
}

static void test_blit_clipping(void)
{
    printf("opaque blits clip both partially off-screen directions\n");
    uint32_t source_pixels[4 * 3];
    uint32_t target_storage[GUARD + 6 * 5 + GUARD];
    prepare(target_storage, 30);
    struct surface source, target;
    surface_init(&source, source_pixels, 12, 4, 3);
    surface_init(&target, target_storage + GUARD, 30, 6, 5);
    surface_clear(&source, 5);
    surface_clear(&target, 1);

    surface_blit(&target, &source, -2, 1);
    check(surface_pixel(&target, 0, 1) == 5 && surface_pixel(&target, 1, 3) == 5,
          "the visible right part of a left-clipped source is copied");
    check(surface_pixel(&target, 2, 1) == 1, "pixels outside that visible part are untouched");
    surface_blit(&target, &source, 4, 4);
    check(surface_pixel(&target, 4, 4) == 5 && surface_pixel(&target, 5, 4) == 5,
          "the visible top part of a bottom-right source is copied");
    surface_blit(&target, &source, INT64_MIN, INT64_MIN);
    surface_blit(&target, &source, INT64_MAX, INT64_MAX);
    check(surface_pixel(&target, 4, 4) == 5,
          "extreme off-screen blits are safe no-ops");
    check(guards_intact(target_storage, 30), "blitting never crossed target guards");
}

static void test_cursor_overlay(void)
{
    printf("the compositor-owned cursor draws into a clipped surface\n");
    uint32_t storage[GUARD + 16 * 16 + GUARD];
    prepare(storage, 256);
    struct surface surface;
    surface_init(&surface, storage + GUARD, 256, 16, 16);
    surface_clear(&surface, 1);

    cursor_draw(&surface, 3, 2, 9, 2);
    check(surface_pixel(&surface, 3, 2) == 9, "the arrow starts at its local position");
    cursor_draw(&surface, -4, -5, 9, 2);
    cursor_draw(&surface, 14, 14, 9, 2);
    check(guards_intact(storage, 256), "cursor clipping preserves surface guards");
}

static struct window_spec make_spec(const char *title, int32_t x, int32_t y,
                                    uint32_t width, uint32_t height)
{
    return (struct window_spec){
        .geometry = { .x = x, .y = y, .width = width, .height = height },
        .title = title,
    };
}

static void test_compositor(void)
{
    printf("the compositor copies opaque window surfaces in z-order\n");
    uint32_t target_storage[GUARD + 8 * 6 + GUARD];
    uint32_t bottom_pixels[5 * 4];
    uint32_t top_pixels[4 * 3];
    prepare(target_storage, 48);
    struct surface target, bottom, top;
    surface_init(&target, target_storage + GUARD, 48, 8, 6);
    surface_init(&bottom, bottom_pixels, 20, 5, 4);
    surface_init(&top, top_pixels, 12, 4, 3);
    surface_clear(&bottom, 2);
    surface_clear(&top, 3);

    struct window_manager manager;
    window_manager_init(&manager);
    struct window_spec bottom_spec = make_spec("bottom", -2, 1, 5, 4);
    struct window_spec top_spec = make_spec("top", 1, 2, 4, 3);
    WindowId bottom_id, top_id;
    window_create(&manager, &bottom_spec, &bottom_id);
    window_create(&manager, &top_spec, &top_id);
    check(window_attach_surface(&manager, bottom_id, &bottom), "attach the bottom surface");
    check(window_attach_surface(&manager, top_id, &top), "attach the top surface");
    check(compositor_compose(&manager, &target, 1), "compose into a target surface");
    check(surface_pixel(&target, 0, 1) == 2, "a partially off-screen bottom window is clipped");
    check(surface_pixel(&target, 1, 2) == 3, "the top window wins in the overlap");
    check(surface_pixel(&target, 7, 5) == 1, "uncovered pixels keep the desktop background");

    check(window_raise(&manager, bottom_id), "raise the bottom window");
    compositor_compose(&manager, &target, 1);
    check(surface_pixel(&target, 1, 2) == 2, "raising changes the overlap deterministically");
    struct window *raised = window_get(&manager, bottom_id);
    raised->minimized = true;
    compositor_compose(&manager, &target, 1);
    check(surface_pixel(&target, 1, 2) == 3, "a minimized window is not composited");
    check(guards_intact(target_storage, 48), "composition respects target guards");

    printf("surface attachment has explicit size semantics\n");
    struct window_geometry changed = { .x = 0, .y = 0, .width = 6, .height = 4 };
    check(!window_set_geometry(&manager, bottom_id, changed),
          "an attached surface prevents an implicit resize");
    check(window_attach_surface(&manager, bottom_id, NULL), "a surface can be detached");
    check(window_set_geometry(&manager, bottom_id, changed),
          "geometry can resize once no surface is attached");
    check(!window_attach_surface(&manager, bottom_id, &bottom),
          "a mismatched surface is refused");
    check(!compositor_compose(NULL, &target, 1), "no manager is refused");
    check(!compositor_compose(&manager, NULL, 1), "no target is refused");
}

/* M16. The narrow paths have to give the same picture as the wide ones, or the
 * mouse gets faster by drawing something slightly wrong. Checked by composing a
 * scene twice, once whole and once as a patchwork of regions, and comparing
 * every pixel.
 */
static void test_region_composition_agrees_with_whole_composition(void)
{
    printf("composing one region gives exactly what composing everything gives\n");

    uint32_t whole_pixels[64 * 40];
    uint32_t patch_pixels[64 * 40];
    uint32_t lower_pixels[20 * 12];
    uint32_t upper_pixels[16 * 10];
    struct surface whole, patch, lower, upper;

    check(surface_init(&whole, whole_pixels, 64 * 40, 64, 40), "the whole target");
    check(surface_init(&patch, patch_pixels, 64 * 40, 64, 40), "the patched target");
    check(surface_init(&lower, lower_pixels, 20 * 12, 20, 12), "a lower surface");
    check(surface_init(&upper, upper_pixels, 16 * 10, 16, 10), "one over it");
    surface_clear(&lower, 0x111111u);
    surface_clear(&upper, 0x222222u);
    /* Something inside each window, so a blit that copied the wrong source
     * column would show as more than a flat colour landing in the right place. */
    surface_fill_rect(&lower, 2, 2, 4, 4, 0x333333u);
    surface_fill_rect(&upper, 8, 5, 6, 3, 0x444444u);

    struct window_manager manager;
    window_manager_init(&manager);
    const struct window_spec lower_spec = {
        .geometry = { .x = 5, .y = 6, .width = 20, .height = 12 },
        .title = "lower",
    };
    const struct window_spec upper_spec = {
        .geometry = { .x = 15, .y = 10, .width = 16, .height = 10 },
        .title = "upper",
    };
    WindowId lower_id = WINDOW_ID_NONE;
    WindowId upper_id = WINDOW_ID_NONE;
    check(window_create(&manager, &lower_spec, &lower_id), "a lower window");
    check(window_create(&manager, &upper_spec, &upper_id), "one over it");
    check(window_attach_surface(&manager, lower_id, &lower), "lower attached");
    check(window_attach_surface(&manager, upper_id, &upper), "upper attached");

    check(compositor_compose(&manager, &whole, 0x090909u), "compose everything");

    /* Deliberately not aligned to any window edge, and deliberately overlapping
     * each other, so a region that double composes a pixel has to give the same
     * answer as one that composes it once. */
    surface_clear(&patch, 0xFFFFFFu);
    const struct region patches[] = {
        region_make(0, 0, 30, 40),
        region_make(25, 0, 39, 22),
        region_make(25, 20, 39, 20),
        region_make(13, 9, 9, 7),
    };
    for (size_t i = 0; i < sizeof patches / sizeof patches[0]; i++) {
        check(compositor_compose_region(&manager, &patch, 0x090909u, patches[i]),
              "compose one region");
    }

    bool identical = true;
    for (uint32_t y = 0; y < 40 && identical; y++) {
        for (uint32_t x = 0; x < 64; x++) {
            if (whole_pixels[y * 64 + x] != patch_pixels[y * 64 + x]) {
                printf("        first difference at %u,%u: whole %06X patched %06X\n",
                       x, y, whole_pixels[y * 64 + x], patch_pixels[y * 64 + x]);
                identical = false;
                break;
            }
        }
    }
    check(identical, "every pixel matches the whole composition");

    printf("a region composition changes nothing outside itself\n");
    surface_clear(&patch, 0xFFFFFFu);
    check(compositor_compose_region(&manager, &patch, 0x090909u,
                                    region_make(10, 10, 8, 8)),
          "compose a small region");
    bool outside_untouched = true;
    for (uint32_t y = 0; y < 40; y++) {
        for (uint32_t x = 0; x < 64; x++) {
            const bool inside = x >= 10 && x < 18 && y >= 10 && y < 18;
            if (!inside && patch_pixels[y * 64 + x] != 0xFFFFFFu) {
                outside_untouched = false;
            }
        }
    }
    check(outside_untouched, "not one pixel outside the region moved");

    printf("a region off the target is harmless rather than an error\n");
    check(compositor_compose_region(&manager, &patch, 0x090909u,
                                    region_make(500, 500, 10, 10)),
          "a region past the edge composes nothing and succeeds");
    check(compositor_compose_region(&manager, &patch, 0x090909u, region_none()),
          "and so does an empty one");
    check(!compositor_compose_region(NULL, &patch, 0, region_make(0, 0, 4, 4)),
          "no manager is still refused");
    check(!compositor_compose_region(&manager, NULL, 0, region_make(0, 0, 4, 4)),
          "and so is no target");
}

/* The clipped blit is what makes the region composition possible, so it gets its
 * own check that it copies the right source pixels and not merely the right
 * number of them. A clip that moved the destination without moving the source
 * would shift a window's contents sideways inside its own frame. */
static void test_clipped_blit_copies_the_right_source_pixels(void)
{
    printf("a clipped blit copies the source pixels that belong under the clip\n");

    uint32_t target_pixels[16 * 16];
    uint32_t source_pixels[8 * 8];
    struct surface target, source;
    check(surface_init(&target, target_pixels, 16 * 16, 16, 16), "a target");
    check(surface_init(&source, source_pixels, 8 * 8, 8, 8), "a source");

    /* Every source pixel carries its own coordinates, so a copy that took the
     * wrong column shows up as a wrong value rather than as a wrong colour. */
    for (uint32_t y = 0; y < 8; y++) {
        for (uint32_t x = 0; x < 8; x++) {
            source_pixels[y * 8 + x] = 0xC0000000u | (y << 8) | x;
        }
    }

    surface_clear(&target, 0);
    surface_blit_clipped(&target, &source, 4, 4, region_make(6, 6, 3, 3));

    bool right_pixels = true;
    bool nothing_else = true;
    for (uint32_t y = 0; y < 16; y++) {
        for (uint32_t x = 0; x < 16; x++) {
            const uint32_t got = target_pixels[y * 16 + x];
            const bool inside = x >= 6 && x < 9 && y >= 6 && y < 9;
            if (inside) {
                const uint32_t want = 0xC0000000u | ((y - 4) << 8) | (x - 4);
                if (got != want) {
                    right_pixels = false;
                }
            } else if (got != 0) {
                nothing_else = false;
            }
        }
    }
    check(right_pixels, "the pixels under the clip are the ones from the source");
    check(nothing_else, "and nothing outside the clip was written");

    printf("a clipped blit agrees with an unclipped one over the whole target\n");
    uint32_t reference_pixels[16 * 16];
    struct surface reference;
    check(surface_init(&reference, reference_pixels, 16 * 16, 16, 16), "a reference");
    surface_clear(&reference, 0);
    surface_clear(&target, 0);
    surface_blit(&reference, &source, -3, 5);
    surface_blit_clipped(&target, &source, -3, 5, region_make(0, 0, 16, 16));
    bool same_as_unclipped = true;
    for (size_t i = 0; i < 16 * 16; i++) {
        if (reference_pixels[i] != target_pixels[i]) {
            same_as_unclipped = false;
        }
    }
    check(same_as_unclipped, "including a source hanging off the left edge");

    printf("an empty or missed clip copies nothing\n");
    surface_clear(&target, 0x5A5A5A5Au);
    surface_blit_clipped(&target, &source, 4, 4, region_none());
    surface_blit_clipped(&target, &source, 4, 4, region_make(40, 40, 4, 4));
    surface_blit_clipped(&target, NULL, 4, 4, region_make(0, 0, 16, 16));
    surface_blit_clipped(NULL, &source, 4, 4, region_make(0, 0, 16, 16));
    bool untouched = true;
    for (size_t i = 0; i < 16 * 16; i++) {
        if (target_pixels[i] != 0x5A5A5A5Au) {
            untouched = false;
        }
    }
    check(untouched, "the target is exactly as it was");
}

int main(void)
{
    test_surface_creation_and_local_drawing();
    test_blit_clipping();
    test_cursor_overlay();
    test_compositor();
    test_clipped_blit_copies_the_right_source_pixels();
    test_region_composition_agrees_with_whole_composition();

    if (failures > 0) {
        printf("\n%d surface or compositor check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nsurface and compositor checks passed\n");
    return 0;
}
