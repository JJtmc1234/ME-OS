#include "desktop.h"

bool desktop_init(struct desktop *desktop, struct window_manager *windows,
                  int64_t screen_width, int64_t screen_height,
                  uint32_t *arena, size_t arena_pixels,
                  uint32_t (*rgb)(uint8_t, uint8_t, uint8_t))
{
    if (desktop == NULL || windows == NULL || rgb == NULL || arena == NULL ||
        arena_pixels == 0 || screen_width <= 0 || screen_height <= 0) {
        return false;
    }
    *desktop = (struct desktop){0};
    desktop->windows = windows;
    desktop->arena = arena;
    desktop->arena_pixels = arena_pixels;
    desktop->layout = tile_defaults();
    desktop->theme = theme_default(rgb);
    desktop->screen_width = screen_width;
    desktop->screen_height = screen_height;
    desktop->workspace = 1;
    return true;
}

size_t desktop_add(struct desktop *desktop, const char *title)
{
    if (desktop == NULL || desktop->app_count >= DESKTOP_MAX_APPS) {
        return DESKTOP_MAX_APPS;
    }

    /* Created with a placeholder geometry the layout replaces immediately. The
     * window manager needs a valid size to exist at all, and an app is never
     * told about this one: the first relayout happens before anything paints. */
    const struct window_spec spec = {
        .geometry = { .x = 0, .y = 0, .width = 64, .height = 64 },
        .title = title,
    };
    WindowId id = WINDOW_ID_NONE;
    if (!window_create(desktop->windows, &spec, &id)) {
        return DESKTOP_MAX_APPS;
    }

    const size_t index = desktop->app_count++;
    struct desktop_app *app = &desktop->apps[index];
    app->id = id;
    app->title = title;
    app->hidden = false;
    return index;
}

size_t desktop_index_of(const struct desktop *desktop, WindowId id)
{
    if (desktop == NULL) {
        return DESKTOP_MAX_APPS;
    }
    for (size_t i = 0; i < desktop->app_count; i++) {
        if (desktop->apps[i].id == id) {
            return i;
        }
    }
    return DESKTOP_MAX_APPS;
}

struct desktop_app *desktop_app_at(struct desktop *desktop, size_t index)
{
    if (desktop == NULL || index >= desktop->app_count) {
        return NULL;
    }
    return &desktop->apps[index];
}

size_t desktop_visible_count(const struct desktop *desktop)
{
    if (desktop == NULL) {
        return 0;
    }
    size_t visible = 0;
    for (size_t i = 0; i < desktop->app_count; i++) {
        if (!desktop->apps[i].hidden) {
            visible++;
        }
    }
    return visible;
}

bool desktop_relayout(struct desktop *desktop)
{
    if (desktop == NULL || desktop->windows == NULL) {
        return false;
    }

    size_t order[DESKTOP_MAX_APPS];
    size_t visible = 0;
    for (size_t i = 0; i < desktop->app_count; i++) {
        if (!desktop->apps[i].hidden) {
            order[visible++] = i;
        }
    }

    struct tile_area tiles[DESKTOP_MAX_APPS];
    if (visible > 0) {
        const size_t placed =
            tile_layout(desktop->layout, desktop->screen_width,
                        desktop->screen_height, visible, tiles, DESKTOP_MAX_APPS);
        /* All or nothing. Laying out some of the windows would leave the rest
         * where they were, which is where the new ones are about to go, and
         * overlapping tiles is the one thing this whole module exists to
         * prevent. */
        if (placed != visible) {
            return false;
        }
    }

    /* Detached before anything moves. The window manager refuses a surface whose
     * size disagrees with its window, so the geometry and the surface cannot
     * both change while they are attached to each other. */
    for (size_t i = 0; i < desktop->app_count; i++) {
        struct window *window = window_get(desktop->windows, desktop->apps[i].id);
        if (window == NULL) {
            return false;
        }
        window_attach_surface(desktop->windows, desktop->apps[i].id, NULL);
        window->minimized = desktop->apps[i].hidden;
    }

    /* Handed out in layout order, so a tile always gets a slice that starts
     * where the previous one ended. The tiles do not overlap, so their areas
     * add up to no more than the workspace and the arena cannot run out unless
     * the caller sized it smaller than that. */
    size_t taken = 0;
    for (size_t k = 0; k < visible; k++) {
        struct desktop_app *app = &desktop->apps[order[k]];
        const struct tile_area tile = tiles[k];
        const struct window_geometry geometry = {
            .x = (int32_t)tile.x,
            .y = (int32_t)tile.y,
            .width = (uint32_t)tile.width,
            .height = (uint32_t)tile.height,
        };
        if (!window_set_geometry(desktop->windows, app->id, geometry)) {
            return false;
        }
        const size_t needed = (size_t)tile.width * (size_t)tile.height;
        if (needed > desktop->arena_pixels - taken) {
            return false;
        }
        if (!surface_init(&app->frame, desktop->arena + taken,
                          desktop->arena_pixels - taken,
                          geometry.width, geometry.height)) {
            return false;
        }
        taken += needed;
        const struct tile_area client =
            shell_client_area(tile.width, tile.height,
                              desktop->layout.border, SHELL_TITLE_HEIGHT);
        if (!surface_view(&app->frame, client.x, client.y,
                          (uint32_t)client.width, (uint32_t)client.height,
                          &app->client)) {
            return false;
        }
        if (!window_attach_surface(desktop->windows, app->id, &app->frame)) {
            return false;
        }
    }
    return true;
}

