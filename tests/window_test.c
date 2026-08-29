/* Host tests for the M13 window object model. No framebuffer or device I/O. */
#include <stdio.h>

#include "window.h"

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

static struct window_spec spec(const char *title, int32_t x, int32_t y,
                               uint32_t width, uint32_t height)
{
    return (struct window_spec){
        .geometry = { .x = x, .y = y, .width = width, .height = height },
        .title = title,
    };
}

static bool title_is(const struct window *window, const char *expected)
{
    if (window == NULL) {
        return false;
    }
    for (size_t i = 0; i < WINDOW_TITLE_CAPACITY; i++) {
        if (window->title[i] != expected[i]) {
            return false;
        }
        if (expected[i] == '\0') {
            return true;
        }
    }
    return false;
}

static void test_create_and_retrieve(void)
{
    printf("windows have stable IDs, geometry and titles\n");
    struct window_manager manager;
    window_manager_init(&manager);
    check(window_count(&manager) == 0, "a new manager is empty");
    check(window_count(NULL) == 0, "no manager has no windows");
    window_manager_init(NULL);

    const struct window_spec demo = spec("Demo", -40, 25, 640, 480);
    WindowId demo_id = 99;
    check(window_create(&manager, &demo, &demo_id), "create a partially off-screen window");
    check(demo_id != WINDOW_ID_NONE, "the ID is valid");
    check(window_count(&manager) == 1, "the count grows");

    const struct window *window = window_get_const(&manager, demo_id);
    check(window != NULL && window->id == demo_id, "retrieve it by stable ID");
    check(window != NULL && window->geometry.x == -40 && window->geometry.y == 25 &&
          window->geometry.width == 640 && window->geometry.height == 480,
          "geometry is retained exactly");
    check(title_is(window, "Demo"), "the title is retained");
    check(window != NULL && !window->focused && !window->minimized,
          "future state begins inactive");

    const struct window_spec system = spec("System", 100, 80, 300, 200);
    WindowId system_id = 99;
    check(window_create(&manager, &system, &system_id), "create another window");
    check(system_id != demo_id, "living windows never share an ID");
    check(window_get(&manager, system_id) != NULL, "mutable retrieval uses the same ID");
    check(window_get(&manager, WINDOW_ID_NONE) == NULL, "zero is never a window ID");
    check(window_get(&manager, 123456u) == NULL, "an unknown ID is not found");
}

static void test_validation_and_capacity(void)
{
    printf("invalid bounds and titles are refused without consuming a slot\n");
    struct window_manager manager;
    window_manager_init(&manager);
    WindowId id = 77;

    struct window_spec bad = spec("bad", 0, 0, 0, 10);
    check(!window_create(&manager, &bad, &id) && id == WINDOW_ID_NONE,
          "zero width is refused and clears the output ID");
    bad = spec("bad", 0, 0, 10, 0);
    check(!window_create(&manager, &bad, &id), "zero height is refused");
    bad = spec("bad", 0, 0, WINDOW_MAX_DIMENSION + 1u, 10);
    check(!window_create(&manager, &bad, &id), "an over-wide object is refused");
    bad = spec("bad", 0, 0, 10, WINDOW_MAX_DIMENSION + 1u);
    check(!window_create(&manager, &bad, &id), "an over-tall object is refused");
    bad = spec(NULL, 0, 0, 10, 10);
    check(!window_create(&manager, &bad, &id), "a null title is refused");

    static const char too_long[WINDOW_TITLE_CAPACITY + 1] =
        "12345678901234567890123456789012";
    bad = spec(too_long, 0, 0, 10, 10);
    check(!window_create(&manager, &bad, &id), "a title with no terminating room is refused");
    check(window_count(&manager) == 0, "invalid creates changed nothing");
    check(!window_create(NULL, &bad, &id), "no manager is safe");
    check(!window_create(&manager, NULL, &id), "no specification is safe");
    bad = spec("ok", 0, 0, 10, 10);
    check(!window_create(&manager, &bad, NULL), "an ID output is required");

    printf("the temporary pool has an explicit safe limit\n");
    WindowId ids[WINDOW_MAX];
    for (size_t i = 0; i < WINDOW_MAX; i++) {
        struct window_spec item = spec("pool", (int32_t)i, 0, 10, 10);
        check(window_create(&manager, &item, &ids[i]), "create while a pool slot remains");
    }
    check(window_count(&manager) == WINDOW_MAX, "every pool slot is accounted for");
    id = 77;
    check(!window_create(&manager, &bad, &id) && id == WINDOW_ID_NONE,
          "one window too many is refused");
}

