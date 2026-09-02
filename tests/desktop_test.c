/* Host tests for the M18 ME OS Default environment.
 *
 * These drive real window objects, the real layout and real surfaces, and then
 * compose the result exactly as the kernel does. The claim being checked is the
 * one that makes this a tiling desktop: composing every visible window paints
 * each pixel of the workspace at most once, so no window is ever drawn over
 * another. That is checked by counting writes, not by looking.
 */
#include <stdio.h>
#include <string.h>

#include "compositor.h"
#include "desktop.h"

#define SCREEN_W 1280
#define SCREEN_H 800
#define POOL_PIXELS (SCREEN_W * SCREEN_H)

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

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* One store shared by every tile, sized for the workspace. The tiles never
 * overlap, so their areas together never exceed it. */
static uint32_t arena[POOL_PIXELS];
static uint32_t screen_pixels[POOL_PIXELS];

static const char *const NAMES[DESKTOP_MAX_APPS] = {
    "DEMO", "SYSTEM", "ABOUT", "NOTES",
};

static struct window_manager windows;
static struct desktop desktop;

static void build(size_t apps)
{
    window_manager_init(&windows);
    check(desktop_init(&desktop, &windows, SCREEN_W, SCREEN_H,
                       arena, POOL_PIXELS, rgb),
          "the desktop starts");
    for (size_t i = 0; i < apps; i++) {
        check(desktop_add(&desktop, NAMES[i]) == i, "an app is added");
    }
    check(desktop_relayout(&desktop), "and the first layout succeeds");
    desktop_paint_frames(&desktop);
}

/* Composes the desktop and reports how many pixels of the workspace were
 * written more than once. Zero is the whole point of a tiling layout. */
static size_t overlapping_pixels(void)
{
    static uint8_t writes[POOL_PIXELS];
    memset(writes, 0, sizeof writes);

    for (size_t z = 0; z < window_count(&windows); z++) {
        const struct window *window = window_at_z(&windows, z);
        if (window == NULL || window->minimized || window->surface == NULL) {
            continue;
        }
        for (int64_t y = 0; y < window->geometry.height; y++) {
            for (int64_t x = 0; x < window->geometry.width; x++) {
                const int64_t sx = window->geometry.x + x;
                const int64_t sy = window->geometry.y + y;
                if (sx < 0 || sy < 0 || sx >= SCREEN_W || sy >= SCREEN_H) {
                    continue;
                }
                writes[sy * SCREEN_W + sx]++;
            }
        }
    }

    size_t doubled = 0;
    for (size_t i = 0; i < POOL_PIXELS; i++) {
        if (writes[i] > 1) {
            doubled++;
        }
    }
    return doubled;
}

static bool anything_in_the_bars(void)
{
    for (size_t z = 0; z < window_count(&windows); z++) {
        const struct window *window = window_at_z(&windows, z);
        if (window == NULL || window->minimized) {
            continue;
        }
        if (window->geometry.y < desktop.layout.top_bar) {
            return true;
        }
        if (window->geometry.y + (int64_t)window->geometry.height >
            SCREEN_H - desktop.layout.bottom_bar) {
            return true;
        }
    }
    return false;
}

static void test_windows_tile_at_every_count(void)
{
    printf("one to four windows tile without a single shared pixel\n");
    for (size_t apps = 1; apps <= DESKTOP_MAX_APPS; apps++) {
        build(apps);
        const size_t doubled = overlapping_pixels();
        if (doubled != 0) {
            printf("        %zu windows: %zu pixels written twice\n", apps, doubled);
        }
        check(doubled == 0, "no pixel is painted by two windows");
        check(!anything_in_the_bars(), "and no window reaches into a bar");
        check(desktop_visible_count(&desktop) == apps, "all of them are visible");
    }
}

