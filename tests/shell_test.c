/* Host tests for the M18 desktop shell.
 *
 * The shell only draws, so what is worth checking is not that it looks right,
 * which needs eyes, but that it never draws outside what it was given, that the
 * rectangles a click is matched against are the ones that were painted, and
 * that a focused tile is visibly different from an unfocused one.
 */
#include <stdio.h>

#include "shell.h"
#include "surface.h"

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

/* A packer that keeps the three channels apart, so a test can tell which colour
 * landed somewhere without knowing a real framebuffer's layout. */
static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
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

static void test_theme_is_one_place(void)
{
    printf("the theme is built once and has no colour left unset\n");
    const struct theme theme = theme_default(rgb);
    check(theme.desktop != theme.bar, "the ground and the bars differ");
    check(theme.accent != theme.border, "focused and unfocused borders differ");
    check(theme.bar_text != theme.bar, "bar text is visible against the bar");
    check(theme.chrome_text != theme.chrome, "and title text against the title");
    check(theme.desktop != 0, "the ground is not pure black");

    const struct theme none = theme_default(NULL);
    check(none.accent == 0, "no packer gives a blank theme rather than a crash");
}

static void test_a_frame_stays_inside_its_own_tile(void)
{
    printf("a window frame never draws outside the surface it was given\n");
    uint32_t storage[GUARD + 200 * 120 + GUARD];
    prepare(storage, 200 * 120);
    struct surface frame;
    check(surface_init(&frame, storage + GUARD, 200 * 120, 200, 120), "a frame");

    const struct theme theme = theme_default(rgb);
    shell_frame(&frame, &theme, "DEMO", true, 2);
    check(guards_intact(storage, 200 * 120), "the guards either side are untouched");

    printf("the focused border is the accent and an unfocused one is not\n");
    const uint32_t focused_corner = surface_pixel(&frame, 0, 0);
    shell_frame(&frame, &theme, "DEMO", false, 2);
    const uint32_t idle_corner = surface_pixel(&frame, 0, 0);
    check(focused_corner == theme.accent, "the focused edge is the accent");
    check(idle_corner == theme.border, "and the unfocused edge is not");
    check(focused_corner != idle_corner, "so the two are told apart at a glance");

    printf("the client area is inside the border and below the title\n");
    const struct tile_area client =
        shell_client_area(200, 120, 2, SHELL_TITLE_HEIGHT);
    check(client.x == 2 && client.y == 2 + SHELL_TITLE_HEIGHT, "it starts inside");
    check(client.x + client.width == 198, "and ends inside on the right");
    check(client.y + client.height == 118, "and at the bottom");

    printf("a tile too small for a title still gives no negative client area\n");
    const struct tile_area tiny = shell_client_area(10, 10, 2, SHELL_TITLE_HEIGHT);
    check(tiny.width == 0 && tiny.height == 0, "it is empty rather than negative");

    printf("a frame smaller than its own border does not turn inside out\n");
    uint32_t small_storage[GUARD + 6 * 6 + GUARD];
    prepare(small_storage, 6 * 6);
    struct surface small;
    check(surface_init(&small, small_storage + GUARD, 6 * 6, 6, 6), "a tiny frame");
    shell_frame(&small, &theme, "X", true, 20);
    check(guards_intact(small_storage, 6 * 6), "and it stayed inside itself");
}

static void test_the_buttons_are_where_the_clicks_are_matched(void)
{
    printf("the title bar buttons sit inside the frame and do not overlap\n");
    struct tile_area hide, close;
    check(shell_hide_button(200, 2, &hide), "a hide button exists at 200 wide");
    check(shell_close_button(200, 2, &close), "and a close button");
    check(hide.x + hide.width <= close.x, "they do not overlap");
    check(close.x + close.width <= 200 - 2, "and close stays inside the border");
    check(hide.x > 2, "and hide stays clear of the left border");

    printf("a frame too narrow for buttons says so rather than placing them badly\n");
    check(!shell_hide_button(20, 2, &hide), "no hide button at 20 wide");
    check(hide.width == 0, "and the rectangle handed back is empty");
    check(!shell_close_button(20, 2, &close), "no close button either");
    check(!shell_hide_button(200, 2, NULL), "nowhere to write is refused");
}

