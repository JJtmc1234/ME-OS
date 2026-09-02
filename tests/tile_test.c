/* Host tests for the M17 tiling layout.
 *
 * The claim being checked is not that the pictures look nice. It is that no two
 * visible windows ever share a pixel, that none of them reaches into the bars,
 * and that a tile too small to be usable is left out rather than drawn wrong.
 * Those are properties, so they are checked for every window count the layout
 * supports rather than for the four that were drawn by hand.
 */
#include <stdio.h>

#include "tile.h"

#define SCREEN_W 1280
#define SCREEN_H 800

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

static bool overlap(struct tile_area a, struct tile_area b)
{
    return a.x < b.x + b.width && b.x < a.x + a.width &&
           a.y < b.y + b.height && b.y < a.y + a.height;
}

static bool inside(struct tile_config config, struct tile_area tile,
                   int64_t screen_w, int64_t screen_h)
{
    return tile.x >= 0 && tile.y >= config.top_bar &&
           tile.x + tile.width <= screen_w &&
           tile.y + tile.height <= screen_h - config.bottom_bar;
}

static void test_the_four_layouts_that_were_drawn(void)
{
    const struct tile_config config = tile_defaults();
    struct tile_area tiles[8];

    printf("one window fills the workspace, with no divider taken out\n");
    check(tile_layout(config, SCREEN_W, SCREEN_H, 1, tiles, 8) == 1, "one placed");
    const struct tile_area workspace = tile_workspace(config, SCREEN_W, SCREEN_H);
    check(tiles[0].x == workspace.x && tiles[0].y == workspace.y &&
          tiles[0].width == workspace.width && tiles[0].height == workspace.height,
          "and it is exactly the workspace");

    printf("two windows are side by side and the same height\n");
    check(tile_layout(config, SCREEN_W, SCREEN_H, 2, tiles, 8) == 2, "two placed");
    check(tiles[0].y == tiles[1].y, "the same top");
    check(tiles[0].height == tiles[1].height, "and the same height");
    check(tiles[0].x < tiles[1].x, "the first is on the left");
    check(tiles[1].x == tiles[0].x + tiles[0].width + config.inner_gap,
          "with exactly one inner gap between them");

    printf("three windows put one on the left and two stacked on the right\n");
    check(tile_layout(config, SCREEN_W, SCREEN_H, 3, tiles, 8) == 3, "three placed");
    check(tiles[0].height == workspace.height, "the left one is full height");
    check(tiles[1].x == tiles[2].x && tiles[1].width == tiles[2].width,
          "the right two share a column");
    check(tiles[2].y == tiles[1].y + tiles[1].height + config.inner_gap,
          "and are one gap apart");

    printf("four windows make a two by two grid\n");
    check(tile_layout(config, SCREEN_W, SCREEN_H, 4, tiles, 8) == 4, "four placed");
    check(tiles[0].x == tiles[1].x, "the left pair share a column");
    check(tiles[2].x == tiles[3].x, "and so does the right pair");
    check(tiles[0].y == tiles[2].y, "the top pair share a row");
    check(tiles[1].y == tiles[3].y, "and so does the bottom pair");
    check(tiles[0].width == tiles[2].width, "an even split by default");
}

/* The property that makes this a tiling desktop rather than a pile of windows.
 * Checked for every count, because the count nobody drew is where a special
 * case would be wrong. */
static void test_no_two_tiles_ever_share_a_pixel(void)
{
    printf("no two tiles overlap, at any count and any screen size\n");
    const struct tile_config config = tile_defaults();
    const int64_t sizes[][2] = {
        {1280, 800}, {1920, 1080}, {1024, 768}, {800, 600}, {640, 480},
    };

    bool clean = true;
    bool within = true;
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        for (size_t count = 1; count <= 8; count++) {
            struct tile_area tiles[8];
            const size_t placed =
                tile_layout(config, sizes[s][0], sizes[s][1], count, tiles, 8);
            for (size_t i = 0; i < placed; i++) {
                if (!inside(config, tiles[i], sizes[s][0], sizes[s][1])) {
                    printf("        %lldx%lld count %zu tile %zu escapes the workspace\n",
                           (long long)sizes[s][0], (long long)sizes[s][1], count, i);
                    within = false;
                }
                for (size_t j = i + 1; j < placed; j++) {
                    if (overlap(tiles[i], tiles[j])) {
                        printf("        %lldx%lld count %zu: tiles %zu and %zu overlap\n",
                               (long long)sizes[s][0], (long long)sizes[s][1],
                               count, i, j);
                        clean = false;
                    }
                }
            }
        }
    }
    check(clean, "one to eight windows on five screen sizes, none overlapping");
    check(within, "and none of them reaching into a bar or off the screen");
}