static void test_hiding_reflows_and_frees_the_space(void)
{
    printf("hiding a window gives its space to the others\n");
    build(4);
    const struct window *demo = window_get_const(&windows, desktop.apps[0].id);
    const uint32_t four_up_area = demo->geometry.width * demo->geometry.height;

    check(desktop_set_hidden(&desktop, desktop.apps[3].id, true), "hide the fourth");
    check(desktop_relayout(&desktop), "and lay out again");
    check(desktop_visible_count(&desktop) == 3, "three are visible");
    check(overlapping_pixels() == 0, "still nothing overlapping");

    check(desktop_set_hidden(&desktop, desktop.apps[2].id, true), "hide the third");
    check(desktop_relayout(&desktop), "and again");
    demo = window_get_const(&windows, desktop.apps[0].id);
    check(demo->geometry.width * demo->geometry.height > four_up_area,
          "the window that stayed is larger than it was");
    check(overlapping_pixels() == 0, "and nothing overlaps");

    printf("a hidden window is not composed and takes no tile space\n");
    const struct window *gone = window_get_const(&windows, desktop.apps[3].id);
    check(gone->minimized, "the hidden window is marked minimized");
    check(gone->surface == NULL, "and has no surface for the compositor to blit");

    printf("showing it again puts it back into the layout\n");
    check(desktop_set_hidden(&desktop, desktop.apps[3].id, false), "show it");
    check(desktop_relayout(&desktop), "and lay out");
    check(desktop_visible_count(&desktop) == 3, "three visible again");
    check(overlapping_pixels() == 0, "with nothing overlapping");

    printf("the last visible window is not allowed to hide\n");
    check(desktop_set_hidden(&desktop, desktop.apps[0].id, true), "hide one");
    check(desktop_set_hidden(&desktop, desktop.apps[1].id, true), "hide another");
    check(desktop_visible_count(&desktop) == 1, "one is left");
    check(!desktop_set_hidden(&desktop, desktop.apps[3].id, true),
          "and hiding it is refused");
    check(desktop_visible_count(&desktop) == 1, "so something is still on screen");
}

static void test_the_client_view_shares_the_frame(void)
{
    printf("an app draws into its client view and it lands inside the tile\n");
    build(2);
    struct desktop_app *app = desktop_app_at(&desktop, 0);
    check(app != NULL, "the first app");

    const struct window *window = window_get_const(&windows, app->id);
    check(app->client.width < window->geometry.width, "the client is narrower");
    check(app->client.height < window->geometry.height, "and shorter than the tile");

    surface_clear(&app->client, 0x00FF00u);
    /* The view shares the frame's pixels, so a write through the client has to
     * be readable through the frame at the offset the border and title imply. */
    const struct tile_area client =
        shell_client_area(window->geometry.width, window->geometry.height,
                          desktop.layout.border, SHELL_TITLE_HEIGHT);
    check(surface_pixel(&app->frame, client.x, client.y) == 0x00FF00u,
          "the frame sees what the client wrote");
    check(surface_pixel(&app->frame, 0, 0) != 0x00FF00u,
          "and the border was not written through");

    printf("the client cannot be used to paint over its own frame\n");
    surface_fill_rect(&app->client, -400, -400, 2000, 2000, 0x0000FFu);
    check(surface_pixel(&app->frame, 0, 0) != 0x0000FFu,
          "a huge fill through the client stops at the client edge");

    printf("a client rectangle is reported in screen coordinates\n");
    const struct region at_origin =
        desktop_client_region(&desktop, app->id, 0, 0, 10, 10);
    check(at_origin.x == window->geometry.x + client.x, "x is offset by both");
    check(at_origin.y == window->geometry.y + client.y, "and so is y");
    const struct region oversize =
        desktop_client_region(&desktop, app->id, 0, 0, 100000, 100000);
    check(oversize.width == app->client.width, "an oversized one is trimmed");
    check(oversize.height == app->client.height, "on both axes");
    const struct region nowhere =
        desktop_client_region(&desktop, WINDOW_ID_NONE, 0, 0, 10, 10);
    check(region_empty(&nowhere), "and an unknown window has no region");
}

static void test_focus_moves_between_visible_tiles(void)
{
    printf("focus steps through the visible windows and skips the hidden ones\n");
    build(4);
    check(window_focus(&windows, desktop.apps[0].id, false), "start on the first");

    check(desktop_focus_step(&desktop, 1), "step forward");
    check(window_focused(&windows) == desktop.apps[1].id, "onto the second");
    check(desktop_focus_step(&desktop, -1), "step back");
    check(window_focused(&windows) == desktop.apps[0].id, "onto the first again");

    check(desktop_set_hidden(&desktop, desktop.apps[1].id, true), "hide the second");
    check(desktop_focus_step(&desktop, 1), "step forward");
    check(window_focused(&windows) == desktop.apps[2].id,
          "past the hidden one to the third");

    printf("hiding the focused window moves focus somewhere real\n");
    check(desktop_set_hidden(&desktop, desktop.apps[2].id, true), "hide the focused");
    check(!desktop.apps[desktop_index_of(&desktop, window_focused(&windows))].hidden,
          "focus landed on a window that is on screen");

    printf("one visible window steps onto itself rather than nowhere\n");
    build(1);
    check(window_focus(&windows, desktop.apps[0].id, false), "focus it");
    check(desktop_focus_step(&desktop, 1), "step");
    check(window_focused(&windows) == desktop.apps[0].id, "and stay there");
}

