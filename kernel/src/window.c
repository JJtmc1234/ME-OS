#include "window.h"

#include <stddef.h>

bool window_geometry_valid(struct window_geometry geometry)
{
    return geometry.width > 0 && geometry.height > 0 &&
           geometry.width <= WINDOW_MAX_DIMENSION &&
           geometry.height <= WINDOW_MAX_DIMENSION;
}

void window_manager_init(struct window_manager *manager)
{
    if (manager == NULL) {
        return;
    }
    *manager = (struct window_manager){0};
    manager->next_id = 1;
}

size_t window_count(const struct window_manager *manager)
{
    return manager == NULL ? 0 : manager->count;
}

static bool title_fits(const char *title)
{
    if (title == NULL) {
        return false;
    }
    for (size_t i = 0; i < WINDOW_TITLE_CAPACITY; i++) {
        if (title[i] == '\0') {
            return true;
        }
    }
    return false;
}

static int slot_for_id(const struct window_manager *manager, WindowId id)
{
    if (manager == NULL || id == WINDOW_ID_NONE) {
        return -1;
    }
    for (size_t i = 0; i < WINDOW_MAX; i++) {
        if (manager->slots[i].occupied && manager->slots[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static bool take_id(struct window_manager *manager, WindowId *out)
{
    WindowId candidate = manager->next_id;
    if (candidate == WINDOW_ID_NONE) {
        candidate = 1;
    }

    /* At most WINDOW_MAX IDs are live, so one of the next WINDOW_MAX + 1
     * candidates must be free even after the counter wraps. */
    for (size_t tries = 0; tries <= WINDOW_MAX; tries++) {
        if (slot_for_id(manager, candidate) < 0) {
            *out = candidate;
            manager->next_id = candidate + 1u;
            if (manager->next_id == WINDOW_ID_NONE) {
                manager->next_id = 1;
            }
            return true;
        }
        candidate++;
        if (candidate == WINDOW_ID_NONE) {
            candidate = 1;
        }
    }
    return false;
}

bool window_create(struct window_manager *manager,
                   const struct window_spec *spec, WindowId *out_id)
{
    if (out_id != NULL) {
        *out_id = WINDOW_ID_NONE;
    }
    if (manager == NULL || spec == NULL || out_id == NULL ||
        manager->count >= WINDOW_MAX ||
        !window_geometry_valid(spec->geometry) || !title_fits(spec->title)) {
        return false;
    }

    size_t slot = WINDOW_MAX;
    for (size_t i = 0; i < WINDOW_MAX; i++) {
        if (!manager->slots[i].occupied) {
            slot = i;
            break;
        }
    }
    if (slot == WINDOW_MAX) {
        return false;
    }

    WindowId id;
    if (!take_id(manager, &id)) {
        return false;
    }

    struct window *window = &manager->slots[slot];
    *window = (struct window){0};
    window->id = id;
    window->geometry = spec->geometry;
    window->occupied = true;
    for (size_t i = 0; i < WINDOW_TITLE_CAPACITY; i++) {
        window->title[i] = spec->title[i];
        if (spec->title[i] == '\0') {
            break;
        }
    }

    manager->z_slots[manager->count] = (uint8_t)slot;
    manager->count++;
    *out_id = id;
    return true;
}

struct window *window_get(struct window_manager *manager, WindowId id)
{
    const int slot = slot_for_id(manager, id);
    return slot < 0 ? NULL : &manager->slots[(size_t)slot];
}

const struct window *window_get_const(const struct window_manager *manager,
                                      WindowId id)
{
    const int slot = slot_for_id(manager, id);
    return slot < 0 ? NULL : &manager->slots[(size_t)slot];
}

bool window_destroy(struct window_manager *manager, WindowId id)
{
    const int slot = slot_for_id(manager, id);
    if (slot < 0) {
        return false;
    }

    size_t z = 0;
    while (z < manager->count && manager->z_slots[z] != (uint8_t)slot) {
        z++;
    }
    if (z == manager->count) {
        return false;
    }
    for (size_t i = z + 1; i < manager->count; i++) {
        manager->z_slots[i - 1] = manager->z_slots[i];
    }
    manager->count--;
    manager->z_slots[manager->count] = 0;
    manager->slots[(size_t)slot] = (struct window){0};
    return true;
}

bool window_set_geometry(struct window_manager *manager, WindowId id,
                         struct window_geometry geometry)
{
    struct window *window = window_get(manager, id);
    if (window == NULL || !window_geometry_valid(geometry) ||
        (window->surface != NULL &&
         (window->surface->width != geometry.width ||
          window->surface->height != geometry.height))) {
        return false;
    }
    window->geometry = geometry;
    return true;
}

bool window_attach_surface(struct window_manager *manager, WindowId id,
                           struct surface *surface)
{
    struct window *window = window_get(manager, id);
    if (window == NULL) {
        return false;
    }
    if (surface != NULL &&
        (!surface_valid(surface) || surface->width != window->geometry.width ||
         surface->height != window->geometry.height)) {
        return false;
    }
    window->surface = surface;
    return true;
}

const struct window *window_at_z(const struct window_manager *manager,
                                 size_t index)
{
    if (manager == NULL || index >= manager->count) {
        return NULL;
    }
    const size_t slot = manager->z_slots[index];
    return slot >= WINDOW_MAX ? NULL : &manager->slots[slot];
}

bool window_raise(struct window_manager *manager, WindowId id)
{
    const int slot = slot_for_id(manager, id);
    if (slot < 0) {
        return false;
    }

    size_t z = 0;
    while (z < manager->count && manager->z_slots[z] != (uint8_t)slot) {
        z++;
    }
    if (z == manager->count) {
        return false;
    }
    if (z + 1 == manager->count) {
        return true;
    }
    for (size_t i = z + 1; i < manager->count; i++) {
        manager->z_slots[i - 1] = manager->z_slots[i];
    }
    manager->z_slots[manager->count - 1] = (uint8_t)slot;
    return true;
}

static bool contains(const struct window_geometry *geometry, int64_t x, int64_t y)
{
    if (x < geometry->x || y < geometry->y) {
        return false;
    }
    const uint64_t across = (uint64_t)x - (uint64_t)(int64_t)geometry->x;
    const uint64_t down = (uint64_t)y - (uint64_t)(int64_t)geometry->y;
    return across < geometry->width && down < geometry->height;
}

WindowId window_hit_test(const struct window_manager *manager,
                         int64_t x, int64_t y)
{
    if (manager == NULL) {
        return WINDOW_ID_NONE;
    }
    for (size_t z = manager->count; z > 0; z--) {
        const struct window *window = window_at_z(manager, z - 1);
        if (window != NULL && !window->minimized &&
            contains(&window->geometry, x, y)) {
            return window->id;
        }
    }
    return WINDOW_ID_NONE;
}