static void test_the_bars_stay_in_their_own_strips(void)
{
    printf("neither bar draws outside its reserved strip\n");
    const int64_t width = 320, height = 200;
    const int64_t top_height = 28, bottom_height = 34;
    uint32_t storage[GUARD + 320 * 200 + GUARD];
    prepare(storage, 320 * 200);
    struct surface desktop;
    check(surface_init(&desktop, storage + GUARD, 320 * 200, 320, 200), "a desktop");
    surface_clear(&desktop, 0x00FF00u);

    const struct theme theme = theme_default(rgb);
    const bool occupied[4] = { true, true, false, false };
    shell_top_bar(&desktop, &theme, width, top_height, 1, occupied, 4, "DEMO",
                  "12:34:56", 125);

    bool below_untouched = true;
    for (int64_t y = top_height; y < height; y++) {
        for (int64_t x = 0; x < width; x++) {
            if (surface_pixel(&desktop, x, y) != 0x00FF00u) {
                below_untouched = false;
            }
        }
    }
    check(below_untouched, "the top bar left everything below it alone");

    const struct shell_task tasks[] = {
        { "DEMO", true, false, false },
        { "SYSTEM INFO", false, false, true },
        { "ABOUT", false, true, false },
    };
    shell_taskbar(&desktop, &theme, width, height, bottom_height, tasks, 3);

    bool above_untouched = true;
    for (int64_t y = top_height; y < height - bottom_height; y++) {
        for (int64_t x = 0; x < width; x++) {
            if (surface_pixel(&desktop, x, y) != 0x00FF00u) {
                above_untouched = false;
            }
        }
    }
    check(above_untouched, "the taskbar left everything above it alone");
    check(guards_intact(storage, 320 * 200), "and both stayed inside the surface");

    printf("more tasks than fit are dropped rather than drawn off the edge\n");
    const struct shell_task many[] = {
        { "ONE", false, false, false },   { "TWO", false, false, false },
        { "THREE", false, false, false }, { "FOUR", false, false, false },
        { "FIVE", false, false, false },  { "SIX", false, false, false },
        { "SEVEN", false, false, false }, { "EIGHT", false, false, false },
    };
    shell_taskbar(&desktop, &theme, width, height, bottom_height, many, 8);
    check(guards_intact(storage, 320 * 200), "eight tasks on a narrow bar stay inside");
}

static void test_a_click_lands_on_the_button_that_was_drawn(void)
{
    printf("hit testing the taskbar agrees with where the buttons were put\n");
    const int64_t width = 1280, height = 800, bar = 34;

    const struct tile_area launcher = shell_launcher_button(height, bar);
    check(launcher.y >= height - bar, "the launcher is inside the taskbar");
    check(launcher.y + launcher.height <= height, "and does not run off the bottom");

    /* The middle of each of three buttons hits that button and no other. */
    bool each_hits_itself = true;
    for (size_t i = 0; i < 3; i++) {
        const int64_t y = height - bar / 2;
        const int64_t x = 6 + 40 + 6 + (int64_t)i * (150 + 4) + 75;
        if (shell_task_at(width, height, bar, 3, x, y) != i) {
            each_hits_itself = false;
        }
    }
    check(each_hits_itself, "the middle of each button hits that button");

    check(shell_task_at(width, height, bar, 3, 5, height - 5) == 3,
          "the launcher is not a task");
    check(shell_task_at(width, height, bar, 3, 640, 400) == 3,
          "a click in the middle of the desktop hits no task");
    check(shell_task_at(width, height, bar, 3, 1270, height - 5) == 3,
          "and nor does one past the last button");
    check(shell_task_at(width, height, bar, 0, 200, height - 5) == 0,
          "with no tasks at all, nothing is hit");
}

static void test_the_icon_stays_in_its_box(void)
{
    printf("the ME OS mark draws inside the size it was given\n");
    uint32_t storage[GUARD + 64 * 64 + GUARD];
    prepare(storage, 64 * 64);
    struct surface surface;
    check(surface_init(&surface, storage + GUARD, 64 * 64, 64, 64), "a surface");
    surface_clear(&surface, 0);

    shell_icon(&surface, 10, 10, 16, 0xFFFFFFu, 0x101010u);
    bool outside_clean = true;
    for (int64_t y = 0; y < 64; y++) {
        for (int64_t x = 0; x < 64; x++) {
            const bool inside = x >= 10 && x < 26 && y >= 10 && y < 26;
            if (!inside && surface_pixel(&surface, x, y) != 0) {
                outside_clean = false;
            }
        }
    }
    check(outside_clean, "not one pixel outside the box");
    check(guards_intact(storage, 64 * 64), "and the surface guards held");

    printf("an icon too small to have rings is refused rather than drawn wrong\n");
    surface_clear(&surface, 0);
    shell_icon(&surface, 10, 10, 2, 0xFFFFFFu, 0x101010u);
    bool nothing_drawn = true;
    for (size_t i = 0; i < 64 * 64; i++) {
        if (surface.pixels[i] != 0) {
            nothing_drawn = false;
        }
    }
    check(nothing_drawn, "a two pixel icon draws nothing");

    printf("the icon takes its colour from the accent, so a theme change reaches it\n");
    surface_clear(&surface, 0);
    shell_icon(&surface, 0, 0, 16, 0x00ABCDu, 0x101010u);
    bool found_accent = false;
    for (size_t i = 0; i < 64 * 64; i++) {
        if (surface.pixels[i] == 0x00ABCDu) {
            found_accent = true;
        }
    }
    check(found_accent, "the accent given is the accent drawn");
}

int main(void)
{
    test_theme_is_one_place();
    test_a_frame_stays_inside_its_own_tile();
    test_the_buttons_are_where_the_clicks_are_matched();
    test_the_bars_stay_in_their_own_strips();
    test_a_click_lands_on_the_button_that_was_drawn();
    test_the_icon_stays_in_its_box();

    if (failures > 0) {
        printf("\n%d desktop shell check(s) FAILED\n", failures);
        return 1;
    }
    printf("\ndesktop shell checks passed\n");
    return 0;
}