static void test_the_focused_frame_is_the_one_with_the_accent(void)
{
    printf("exactly one tile carries the accent border\n");
    build(3);
    check(window_focus(&windows, desktop.apps[1].id, false), "focus the second");
    desktop_paint_frames(&desktop);

    size_t accented = 0;
    for (size_t i = 0; i < desktop.app_count; i++) {
        if (surface_pixel(&desktop.apps[i].frame, 0, 0) == desktop.theme.accent) {
            accented++;
        }
    }
    check(accented == 1, "one and only one");
    check(surface_pixel(&desktop.apps[1].frame, 0, 0) == desktop.theme.accent,
          "and it is the focused one");
    check(surface_pixel(&desktop.apps[0].frame, 0, 0) == desktop.theme.border,
          "the others carry the idle border");
}

static void test_the_bars_are_drawn_after_composition(void)
{
    printf("the bars survive a composition that would otherwise clear them\n");
    build(2);
    struct surface screen;
    check(surface_init(&screen, screen_pixels, POOL_PIXELS, SCREEN_W, SCREEN_H),
          "a screen surface");

    check(compositor_compose(&windows, &screen, desktop.theme.desktop),
          "compose the windows");
    desktop_draw_bars(&desktop, &screen, 125);

    check(surface_pixel(&screen, 4, 4) == desktop.theme.bar,
          "the top bar is on the screen");
    check(surface_pixel(&screen, 4, SCREEN_H - 4) == desktop.theme.bar,
          "and so is the taskbar");

    const struct region top = desktop_top_bar_region(&desktop);
    const struct region bottom = desktop_taskbar_region(&desktop);
    check(top.y == 0 && top.height == desktop.layout.top_bar, "the top strip");
    check(bottom.y + bottom.height == SCREEN_H, "and the bottom one reach the edges");
    check(!region_overlaps(top, bottom), "and they do not overlap each other");
}

static void test_a_taskbar_click_finds_the_right_app(void)
{
    printf("a taskbar click lands on the app whose button was drawn\n");
    build(3);
    const int64_t y = SCREEN_H - desktop.layout.bottom_bar / 2;
    bool each = true;
    for (size_t i = 0; i < 3; i++) {
        const int64_t x = 6 + 40 + 6 + (int64_t)i * (150 + 4) + 75;
        if (desktop_taskbar_hit(&desktop, x, y) != i) {
            each = false;
        }
    }
    check(each, "each button hits its own app");
    check(desktop_taskbar_hit(&desktop, 640, 400) == DESKTOP_MAX_APPS,
          "a click on the desktop hits nothing");
    check(desktop_taskbar_hit(NULL, 100, y) == DESKTOP_MAX_APPS,
          "and no desktop hits nothing");
}

static void test_nonsense_is_refused(void)
{
    printf("bad arguments are refused rather than half applied\n");
    struct desktop empty;
    check(!desktop_init(NULL, &windows, SCREEN_W, SCREEN_H, arena, POOL_PIXELS, rgb),
          "no desktop");
    check(!desktop_init(&empty, NULL, SCREEN_W, SCREEN_H, arena, POOL_PIXELS, rgb),
          "no windows");
    check(!desktop_init(&empty, &windows, 0, 0, arena, POOL_PIXELS, rgb), "no screen");
    check(!desktop_init(&empty, &windows, SCREEN_W, SCREEN_H, arena, POOL_PIXELS, NULL),
          "no packer");
    check(!desktop_init(&empty, &windows, SCREEN_W, SCREEN_H, NULL, POOL_PIXELS, rgb),
          "no arena");
    check(!desktop_init(&empty, &windows, SCREEN_W, SCREEN_H, arena, 0, rgb),
          "an empty arena");

    build(DESKTOP_MAX_APPS);
    check(desktop_add(&desktop, "ONE TOO MANY") == DESKTOP_MAX_APPS,
          "an app past the limit is refused");
    check(desktop_index_of(&desktop, WINDOW_ID_NONE) == DESKTOP_MAX_APPS,
          "an unknown window has no index");
    check(desktop_app_at(&desktop, 99) == NULL, "and no app past the end");
    check(!desktop_relayout(NULL), "no desktop cannot be laid out");
    check(!desktop_focus_step(&desktop, 0), "a step of nowhere is refused");
}

int main(void)
{
    test_windows_tile_at_every_count();
    test_hiding_reflows_and_frees_the_space();
    test_the_client_view_shares_the_frame();
    test_focus_moves_between_visible_tiles();
    test_the_focused_frame_is_the_one_with_the_accent();
    test_the_bars_are_drawn_after_composition();
    test_a_taskbar_click_finds_the_right_app();
    test_nonsense_is_refused();

    if (failures > 0) {
        printf("\n%d desktop check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nME OS Default desktop checks passed\n");
    return 0;
}
