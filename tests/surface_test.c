/* Host tests for M14 software surfaces and composition. */
#include <stdint.h>
#include <stdio.h>

#include "compositor.h"
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

int main(void)
{
    test_surface_creation_and_local_drawing();
    test_blit_clipping();
    test_cursor_overlay();
    test_compositor();

    if (failures > 0) {
        printf("\n%d surface or compositor check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nsurface and compositor checks passed\n");
    return 0;
}