static void test_bars_are_reserved(void)
{
    printf("the bars keep their space whatever else happens\n");
    const struct tile_config config = tile_defaults();
    struct tile_area tiles[8];

    for (size_t count = 1; count <= 4; count++) {
        const size_t placed = tile_layout(config, SCREEN_W, SCREEN_H, count, tiles, 8);
        bool clear = true;
        for (size_t i = 0; i < placed; i++) {
            if (tiles[i].y < config.top_bar) clear = false;
            if (tiles[i].y + tiles[i].height > SCREEN_H - config.bottom_bar) clear = false;
        }
        check(clear && placed == count, "no tile reaches into a bar");
    }

    printf("bars taller than the screen leave nothing to tile, rather than a negative\n");
    struct tile_config crowded = tile_defaults();
    crowded.top_bar = 500;
    crowded.bottom_bar = 500;
    const struct tile_area none = tile_workspace(crowded, SCREEN_W, SCREEN_H);
    check(none.width == 0 && none.height == 0, "the workspace is empty");
    check(tile_layout(crowded, SCREEN_W, SCREEN_H, 2, tiles, 8) == 0,
          "and nothing is placed");
}

static void test_a_tile_too_small_is_not_placed(void)
{
    printf("a tile below the minimum is left out rather than drawn too small\n");
    const struct tile_config config = tile_defaults();
    struct tile_area tiles[8];

    /* Eight windows on a very small screen cannot all meet the minimum, and
     * placing them anyway would produce windows a person cannot use and a
     * compositor would still have to draw. */
    const size_t placed = tile_layout(config, 320, 240, 8, tiles, 8);
    check(placed == 0, "eight windows on 320x240 places none");

    bool all_big_enough = true;
    for (size_t count = 1; count <= 8; count++) {
        const size_t n = tile_layout(config, SCREEN_W, SCREEN_H, count, tiles, 8);
        for (size_t i = 0; i < n; i++) {
            if (tiles[i].width < config.min_width ||
                tiles[i].height < config.min_height) {
                all_big_enough = false;
            }
        }
    }
    check(all_big_enough, "every tile that is placed meets the minimum");
}

static void test_the_master_split_moves_the_divider(void)
{
    printf("the master percentage moves the divider and nothing else\n");
    struct tile_config config = tile_defaults();
    struct tile_area even[8], wide[8];

    check(tile_layout(config, SCREEN_W, SCREEN_H, 2, even, 8) == 2, "an even split");
    config.master_percent = 70;
    check(tile_layout(config, SCREEN_W, SCREEN_H, 2, wide, 8) == 2, "a wider left");

    check(wide[0].width > even[0].width, "the left tile grew");
    check(wide[1].width < even[1].width, "the right tile shrank");
    check(wide[0].width + wide[1].width == even[0].width + even[1].width,
          "and the total width did not change");
    check(wide[1].x == wide[0].x + wide[0].width + config.inner_gap,
          "the gap between them is still exactly one inner gap");

    printf("a percentage past either end stops at the end\n");
    config.master_percent = -40;
    check(tile_layout(config, SCREEN_W, SCREEN_H, 2, wide, 8) == 2,
          "a negative percentage still lays out");
    check(wide[0].width > 0 && wide[1].width > 0, "with both tiles still real");
    config.master_percent = 900;
    check(tile_layout(config, SCREEN_W, SCREEN_H, 2, wide, 8) == 2,
          "and so does one past 100");
    check(wide[0].width > 0 && wide[1].width > 0, "with both tiles still real");
}

static void test_invalid_requests_are_refused(void)
{
    printf("nonsense is refused rather than written somewhere\n");
    const struct tile_config config = tile_defaults();
    struct tile_area tiles[8];
    check(tile_layout(config, SCREEN_W, SCREEN_H, 0, tiles, 8) == 0, "no windows");
    check(tile_layout(config, SCREEN_W, SCREEN_H, 2, NULL, 8) == 0, "nowhere to write");
    check(tile_layout(config, SCREEN_W, SCREEN_H, 2, tiles, 0) == 0, "no capacity");
    check(tile_layout(config, 0, 0, 1, tiles, 8) == 0, "no screen");
    check(tile_layout(config, -100, -100, 1, tiles, 8) == 0, "a negative screen");

    printf("more windows than capacity places only what fits in the answer\n");
    struct tile_area two[2];
    const size_t placed = tile_layout(config, SCREEN_W, SCREEN_H, 6, two, 2);
    check(placed <= 2, "and never writes past the end of the array");
}

int main(void)
{
    test_the_four_layouts_that_were_drawn();
    test_no_two_tiles_ever_share_a_pixel();
    test_bars_are_reserved();
    test_a_tile_too_small_is_not_placed();
    test_the_master_split_moves_the_divider();
    test_invalid_requests_are_refused();

    if (failures > 0) {
        printf("\n%d tiling check(s) FAILED\n", failures);
        return 1;
    }
    printf("\ntiling layout checks passed\n");
    return 0;
}
