#include "tile.h"

/* The ME OS Default look. Small gaps, a thin accent border, and bars that are
 * tall enough to read and no taller. Vertical space belongs to the work. */
#define TILE_TOP_BAR        28
#define TILE_BOTTOM_BAR     34
#define TILE_OUTER_GAP      10
#define TILE_INNER_GAP      8
#define TILE_BORDER         2
#define TILE_MIN_WIDTH      120
#define TILE_MIN_HEIGHT     80
#define TILE_MASTER_PERCENT 50

struct tile_config tile_defaults(void)
{
    const struct tile_config config = {
        .top_bar = TILE_TOP_BAR,
        .bottom_bar = TILE_BOTTOM_BAR,
        .outer_gap = TILE_OUTER_GAP,
        .inner_gap = TILE_INNER_GAP,
        .border = TILE_BORDER,
        .min_width = TILE_MIN_WIDTH,
        .min_height = TILE_MIN_HEIGHT,
        .master_percent = TILE_MASTER_PERCENT,
    };
    return config;
}

static struct tile_area area_none(void)
{
    const struct tile_area none = { 0, 0, 0, 0 };
    return none;
}

static struct tile_area area_make(int64_t x, int64_t y, int64_t width, int64_t height)
{
    if (width <= 0 || height <= 0) {
        return area_none();
    }
    const struct tile_area area = { x, y, width, height };
    return area;
}

struct tile_area tile_workspace(struct tile_config config,
                                int64_t screen_width, int64_t screen_height)
{
    /* A negative reservation would hand back more screen than there is, so each
     * one is floored rather than trusted. A settings file is a thing a person
     * types into. */
    if (config.top_bar < 0) config.top_bar = 0;
    if (config.bottom_bar < 0) config.bottom_bar = 0;
    if (config.outer_gap < 0) config.outer_gap = 0;

    const int64_t x = config.outer_gap;
    const int64_t y = config.top_bar + config.outer_gap;
    const int64_t width = screen_width - 2 * config.outer_gap;
    const int64_t height =
        screen_height - config.top_bar - config.bottom_bar - 2 * config.outer_gap;
    return area_make(x, y, width, height);
}

/* Splits `area` into `count` rows separated by the inner gap, writing them into
 * `out`. Returns how many rows met the minimum height. */
static size_t split_rows(struct tile_config config, struct tile_area area,
                         size_t count, struct tile_area *out)
{
    if (count == 0) {
        return 0;
    }
    const int64_t gaps = config.inner_gap * (int64_t)(count - 1);
    const int64_t each = (area.height - gaps) / (int64_t)count;
    if (each < config.min_height || area.width < config.min_width) {
        return 0;
    }

    for (size_t i = 0; i < count; i++) {
        /* The last row takes the remainder, so rounding never leaves a strip of
         * desktop showing along the bottom of the column. */
        const int64_t top = area.y + (int64_t)i * (each + config.inner_gap);
        const int64_t height =
            (i + 1 == count) ? area.y + area.height - top : each;
        out[i] = area_make(area.x, top, area.width, height);
    }
    return count;
}

size_t tile_layout(struct tile_config config,
                   int64_t screen_width, int64_t screen_height,
                   size_t count, struct tile_area *out, size_t capacity)
{
    if (out == NULL || count == 0 || capacity == 0) {
        return 0;
    }
    if (count > capacity) {
        count = capacity;
    }
    if (config.inner_gap < 0) config.inner_gap = 0;
    if (config.min_width < 0) config.min_width = 0;
    if (config.min_height < 0) config.min_height = 0;

    const struct tile_area workspace =
        tile_workspace(config, screen_width, screen_height);
    if (workspace.width <= 0 || workspace.height <= 0) {
        return 0;
    }

    /* One window is not a column of one beside an empty column. It is the whole
     * workspace, with no divider and no gap taken out for a neighbour that is
     * not there. */
    if (count == 1) {
        return split_rows(config, workspace, 1, out);
    }

    const size_t left_count = count / 2;
    const size_t right_count = count - left_count;

    int64_t percent = config.master_percent;
    /* Clamped rather than refused. A percentage outside this range is a tile
     * dragged past the edge, and stopping at the edge is what a divider does. */
    if (percent < 10) percent = 10;
    if (percent > 90) percent = 90;

    const int64_t usable = workspace.width - config.inner_gap;
    const int64_t left_width = (usable * percent) / 100;
    const int64_t right_width = usable - left_width;

    const struct tile_area left_column =
        area_make(workspace.x, workspace.y, left_width, workspace.height);
    const struct tile_area right_column =
        area_make(workspace.x + left_width + config.inner_gap, workspace.y,
                  right_width, workspace.height);

    const size_t placed_left = split_rows(config, left_column, left_count, out);
    if (placed_left != left_count) {
        return 0;
    }
    const size_t placed_right =
        split_rows(config, right_column, right_count, out + left_count);
    if (placed_right != right_count) {
        return 0;
    }
    return count;
}
