/* Where the visible windows go. Pure arithmetic, no drawing, no window objects.
 *
 * ME OS Default is tiling first. Normal windows do not overlap and do not
 * choose where they sit: opening one, closing one or focusing one recalculates
 * the whole layout, and an app is told the size it was given rather than asking
 * for a position. See M17 in docs/milestones.md.
 *
 * Kept separate from window.c on purpose. The layout is the part with the
 * arithmetic worth testing, and it can be tested without a window, a surface or
 * a framebuffer anywhere near it.
 */
#ifndef ME_TILE_H
#define ME_TILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One tile, in screen coordinates. The border is drawn inside this, so a window
 * given a tile owns every pixel of it. */
struct tile_area {
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
};

/* The look of the layout, in one place so a future settings screen has
 * something to write to rather than constants scattered through the drawing. */
struct tile_config {
    int64_t top_bar;        /* reserved along the top, never tiled over */
    int64_t bottom_bar;     /* reserved along the bottom, never tiled over */
    int64_t outer_gap;      /* between the reserved area and the tiles */
    int64_t inner_gap;      /* between one tile and the next */
    int64_t border;         /* accent border on the focused tile */
    int64_t min_width;      /* below this a tile is not placed at all */
    int64_t min_height;
    /* How much of the width the left column takes when there are two, as a
     * percentage. 50 is an even split. This is the one number tile resizing
     * moves, which is why it is here rather than being a literal in the
     * arithmetic. */
    int64_t master_percent;
};

struct tile_config tile_defaults(void);

/* The rectangle tiles are allowed to use: the screen minus the bars and the
 * outer gap. Empty when the bars leave nothing. */
struct tile_area tile_workspace(struct tile_config config,
                                int64_t screen_width, int64_t screen_height);

/* Places `count` windows and writes the areas into `out`.
 *
 * Returns how many were placed. A tile that cannot meet the minimum dimensions
 * is not placed at all rather than placed too small, so a caller that asks for
 * more windows than the screen can hold gets fewer areas back and knows it.
 *
 * The rule is two columns. The left column takes `master_percent` of the width
 * and holds `count / 2` windows, the right column holds the rest, and each
 * column splits its height evenly. One window fills the whole workspace, two
 * are side by side, three are one on the left and two on the right, four are a
 * two by two grid. It is deliberately one rule rather than a table of special
 * cases, because a table is where the case nobody drew goes wrong.
 */
size_t tile_layout(struct tile_config config,
                   int64_t screen_width, int64_t screen_height,
                   size_t count, struct tile_area *out, size_t capacity);

#endif /* ME_TILE_H */