static void test_lifetime_and_geometry(void)
{
    printf("destroying invalidates only the destroyed ID\n");
    struct window_manager manager;
    window_manager_init(&manager);
    WindowId first, second, third;
    struct window_spec a = spec("A", 0, 0, 100, 100);
    struct window_spec b = spec("B", 20, 20, 100, 100);
    struct window_spec c = spec("C", 40, 40, 100, 100);
    window_create(&manager, &a, &first);
    window_create(&manager, &b, &second);
    window_create(&manager, &c, &third);

    check(window_destroy(&manager, second), "destroy a window");
    check(window_get(&manager, second) == NULL, "its ID is immediately invalid");
    check(window_get(&manager, first) != NULL && window_get(&manager, third) != NULL,
          "other stable IDs still retrieve their windows");
    check(window_count(&manager) == 2, "the count shrinks");
    check(!window_destroy(&manager, second), "destroying it twice is refused");
    check(!window_destroy(&manager, WINDOW_ID_NONE), "destroying ID zero is refused");

    WindowId replacement;
    struct window_spec d = spec("D", 60, 60, 100, 100);
    check(window_create(&manager, &d, &replacement), "a freed slot can be reused");
    check(replacement != second, "slot reuse does not immediately reuse its stable ID");

    const struct window_geometry moved = { .x = -100, .y = -50, .width = 320, .height = 240 };
    check(window_set_geometry(&manager, first, moved), "geometry can be changed by ID");
    const struct window *window = window_get_const(&manager, first);
    check(window != NULL && window->geometry.x == -100 && window->geometry.height == 240,
          "the changed geometry is visible through retrieval");
    const struct window_geometry invalid = { .x = 1, .y = 2, .width = 0, .height = 20 };
    check(!window_set_geometry(&manager, first, invalid), "invalid new geometry is refused");
    check(window != NULL && window->geometry.width == 320, "refusal leaves geometry unchanged");
    check(!window_set_geometry(&manager, second, moved), "a destroyed ID cannot be moved");
}

static void test_z_order_and_hit_testing(void)
{
    printf("z-order is deterministic and hit testing walks it top-down\n");
    struct window_manager manager;
    window_manager_init(&manager);
    WindowId a_id, b_id, c_id;
    struct window_spec a = spec("A", 0, 0, 100, 100);
    struct window_spec b = spec("B", 50, 50, 100, 100);
    struct window_spec c = spec("C", 300, 300, 50, 50);
    window_create(&manager, &a, &a_id);
    window_create(&manager, &b, &b_id);
    window_create(&manager, &c, &c_id);

    check(window_at_z(&manager, 0)->id == a_id, "first created is at the bottom");
    check(window_at_z(&manager, 1)->id == b_id, "second is above it");
    check(window_at_z(&manager, 2)->id == c_id, "new windows start on top");
    check(window_at_z(&manager, 3) == NULL && window_at_z(NULL, 0) == NULL,
          "out-of-range z access is safe");
    check(window_hit_test(&manager, 75, 75) == b_id,
          "the top overlapping window receives the hit");
    check(window_hit_test(&manager, 10, 10) == a_id, "a point in only one finds it");
    check(window_hit_test(&manager, 200, 200) == WINDOW_ID_NONE,
          "the background has no window");

    check(window_raise(&manager, a_id), "raise the bottom window");
    check(window_at_z(&manager, 0)->id == b_id &&
          window_at_z(&manager, 1)->id == c_id &&
          window_at_z(&manager, 2)->id == a_id,
          "raising preserves every other relative order");
    check(window_hit_test(&manager, 75, 75) == a_id, "the raised overlap is now targeted");
    check(window_raise(&manager, a_id), "raising the top window is a successful no-op");
    check(!window_raise(&manager, 123456u), "an unknown window cannot be raised");

    struct window *top = window_get(&manager, a_id);
    top->minimized = true;
    check(window_hit_test(&manager, 75, 75) == b_id, "minimized windows are skipped");
    top->minimized = false;
    check(window_hit_test(NULL, 0, 0) == WINDOW_ID_NONE, "no manager has no hit");
    check(window_hit_test(&manager, 100, 10) == WINDOW_ID_NONE,
          "the first pixel beyond a right edge is outside");

    check(window_destroy(&manager, c_id), "destroy a middle z-order entry");
    check(window_at_z(&manager, 0)->id == b_id && window_at_z(&manager, 1)->id == a_id,
          "destroying closes the gap without reordering survivors");
}

static void test_id_wrap(void)
{
    printf("the ID counter skips zero and living IDs when it wraps\n");
    struct window_manager manager;
    window_manager_init(&manager);
    struct window_spec item = spec("wrap", 0, 0, 10, 10);
    WindowId ordinary, last, after;
    window_create(&manager, &item, &ordinary);   /* occupies ID 1 */
    manager.next_id = UINT32_MAX;
    check(window_create(&manager, &item, &last) && last == UINT32_MAX,
          "the largest ID is usable");
    check(window_create(&manager, &item, &after) && after == 2,
          "wrap skips zero and the still-living ID 1");
}

int main(void)
{
    test_create_and_retrieve();
    test_validation_and_capacity();
    test_lifetime_and_geometry();
    test_z_order_and_hit_testing();
    test_id_wrap();

    if (failures > 0) {
        printf("\n%d window object check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nwindow object checks passed\n");
    return 0;
}