void desktop_paint_frames(struct desktop *desktop)
{
    if (desktop == NULL) {
        return;
    }
    const WindowId focused = window_focused(desktop->windows);
    for (size_t i = 0; i < desktop->app_count; i++) {
        struct desktop_app *app = &desktop->apps[i];
        if (app->hidden) {
            continue;
        }
        shell_frame(&app->frame, &desktop->theme, app->title,
                    app->id == focused, desktop->layout.border);
    }
}

struct region desktop_top_bar_region(const struct desktop *desktop)
{
    if (desktop == NULL) {
        return region_none();
    }
    return region_make(0, 0, desktop->screen_width, desktop->layout.top_bar);
}

struct region desktop_taskbar_region(const struct desktop *desktop)
{
    if (desktop == NULL) {
        return region_none();
    }
    return region_make(0, desktop->screen_height - desktop->layout.bottom_bar,
                       desktop->screen_width, desktop->layout.bottom_bar);
}

void desktop_draw_bars(struct desktop *desktop, struct surface *target,
                       const char *clock, uint64_t uptime_seconds)
{
    if (desktop == NULL || target == NULL) {
        return;
    }
    const WindowId focused = window_focused(desktop->windows);
    const size_t index = desktop_index_of(desktop, focused);
    const char *name = index < desktop->app_count ? desktop->apps[index].title : "";

    shell_top_bar(target, &desktop->theme, desktop->screen_width,
                  desktop->layout.top_bar, desktop->workspace, name,
                  clock, uptime_seconds);

    struct shell_task tasks[DESKTOP_MAX_APPS];
    for (size_t i = 0; i < desktop->app_count; i++) {
        tasks[i].name = desktop->apps[i].title;
        tasks[i].focused = desktop->apps[i].id == focused;
        tasks[i].hidden = desktop->apps[i].hidden;
    }
    shell_taskbar(target, &desktop->theme, desktop->screen_width,
                  desktop->screen_height, desktop->layout.bottom_bar,
                  tasks, desktop->app_count);
}

struct region desktop_client_region(const struct desktop *desktop, WindowId id,
                                    int64_t x, int64_t y,
                                    int64_t width, int64_t height)
{
    if (desktop == NULL) {
        return region_none();
    }
    const size_t index = desktop_index_of(desktop, id);
    if (index >= desktop->app_count || desktop->apps[index].hidden) {
        return region_none();
    }
    const struct window *window = window_get_const(desktop->windows, id);
    if (window == NULL) {
        return region_none();
    }
    const struct desktop_app *app = &desktop->apps[index];
    /* Trimmed to the client first, so an app asking about a rectangle larger
     * than the area it was given cannot present over its own frame. */
    const struct region local =
        region_clip(region_make(x, y, width, height),
                    (int64_t)app->client.width, (int64_t)app->client.height);
    if (region_empty(&local)) {
        return region_none();
    }
    const struct tile_area client =
        shell_client_area(window->geometry.width, window->geometry.height,
                          desktop->layout.border, SHELL_TITLE_HEIGHT);
    return region_make(local.x + window->geometry.x + client.x,
                       local.y + window->geometry.y + client.y,
                       local.width, local.height);
}

bool desktop_focus_step(struct desktop *desktop, int step)
{
    if (desktop == NULL || step == 0) {
        return false;
    }
    const size_t visible = desktop_visible_count(desktop);
    if (visible == 0) {
        return false;
    }

    const size_t here = desktop_index_of(desktop, window_focused(desktop->windows));
    size_t at = here < desktop->app_count ? here : 0;

    /* At most one pass round the table, so a run of hidden windows cannot spin
     * forever and a table with one visible window lands back on itself. */
    for (size_t tried = 0; tried < desktop->app_count; tried++) {
        if (step > 0) {
            at = (at + 1) % desktop->app_count;
        } else {
            at = (at + desktop->app_count - 1) % desktop->app_count;
        }
        if (!desktop->apps[at].hidden) {
            /* Without raising. Tiles do not overlap, so the z-order says nothing
             * about what a person can see, and reordering it on every focus
             * change would make the compositor redraw for no visible reason. */
            return window_focus(desktop->windows, desktop->apps[at].id, false);
        }
    }
    return false;
}

bool desktop_set_hidden(struct desktop *desktop, WindowId id, bool hidden)
{
    if (desktop == NULL) {
        return false;
    }
    const size_t index = desktop_index_of(desktop, id);
    if (index >= desktop->app_count) {
        return false;
    }
    /* The last visible window does not get to hide. A desktop with nothing on
     * it and no way to get anything back is not a state worth being able to
     * reach with one key. */
    if (hidden && desktop_visible_count(desktop) <= 1) {
        return false;
    }
    if (desktop->apps[index].hidden == hidden) {
        return false;
    }
    desktop->apps[index].hidden = hidden;
    if (hidden && window_focused(desktop->windows) == id) {
        desktop_focus_step(desktop, 1);
    }
    return true;
}

size_t desktop_taskbar_hit(const struct desktop *desktop, int64_t x, int64_t y)
{
    if (desktop == NULL) {
        return DESKTOP_MAX_APPS;
    }
    const size_t hit =
        shell_task_at(desktop->screen_width, desktop->screen_height,
                      desktop->layout.bottom_bar, desktop->app_count, x, y);
    return hit < desktop->app_count ? hit : DESKTOP_MAX_APPS;
}
