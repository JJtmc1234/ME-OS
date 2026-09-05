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

/* Five are ME OS's own and the sixth is the slot a program's window goes in,
 * which is why there is one more of these than the desktop opens at boot. */
static const char *const NAMES[DESKTOP_MAX_APPS] = {
    "DEMO", "SYSTEM", "ABOUT", "NOTES", "EDITOR", "A PROGRAM",
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

    /* An empty workspace used to be refused, on the grounds that a desktop with
     * nothing on it and no way back is not worth reaching with one key. There is
     * a way back now: the taskbar shows every window on every workspace. */
    printf("hiding everything leaves an empty workspace, not a broken one\n");
    check(desktop_set_hidden(&desktop, desktop.apps[0].id, true), "hide one");
    check(desktop_set_hidden(&desktop, desktop.apps[1].id, true), "hide another");
    check(desktop_set_hidden(&desktop, desktop.apps[3].id, true), "and the last");
    check(desktop_visible_count(&desktop) == 0, "nothing is on screen");
    check(desktop_relayout(&desktop), "and laying out an empty screen succeeds");
    check(desktop_set_hidden(&desktop, desktop.apps[0].id, false), "one comes back");
    check(desktop_visible_count(&desktop) == 1, "and it is on screen again");
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
    desktop_draw_bars(&desktop, &screen, "12:34:56", 125);

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


    printf("an app can be taken away again, which is what a program needs\n");
    /* Until M34 every window on this desktop was made once at boot and never
     * went away, so there was nothing to remove. A program's window has to go
     * when the program does, or the next program finds the desktop full of the
     * last one's. */
    build(DESKTOP_MAX_APPS);
    check(desktop.app_count == DESKTOP_MAX_APPS, "the desktop is full");
    check(desktop_add(&desktop, "NO ROOM") == DESKTOP_MAX_APPS,
          "so nothing else fits");

    const WindowId leaving = desktop_app_at(&desktop, 2)->id;
    const WindowId after = desktop_app_at(&desktop, 3)->id;
    check(desktop_remove(&desktop, leaving), "one in the middle is removed");
    check(desktop.app_count == DESKTOP_MAX_APPS - 1, "and the count drops");
    check(desktop_index_of(&desktop, leaving) == DESKTOP_MAX_APPS,
          "it can no longer be found");
    check(desktop_app_at(&desktop, 2)->id == after,
          "the ones after it moved down rather than leaving a hole");
    check(desktop_relayout(&desktop), "and the layout still works");

    check(!desktop_remove(&desktop, leaving), "removing it twice is refused");
    check(!desktop_remove(&desktop, WINDOW_ID_NONE), "so is removing nothing");
    check(desktop.app_count == DESKTOP_MAX_APPS - 1, "neither changed the count");

    check(desktop_add(&desktop, "A PROGRAM") == DESKTOP_MAX_APPS - 1,
          "and the freed slot can be used again");
    check(desktop_relayout(&desktop), "with a working layout");

    /* Every one of them, so nothing depends on which slot was taken. */
    build(DESKTOP_MAX_APPS);
    for (size_t i = DESKTOP_MAX_APPS; i > 0; i--) {
        check(desktop_remove(&desktop, desktop_app_at(&desktop, i - 1)->id),
              "each one is removed in turn");
    }
    check(desktop.app_count == 0, "and the desktop ends up empty");

    build(DESKTOP_MAX_APPS);
    const WindowId first = desktop_app_at(&desktop, 0)->id;
    check(desktop_remove(&desktop, first), "the first one is removed");
    check(desktop_app_at(&desktop, 0)->id != first, "and something else is first now");
    check(desktop_relayout(&desktop), "with a working layout");

    build(DESKTOP_MAX_APPS);
    check(desktop_add(&desktop, "ONE TOO MANY") == DESKTOP_MAX_APPS,
          "an app past the limit is refused");
    check(desktop_index_of(&desktop, WINDOW_ID_NONE) == DESKTOP_MAX_APPS,
          "an unknown window has no index");
    check(desktop_app_at(&desktop, 99) == NULL, "and no app past the end");
    check(!desktop_relayout(NULL), "no desktop cannot be laid out");
    check(!desktop_focus_step(&desktop, 0), "a step of nowhere is refused");
}

/* M22. A workspace is a different kind of absent from a hidden window: hiding is
 * about this screen, a workspace is about which screen you are looking at. */
