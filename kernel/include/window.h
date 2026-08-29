/* Bounded window objects for the first window-system milestone.
 *
 * The kernel has no allocator yet. Storage is therefore a fixed pool, but IDs
 * are deliberately independent of pool slots and every operation goes through
 * the manager. Replacing the pool with allocated objects later will not change
 * the public lifetime or z-order rules.
 */
#ifndef ME_WINDOW_H
#define ME_WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t WindowId;

#define WINDOW_ID_NONE ((WindowId)0)
#define WINDOW_MAX 8
#define WINDOW_TITLE_CAPACITY 32
#define WINDOW_MAX_DIMENSION 4096u

struct window_geometry {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};

struct window_spec {
    struct window_geometry geometry;
    const char *title;
};

struct window {
    WindowId id;
    struct window_geometry geometry;
    char title[WINDOW_TITLE_CAPACITY];
    bool focused;
    bool minimized;
    bool occupied;
};

struct window_manager {
    struct window slots[WINDOW_MAX];
    uint8_t z_slots[WINDOW_MAX];       /* bottom to top */
    size_t count;
    WindowId next_id;
};

bool window_geometry_valid(struct window_geometry geometry);
void window_manager_init(struct window_manager *manager);
size_t window_count(const struct window_manager *manager);

/* New windows are placed at the top of the deterministic z-order. */
bool window_create(struct window_manager *manager,
                   const struct window_spec *spec, WindowId *out_id);
bool window_destroy(struct window_manager *manager, WindowId id);

struct window *window_get(struct window_manager *manager, WindowId id);
const struct window *window_get_const(const struct window_manager *manager,
                                      WindowId id);
bool window_set_geometry(struct window_manager *manager, WindowId id,
                         struct window_geometry geometry);

/* Index zero is the bottom window. Raising preserves every other ordering. */
const struct window *window_at_z(const struct window_manager *manager,
                                 size_t index);
bool window_raise(struct window_manager *manager, WindowId id);

/* Returns the topmost non-minimized window containing the point. */
WindowId window_hit_test(const struct window_manager *manager,
                         int64_t x, int64_t y);

#endif /* ME_WINDOW_H */
