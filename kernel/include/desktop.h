/* The ME OS Default environment: which windows exist, where the layout puts
 * them, and what their frames look like.
 *
 * This is the piece that makes ME OS tiling first. An app never chooses where it
 * sits. It is given a tile, told the size of the area inside the frame, and
 * asked to paint that. Opening, hiding or closing a window recalculates every
 * tile, so no two visible windows can overlap by construction rather than by a
 * z-order that happens to keep them apart.
 *
 * Drawing the tiles onto the screen is not here. That is the compositor's job
 * and it has not changed. See M18 in docs/milestones.md.
 */
#ifndef ME_DESKTOP_H
#define ME_DESKTOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "region.h"
#include "shell.h"
#include "surface.h"
#include "tile.h"
#include "window.h"

#define DESKTOP_MAX_APPS 5
/* How many sets of tiles there are. Four, because that is as many as anybody
 * keeps in their head, and because the keyboard has to reach each one with a
 * single key. */
#define DESKTOP_WORKSPACES 4

struct desktop_app {
    WindowId id;
    const char *title;
    /* The whole tile. This is what the compositor blits, so the frame and the
     * content inside it reach the screen together. */
    struct surface frame;
    /* A view onto the part of the frame inside the border and below the title.
     * It shares the frame's pixels, so an app drawing into it draws into the
     * tile with no second copy and no second blit. */
    struct surface client;
    /* Out of the layout and still on the taskbar. A hidden window takes no tile
     * space, which is the whole reason to hide one. */
    bool hidden;
    /* Which set of tiles it belongs to, from one. A window on another workspace
     * is as absent from this one as a hidden window is, and it is a different
     * kind of absent: hiding is about this screen, a workspace is about which
     * screen you are looking at. */
    int64_t workspace;
};

struct desktop {
    struct window_manager *windows;
    /* One store for every tile, handed out in layout order.
     *
     * Shared rather than one per app, and sized for the workspace rather than
     * for the screen, because tiles never overlap: whatever the layout does, the
     * visible tiles together cover at most the workspace. A private store per
     * app, each big enough to be the only window, would be several times the
     * memory for a case that cannot happen, and at boot the zeroing of it was
     * long enough to see on the screen. */
    uint32_t *arena;
    size_t arena_pixels;
    struct tile_config layout;
    struct theme theme;
    struct desktop_app apps[DESKTOP_MAX_APPS];
    size_t app_count;
    int64_t screen_width;
    int64_t screen_height;
    int64_t workspace;
};

bool desktop_init(struct desktop *desktop, struct window_manager *windows,
                  int64_t screen_width, int64_t screen_height,
                  uint32_t *arena, size_t arena_pixels,
                  uint32_t (*rgb)(uint8_t, uint8_t, uint8_t));

/* Adds an app. It gets its pixels from the shared arena at every layout.
 * Returns DESKTOP_MAX_APPS on failure. */
size_t desktop_add(struct desktop *desktop, const char *title);

size_t desktop_index_of(const struct desktop *desktop, WindowId id);
struct desktop_app *desktop_app_at(struct desktop *desktop, size_t index);
size_t desktop_visible_count(const struct desktop *desktop);

/* Whether this app is on the screen right now: not hidden, and on the workspace
 * being looked at. The one place that answer is worked out. */
bool desktop_on_screen(const struct desktop *desktop, size_t index);

/* Changes which set of tiles is on screen. The caller lays out afterwards, and
 * the layout is what moves focus onto something visible. False when the number
 * names no workspace or is the one already being looked at. */
bool desktop_switch_workspace(struct desktop *desktop, int64_t to);

/* Sends a window to another workspace, leaving you where you are. Moving a
 * window away and following it are two different wishes, and this is the one
 * that lets a workspace be cleared. */
bool desktop_move_to_workspace(struct desktop *desktop, WindowId id, int64_t to);

/* Whether any window lives on that workspace, so the bar can show which of them
 * have something on them. */
bool desktop_workspace_occupied(const struct desktop *desktop, int64_t which);

/* Recomputes every tile, resizes every visible window's surface and rebuilds
 * every client view. False means the screen cannot hold what is visible, and
 * nothing has been changed.
 *
 * Focus settles here too, onto something on the screen, because this is the one
 * place that knows what is on it afterwards. */
bool desktop_relayout(struct desktop *desktop);

/* Paints every visible frame: ground, title strip, buttons, border. The client
 * area is cleared along with the rest, so an app repaints after this. */
void desktop_paint_frames(struct desktop *desktop);

/* Both bars, straight onto the target. Called after composition rather than
 * before it, because the compositor clears what it composes and would wipe
 * them. */
void desktop_draw_bars(struct desktop *desktop, struct surface *target,
                       const char *clock, uint64_t uptime_seconds);

struct region desktop_top_bar_region(const struct desktop *desktop);
struct region desktop_taskbar_region(const struct desktop *desktop);

/* A rectangle in an app's own client coordinates, said in screen coordinates,
 * so a dirty region computed by an app can be presented by the compositor. */
struct region desktop_client_region(const struct desktop *desktop, WindowId id,
                                    int64_t x, int64_t y,
                                    int64_t width, int64_t height);

/* Moves focus to the next or previous visible window. `step` is +1 or -1. */
bool desktop_focus_step(struct desktop *desktop, int step);

/* Takes a window out of the layout or puts it back. The caller relays out. */
bool desktop_set_hidden(struct desktop *desktop, WindowId id, bool hidden);

/* Which taskbar button a screen point falls on, or DESKTOP_MAX_APPS for none. */
size_t desktop_taskbar_hit(const struct desktop *desktop, int64_t x, int64_t y);

#endif /* ME_DESKTOP_H */