static void test_workspaces(void)
{
    printf("windows start on the workspace that was current when they were made\n");
    build(4);
    check(desktop.workspace == 1, "the desktop starts on the first");
    check(desktop_visible_count(&desktop) == 4, "with everything on it");
    check(desktop_workspace_occupied(&desktop, 1), "the first has windows");
    check(!desktop_workspace_occupied(&desktop, 2), "and the second has none");

    printf("moving a window takes it off this screen without hiding it\n");
    check(desktop_move_to_workspace(&desktop, desktop.apps[3].id, 2), "move one");
    check(desktop_visible_count(&desktop) == 3, "three are left here");
    check(!desktop.apps[3].hidden, "and the one that left is not hidden");
    check(desktop_workspace_occupied(&desktop, 2), "the second workspace has it");
    check(desktop_relayout(&desktop), "the rest lay out again");
    check(overlapping_pixels() == 0, "with nothing overlapping");

    const struct window *gone = window_get_const(&windows, desktop.apps[3].id);
    check(gone->minimized, "the compositor is told not to draw it");

    printf("switching workspace changes which windows are on screen\n");
    check(desktop_switch_workspace(&desktop, 2), "go to the second");
    check(desktop_relayout(&desktop), "and lay the new screen out");
    check(desktop_visible_count(&desktop) == 1, "one window is there");
    check(desktop_on_screen(&desktop, 3), "and it is the one that was moved");
    check(!desktop_on_screen(&desktop, 0), "the others are not");
    const struct window *alone = window_get_const(&windows, desktop.apps[3].id);
    check(!alone->minimized, "and is drawn now");

    printf("focus follows, because focus on a window nobody sees types into the void\n");
    check(window_focused(&windows) == desktop.apps[3].id, "focus is on the visible one");
    check(desktop_switch_workspace(&desktop, 1), "back to the first");
    check(desktop_relayout(&desktop), "and lay it out");
    check(desktop_on_screen(&desktop, desktop_index_of(&desktop,
                                                       window_focused(&windows))),
          "and focus landed on something on this screen");

    printf("moving the focused window away moves focus to something still here\n");
    window_focus(&windows, desktop.apps[0].id, false);
    check(desktop_move_to_workspace(&desktop, desktop.apps[0].id, 3), "move it away");
    check(desktop_relayout(&desktop), "and lay out");
    check(window_focused(&windows) != desktop.apps[0].id, "focus left with it");
    check(desktop_on_screen(&desktop, desktop_index_of(&desktop,
                                                       window_focused(&windows))),
          "onto a window that is on screen");

    printf("an empty workspace is a real state rather than a fault\n");
    build(1);
    check(desktop_move_to_workspace(&desktop, desktop.apps[0].id, 4), "send the only one away");
    check(desktop_visible_count(&desktop) == 0, "this workspace is empty");
    check(desktop_relayout(&desktop), "and laying it out succeeds");
    check(desktop_switch_workspace(&desktop, 4), "following it");
    check(desktop_relayout(&desktop), "and laying that out");
    check(desktop_visible_count(&desktop) == 1, "finds it there");

    printf("a workspace that does not exist is refused\n");
    build(2);
    check(!desktop_switch_workspace(&desktop, 0), "there is no workspace zero");
    check(!desktop_switch_workspace(&desktop, DESKTOP_WORKSPACES + 1), "nor one past the last");
    check(!desktop_switch_workspace(&desktop, 1), "nor a move to the one you are on");
    check(!desktop_move_to_workspace(&desktop, desktop.apps[0].id, 99), "nor a move to nowhere");
    check(!desktop_move_to_workspace(&desktop, WINDOW_ID_NONE, 2), "nor of nothing");
    check(!desktop_switch_workspace(NULL, 2), "and no desktop switches nowhere");
    check(desktop.workspace == 1, "so the desktop stayed where it was");
}

/* The bug this exists for. The tiles share one arena, handed out in layout
 * order, so the slice a window had is given to a different window the moment it
 * leaves the screen. An app that kept drawing into its old surface was writing
 * into somebody else's tile, and it showed up as one window's picture smeared
 * across a third one.
 *
 * Checked by doing exactly what the app was doing: drawing into the surface it
 * held before it went away, and looking at whether anybody else changed.
 */
static void test_an_offscreen_window_cannot_draw_into_another(void)
{
    printf("a window that leaves the screen has its surface emptied\n");
    build(4);
    struct desktop_app *leaving = desktop_app_at(&desktop, 0);
    check(surface_valid(&leaving->client), "it has a real surface while on screen");

    check(desktop_move_to_workspace(&desktop, leaving->id, 2), "send it away");
    check(desktop_relayout(&desktop), "and lay the rest out");
    check(!surface_valid(&leaving->client), "its client is emptied");
    check(!surface_valid(&leaving->frame), "and so is its frame");

    printf("so drawing into it changes nothing anywhere\n");
    struct desktop_app *staying = desktop_app_at(&desktop, 1);
    surface_clear(&staying->client, 0x00FF00u);

    /* Every call an app might make on a surface it no longer owns. */
    surface_clear(&leaving->client, 0xFF0000u);
    surface_fill_rect(&leaving->client, 0, 0, 4000, 4000, 0xFF0000u);
    surface_draw_line(&leaving->client, -100, -100, 4000, 4000, 0xFF0000u);
    surface_draw_string(&leaving->client, "SMEAR", 0, 0, 0xFF0000u, 4);
    surface_put_pixel(&leaving->client, 10, 10, 0xFF0000u);

    bool untouched = true;
    for (uint32_t y = 0; y < staying->client.height; y++) {
        for (uint32_t x = 0; x < staying->client.width; x++) {
            if (surface_pixel(&staying->client, x, y) != 0x00FF00u) {
                untouched = false;
            }
        }
    }
    check(untouched, "the window that stayed is exactly as it was");

    printf("and it gets a real surface again when it comes back\n");
    check(desktop_switch_workspace(&desktop, 2), "go to where it went");
    check(desktop_relayout(&desktop), "and lay that out");
    check(surface_valid(&leaving->client), "its surface is real again");
    surface_clear(&leaving->client, 0x0000FFu);
    check(surface_pixel(&leaving->client, 1, 1) == 0x0000FFu, "and it can be drawn in");

    printf("the same holds for a window that is merely hidden\n");
    build(3);
    struct desktop_app *hidden = desktop_app_at(&desktop, 2);
    check(desktop_set_hidden(&desktop, hidden->id, true), "hide one");
    check(desktop_relayout(&desktop), "and lay out");
    check(!surface_valid(&hidden->client), "its surface is emptied too");
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
    test_workspaces();
    test_an_offscreen_window_cannot_draw_into_another();
    test_nonsense_is_refused();

    if (failures > 0) {
        printf("\n%d desktop check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nME OS Default desktop checks passed\n");
    return 0;
}
