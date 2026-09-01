/* ME OS kernel entry.
 *
 * M1: boot over UEFI and draw a fixed message.
 * M2: read the keyboard and show the last key pressed, without disturbing
 *     the M1 message.
 * M3: draw one static rectangle, without disturbing either.
 * M4: show a mouse cursor that moves, without disturbing any of them.
 * M5: move the rectangle across the screen over time, still without
 *     disturbing any of them.
 * M6: work out whole number sums typed on the keyboard, and show the result,
 *     still without disturbing any of them.
 * M7: work out one conditional, IF a > b THEN x ELSE y, on the same line.
 * M8: remember values under names, and use them in sums and conditionals.
 * M12: turn a triangle about its own centre, using floating point.
 * M9: steer the rectangle with the arrow keys.
 * M10: wrap the steered rectangle at each edge of its safe corridor.
 * M11: pick the rectangle up with the left mouse button and drag it.
 * M13: create stable window objects in deterministic z-order.
 * M14: draw into per-window surfaces and composite them over a desktop.
 * M15: route keyboard, mouse and focus through bounded per-window queues.
 *
 * Demo owns every earlier graphic in local coordinates. The compositor alone
 * presents window surfaces and the cursor to the framebuffer. Device state is
 * translated to window events before Demo sees it. There is no console or
 * scrolling yet.
 */
#include <stddef.h>
#include <stdint.h>

#include "calc.h"
#include "compositor.h"
#include "cursor.h"
#include "fb.h"
#include "fpu.h"
#include "geometry.h"
#include "font.h"
#include "kbd.h"
#include "limine.h"
#include "log.h"
#include "mouse.h"
#include "pointer.h"
#include "rect.h"
#include "region.h"
#include "surface.h"
#include "timer.h"
#include "vars.h"
#include "window.h"

#define M1_MESSAGE  "IF YOU SEE THIS IT WORKED"
#define M2_PROMPT   "PRESS A KEY"
#define M2_PREFIX   "LAST KEY "
/* M6: the sum line sits above the message, in the empty half of the screen. */
#define M6_LINE_GAP 2

/* M3: one static rectangle. Its size is a fraction of the screen so it stays
 * clearly visible at any resolution. tests/check_boot.py mirrors these two
 * numbers, so change them in both places or the check will fail. */
#define M3_RECT_WIDTH_DIVISOR  4
#define M3_RECT_HEIGHT_DIVISOR 14

/* M5: how fast the rectangle crosses the screen, in pixels per second. Slow
 * enough to watch, fast enough that a two second gap between screenshots is
 * obviously different. */
#define M5_RECT_SPEED 60

/* M9: how far one arrow key press moves the rectangle, and how much room to
 * leave between it and the things it must not paint over. Arrows were chosen
 * rather than letters because since M8 every letter is part of a typed sum, so
 * WASD would steer the rectangle and type into the calculator at the same time. */
#define M9_STEP 16
#define M9_CLEARANCE 8

/* M12: where the triangle lives. Below everything else, and small enough that
 * a circle of that radius fits with room to spare.
 * tests/check_boot.py mirrors these divisors. */
#define M12_CENTRE_X_DIVISOR 2
#define M12_CENTRE_Y_DIVISOR 8
#define M12_CENTRE_Y_PARTS   7
#define M12_RADIUS_DIVISOR   12

/* M4: where the cursor starts. Clear of the message, the key line and the
 * rectangle, so it never has to overlap them just by existing.
 * tests/check_boot.py mirrors these divisors. */
#define M4_CURSOR_START_X_DIVISOR 4
#define M4_CURSOR_START_Y_DIVISOR 6

/* M14 fixed backing stores. The desktop store supports the QEMU resolutions
 * used by this project; window stores exactly match their current objects.
 * These are explicit bounded pools until a real allocator exists. */
#define DESKTOP_MAX_WIDTH  1920u
#define DESKTOP_MAX_HEIGHT 1080u
#define DEMO_X 40
#define DEMO_Y 40
#define DEMO_WIDTH 1180u
#define DEMO_HEIGHT 720u
#define SYSTEM_X 860
#define SYSTEM_Y 80
#define SYSTEM_WIDTH 300u
#define SYSTEM_HEIGHT 180u

/* Limine scans the executable for these structures, so they must survive
 * optimisation and stay inside the section the linker script keeps. */
__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

/* Where the key line is drawn, worked out once the resolution is known. */
static uint64_t key_line_y;
static uint64_t key_line_scale;
static uint32_t colour_text;
static uint32_t colour_background;
static uint32_t colour_rect;
static uint64_t sum_line_y;
static uint32_t colour_triangle;
static uint32_t colour_desktop;
static uint32_t colour_system;
static uint32_t colour_accent;
static struct triangle_screen triangle_drawn;
static bool triangle_showing;
static struct moving_rect rect_drawn;
static bool rect_showing;
static int64_t rect_min_y, rect_max_y;
static struct calc calc_state;
/* M8: the variables. They sit beside the calculator rather than inside it
 * because clearing the line must not forget what has been stored. */
static struct vars vars_state;
static uint32_t colour_cursor;
static struct pointer pointer_state;
static struct moving_rect rect_state;
static struct rect_drag rect_drag_state;
static bool mouse_left_down;
static struct window_manager window_manager;
static WindowId demo_window_id;
static WindowId system_window_id;
static struct surface desktop_surface;
static struct surface demo_surface;
static struct surface system_surface;
static bool desktop_cursor_visible;
static uint32_t desktop_pixels[DESKTOP_MAX_WIDTH * DESKTOP_MAX_HEIGHT];
static uint32_t demo_pixels[DEMO_WIDTH * DEMO_HEIGHT];
static uint32_t system_pixels[SYSTEM_WIDTH * SYSTEM_HEIGHT];

/* Stops the CPU for good. Interrupts are masked so nothing can wake us into
 * a triple fault, which is what an unhandled interrupt would cause here. */
__attribute__((noreturn))
static void halt(void)
{
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

__attribute__((noreturn))
static void fail(const char *reason)
{
    log_str("me-os: FAILED: ");
    log_str(reason);
    log_str("\n");
    halt();
}

static uint64_t str_len(const char *s)
{
    uint64_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

/* Picks a whole number pixel scale so the message spans roughly three
 * quarters of the display at any resolution Limine gives us. */
static uint64_t pick_scale(uint64_t screen_w, uint64_t text_chars)
{
    if (text_chars == 0) {
        return 1;
    }

    uint64_t scale = (screen_w * 3) / (4 * text_chars * FONT_WIDTH);

    if (scale < 1) {
        scale = 1;
    } else if (scale > 16) {
        scale = 16;
    }
    return scale;
}

static uint64_t centred_x(uint64_t chars, uint64_t scale)
{
    uint64_t width = chars * FONT_WIDTH * scale;
    return width < demo_surface.width ? (demo_surface.width - width) / 2 : 0;
}

/* M16 counters. Enough to answer why the mouse was slow and whether it still
 * is, and nothing more. A profiler is not wanted here; six numbers are. */
static struct {
    uint64_t mouse_packets;     /* read off the controller */
    uint64_t mouse_batches;     /* loop passes that found at least one */
    uint64_t cursor_updates;    /* times the cursor actually moved */
    uint64_t whole_presents;    /* whole screen composed and written out */
    uint64_t region_presents;   /* one rectangle composed and written out */
    uint64_t pixels_presented;  /* pixels actually written to the display */
    uint64_t cursor_pixels;     /* of those, the ones a cursor move cost */
} counters;

/* Where the cursor sits on the desktop, outline included.
 *
 * Grown by one pixel because `cursor_draw` paints an outline a pixel outside the
 * arrow itself, and a region built from the arrow alone would leave that outline
 * behind when the cursor moved off it. */
static struct region cursor_region(int64_t x, int64_t y)
{
    return region_expand(region_make(x, y, CURSOR_WIDTH, CURSOR_HEIGHT), 1);
}

/* A rectangle drawn in Demo's own coordinates, said in desktop coordinates.
 *
 * Read from the window rather than from the constants it was created with, so
 * this keeps telling the truth once a layout manager starts moving windows. */
static struct region demo_region(int64_t x, int64_t y, int64_t width, int64_t height)
{
    const struct window *window = window_get_const(&window_manager, demo_window_id);
    if (window == NULL) {
        return region_none();
    }
    const struct region local =
        region_clip(region_make(x, y, width, height),
                    (int64_t)window->geometry.width, (int64_t)window->geometry.height);
    if (region_empty(&local)) {
        return region_none();
    }
    return region_make(local.x + window->geometry.x, local.y + window->geometry.y,
                       local.width, local.height);
}

/* Composes one rectangle of the desktop and writes only that rectangle out.
 *
 * The cursor is put back whenever the rectangle touches it. It is an overlay the
 * compositor owns rather than a window, so composing the pixels underneath it is
 * what removes it, and anything that composes over the cursor has to redraw it
 * or the cursor disappears wherever a window repaints.
 *
 * Composed from the window surfaces every time rather than from a saved copy of
 * what was underneath. Saving and restoring is faster still and is wrong the
 * moment a window repaints under the cursor, which would stamp stale content
 * wherever the cursor had been. */
static void compose_and_present(struct region region)
{
    region = region_clip(region, (int64_t)fb_width(), (int64_t)fb_height());
    if (region_empty(&region)) {
        return;
    }
    if (!compositor_compose_region(&window_manager, &desktop_surface,
                                   colour_desktop, region)) {
        fail("could not compose the desktop");
    }
    if (desktop_cursor_visible &&
        region_overlaps(region, cursor_region(pointer_state.x, pointer_state.y))) {
        cursor_draw(&desktop_surface, pointer_state.x, pointer_state.y,
                    colour_cursor, colour_background);
    }
    counters.pixels_presented += fb_present_region(&desktop_surface, region);
}

/* Apps update only their own surfaces. These two are the one path from those
 * surfaces to the framebuffer.
 *
 * Everything that changes part of the screen says which part. Before M16 there
 * was only the whole screen version below, and every mouse packet went through
 * it: at 1280x800 that cleared 1,024,000 desktop pixels, blitted every window
 * back over them, and then wrote 1,024,000 pixels out across the graphics
 * adapter, for a cursor that had moved a few pixels. */
static void present_region(struct region region)
{
    counters.region_presents++;
    compose_and_present(region);
}

static void present_desktop(void)
{
    counters.whole_presents++;
    compose_and_present(region_make(0, 0, (int64_t)fb_width(), (int64_t)fb_height()));
}

static void log_counters(void)
{
    log_str("me-os: input packets ");
    log_dec(counters.mouse_packets);
    log_str(" batches ");
    log_dec(counters.mouse_batches);
    log_str(" cursor ");
    log_dec(counters.cursor_updates);
    log_str(" whole ");
    log_dec(counters.whole_presents);
    log_str(" region ");
    log_dec(counters.region_presents);
    log_str(" pixels ");
    log_dec(counters.pixels_presented);
    log_str(" cursorpixels ");
    log_dec(counters.cursor_pixels);
    log_str("\n");
}

/* Replaces the key line. The whole line is cleared first, so a shorter
 * message cannot leave the tail of a longer one behind. */
static void draw_key_line(const char *text)
{
    const uint64_t height = FONT_HEIGHT * key_line_scale;

    surface_fill_rect(&demo_surface, 0, (int64_t)key_line_y,
                      demo_surface.width, (uint32_t)height, colour_background);
    surface_draw_string(&demo_surface, text,
                        (int64_t)centred_x(str_len(text), key_line_scale),
                        (int64_t)key_line_y, colour_text, (uint32_t)key_line_scale);
    present_region(demo_region(0, (int64_t)key_line_y,
                               (int64_t)demo_surface.width, (int64_t)height));
}

/* Erases the rectangle's old row and draws it at its current position. Only
 * the strip the rectangle lives in is touched, and it sits below every line of
 * text, so nothing else has to be redrawn. */
static void draw_rect(void)
{
    /* Erase exactly where it was, not the whole row. Since M9 it can move up
     * and down as well, so the old place is not always on the same line. */
    struct region changed = region_none();
    if (rect_showing) {
        surface_fill_rect(&demo_surface, rect_drawn.x, rect_drawn.y,
                          (uint32_t)rect_drawn.width, (uint32_t)rect_drawn.height,
                          colour_background);
        changed = demo_region(rect_drawn.x, rect_drawn.y,
                              (int64_t)rect_drawn.width, (int64_t)rect_drawn.height);
    }
    surface_fill_rect(&demo_surface, rect_state.x, rect_state.y,
                      (uint32_t)rect_state.width, (uint32_t)rect_state.height,
                      colour_rect);
    changed = region_union(changed,
                           demo_region(rect_state.x, rect_state.y,
                                       (int64_t)rect_state.width,
                                       (int64_t)rect_state.height));
    rect_drawn = rect_state;
    rect_showing = true;
    /* One rectangle covering where it was and where it is. A step is a few
     * pixels, so joining the two costs far less than presenting them apart
     * would cost in a second composition. A drag can jump further, and the
     * join is still bounded by the window. */
    present_region(changed);
}

/* Redraws the sum line in Demo-local coordinates. */
static void draw_sum_line(void)
{
    char line[CALC_MAX_INPUT + CALC_MAX_NUMBER + 2];
    const uint64_t height = FONT_HEIGHT * key_line_scale;

    calc_line(&calc_state, line, sizeof line);

    surface_fill_rect(&demo_surface, 0, (int64_t)sum_line_y,
                      demo_surface.width, (uint32_t)height, colour_background);
    surface_draw_string(&demo_surface, line,
                        (int64_t)centred_x(str_len(line), key_line_scale),
                        (int64_t)sum_line_y, colour_text, (uint32_t)key_line_scale);
    present_region(demo_region(0, (int64_t)sum_line_y,
                               (int64_t)demo_surface.width, (int64_t)height));
}

static bool same_name(const char *a, const char *b)
{
    for (uint64_t i = 0; a[i] != '\0' || b[i] != '\0'; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

/* Turns a key press into the character the calculator understands, or 0. */
static char calc_char_for(const struct kbd_key *key)
{
    if (key->ch != '\0') {
        return key->ch;
    }
    if (key->name == NULL) {
        return '\0';
    }
    if (same_name(key->name, "ENTER")) {
        return CALC_EVALUATE;
    }
    if (same_name(key->name, "BACKSPACE")) {
        return CALC_DELETE;
    }
    if (same_name(key->name, "ESCAPE")) {
        return CALC_CLEAR;
    }
    return '\0';
}

/* Draws the triangle's three edges in one colour. Used both to draw it and,
 * with the background colour, to rub the last one out. */
static void stroke_triangle(const struct triangle_screen *shape, uint32_t colour)
{
    for (int i = 0; i < TRIANGLE_VERTICES; i++) {
        const int next = (i + 1) % TRIANGLE_VERTICES;
        surface_draw_line(&demo_surface,
                          shape->x[i], shape->y[i],
                          shape->x[next], shape->y[next], colour);
    }
}

/* The smallest Demo-local rectangle holding all three vertices. */
static struct region triangle_region(const struct triangle_screen *shape)
{
    int64_t left = shape->x[0], right = shape->x[0];
    int64_t top = shape->y[0], bottom = shape->y[0];

    for (int i = 1; i < TRIANGLE_VERTICES; i++) {
        if (shape->x[i] < left) left = shape->x[i];
        if (shape->x[i] > right) right = shape->x[i];
        if (shape->y[i] < top) top = shape->y[i];
        if (shape->y[i] > bottom) bottom = shape->y[i];
    }
    /* Inclusive of the far vertex, and a pixel of slack on every side, because
     * a line ends on the pixel it names rather than before it. */
    return demo_region(left - 1, top - 1, right - left + 3, bottom - top + 3);
}

/* Erases where the triangle was and draws where it is now. */
static void redraw_triangle(void)
{
    struct triangle_screen now;

    triangle_vertices(&now);

    struct region changed = region_none();
    if (triangle_showing) {
        stroke_triangle(&triangle_drawn, colour_background);
        changed = triangle_region(&triangle_drawn);
    }
    stroke_triangle(&now, colour_triangle);
    changed = region_union(changed, triangle_region(&now));
    triangle_drawn = now;
    triangle_showing = true;
    present_region(changed);
}

static void show_key(const struct kbd_key *key)
{
    /* Long enough for the prefix plus the longest key name. */
    char line[32];
    uint64_t n = 0;

    for (const char *p = M2_PREFIX; *p != '\0' && n < sizeof(line) - 1; p++) {
        line[n++] = *p;
    }
    if (key->ch != '\0') {
        /* A space key would be invisible, so name it instead of drawing it. */
        if (key->ch == ' ') {
            for (const char *p = "SPACE"; *p != '\0' && n < sizeof(line) - 1; p++) {
                line[n++] = *p;
            }
        } else if (n < sizeof(line) - 1) {
            line[n++] = key->ch;
        }
    } else {
        for (const char *p = key->name; *p != '\0' && n < sizeof(line) - 1; p++) {
            line[n++] = *p;
        }
    }
    line[n] = '\0';

    draw_key_line(line);
    log_str("me-os: key ");
    log_str(line + str_len(M2_PREFIX));
    log_str("\n");
}

static enum window_key_code window_key_code_for(const struct kbd_key *key)
{
    if (key->ch != '\0' || key->name == NULL) return WINDOW_KEY_NONE;
    if (same_name(key->name, "ENTER")) return WINDOW_KEY_ENTER;
    if (same_name(key->name, "ESCAPE")) return WINDOW_KEY_ESCAPE;
    if (same_name(key->name, "BACKSPACE")) return WINDOW_KEY_BACKSPACE;
    if (same_name(key->name, "TAB")) return WINDOW_KEY_TAB;
    if (same_name(key->name, "UP")) return WINDOW_KEY_UP;
    if (same_name(key->name, "DOWN")) return WINDOW_KEY_DOWN;
    if (same_name(key->name, "LEFT")) return WINDOW_KEY_LEFT;
    if (same_name(key->name, "RIGHT")) return WINDOW_KEY_RIGHT;
    return WINDOW_KEY_NONE;
}

static const char *window_key_name(enum window_key_code code)
{
    switch (code) {
    case WINDOW_KEY_ENTER: return "ENTER";
    case WINDOW_KEY_ESCAPE: return "ESCAPE";
    case WINDOW_KEY_BACKSPACE: return "BACKSPACE";
    case WINDOW_KEY_TAB: return "TAB";
    case WINDOW_KEY_UP: return "UP";
    case WINDOW_KEY_DOWN: return "DOWN";
    case WINDOW_KEY_LEFT: return "LEFT";
    case WINDOW_KEY_RIGHT: return "RIGHT";
    default: return NULL;
    }
}

static void handle_demo_key(const struct window_event *event)
{
    const struct kbd_key key = {
        .ch = event->data.key.ch,
        .name = window_key_name(event->data.key.code),
    };
    show_key(&key);

    int64_t dx = 0, dy = 0;
    if (event->data.key.code == WINDOW_KEY_LEFT) {
        dx = -M9_STEP;
    } else if (event->data.key.code == WINDOW_KEY_RIGHT) {
        dx = M9_STEP;
    } else if (event->data.key.code == WINDOW_KEY_UP) {
        dy = -M9_STEP;
    } else if (event->data.key.code == WINDOW_KEY_DOWN) {
        dy = M9_STEP;
    }
    if (dx != 0 || dy != 0) {
        if (rect_state.speed != 0) {
            rect_state.speed = 0;
            log_stage("the rectangle is being steered, so it stopped drifting");
        }
        if (rect_nudge(&rect_state, dx, dy, demo_surface.width,
                       rect_min_y, rect_max_y)) {
            draw_rect();
            log_str("me-os: rectangle moved to ");
            log_dec((uint64_t)rect_state.x);
            log_str(",");
            log_dec((uint64_t)rect_state.y);
            log_str("\n");
        }
    }

    const char typed = calc_char_for(&key);
    if (typed != '\0' && calc_key(&calc_state, typed)) {
        draw_sum_line();
        if (calc_state.has_result) {
            log_str("me-os: sum ");
            log_str(calc_state.text);
            log_str(" = ");
            if (calc_state.result < 0) {
                log_str("-");
                log_dec((uint64_t)(-(calc_state.result + 1)) + 1u);
            } else {
                log_dec((uint64_t)calc_state.result);
            }
            log_str("\n");
        } else if (calc_state.error) {
            log_stage("sum could not be evaluated");
        }
    }
}

static void handle_demo_event(const struct window_event *event)
{
    if (event->type == WINDOW_EVENT_KEY_DOWN) {
        handle_demo_key(event);
        return;
    }
    if (event->type == WINDOW_EVENT_MOUSE_DOWN) {
        if (rect_drag_begin(&rect_drag_state, &rect_state,
                            event->data.mouse.x, event->data.mouse.y)) {
            rect_state.speed = 0;
            log_stage("rectangle drag started");
        }
        return;
    }
    if (event->type == WINDOW_EVENT_MOUSE_UP) {
        if (rect_drag_end(&rect_drag_state)) {
            log_stage("rectangle drag ended");
        }
        return;
    }
    if (event->type == WINDOW_EVENT_MOUSE_MOVE &&
        (event->data.mouse.buttons & WINDOW_MOUSE_LEFT) != 0 &&
        rect_drag_state.active &&
        rect_drag_move(&rect_drag_state, &rect_state,
                       event->data.mouse.x, event->data.mouse.y,
                       demo_surface.width, rect_min_y, rect_max_y)) {
        draw_rect();
        log_str("me-os: rectangle dragged to ");
        log_dec((uint64_t)rect_state.x);
        log_str(",");
        log_dec((uint64_t)rect_state.y);
        log_str("\n");
    }
}

static void drain_demo_events(void)
{
    struct window_event event;
    while (window_next_event(&window_manager, demo_window_id, &event)) {
        handle_demo_event(&event);
    }
}

void kmain(void)
{
    __asm__ volatile ("cli");  /* no interrupt table exists yet */

    log_init();
    log_stage("kernel entered");

    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        fail("bootloader does not support Limine base revision 3");
    }

    struct limine_framebuffer_response *res = framebuffer_request.response;
    if (res == NULL || res->framebuffer_count == 0) {
        fail("no framebuffer provided by the bootloader");
    }

    if (!fb_init(res->framebuffers[0])) {
        fail("unsupported framebuffer layout");
    }

    log_str("me-os: framebuffer ");
    log_dec(fb_width());
    log_str("x");
    log_dec(fb_height());
    log_str(" bpp 32\n");

    /* M13 objects become the visible M14 Demo and System windows through the
     * same stable-ID and attachment paths future callers will use. */
    window_manager_init(&window_manager);
    const struct window_spec demo_window = {
        .geometry = { .x = DEMO_X, .y = DEMO_Y,
                      .width = DEMO_WIDTH, .height = DEMO_HEIGHT },
        .title = "Demo",
    };
    const struct window_spec system_window = {
        .geometry = { .x = SYSTEM_X, .y = SYSTEM_Y,
                      .width = SYSTEM_WIDTH, .height = SYSTEM_HEIGHT },
        .title = "System",
    };
    if (!window_create(&window_manager, &demo_window, &demo_window_id) ||
        !window_create(&window_manager, &system_window, &system_window_id)) {
        fail("could not create the M13 window objects");
    }
    log_str("me-os: window model ready, created IDs ");
    log_dec(demo_window_id);
    log_str(" and ");
    log_dec(system_window_id);
    log_str("\n");

    colour_background = fb_rgb(0, 0, 0);
    colour_text = fb_rgb(255, 255, 255);
    colour_rect = fb_rgb(60, 170, 220);
    colour_cursor = fb_rgb(255, 214, 64);
    colour_triangle = fb_rgb(80, 220, 120);
    colour_desktop = fb_rgb(18, 24, 38);
    colour_system = fb_rgb(34, 46, 70);
    colour_accent = fb_rgb(82, 190, 220);

    if (fb_width() > DESKTOP_MAX_WIDTH || fb_height() > DESKTOP_MAX_HEIGHT ||
        !surface_init(&desktop_surface, desktop_pixels,
                      DESKTOP_MAX_WIDTH * DESKTOP_MAX_HEIGHT,
                      (uint32_t)fb_width(), (uint32_t)fb_height()) ||
        !surface_init(&demo_surface, demo_pixels,
                      DEMO_WIDTH * DEMO_HEIGHT, DEMO_WIDTH, DEMO_HEIGHT) ||
        !surface_init(&system_surface, system_pixels,
                      SYSTEM_WIDTH * SYSTEM_HEIGHT, SYSTEM_WIDTH, SYSTEM_HEIGHT) ||
        !window_attach_surface(&window_manager, demo_window_id, &demo_surface) ||
        !window_attach_surface(&window_manager, system_window_id, &system_surface)) {
        fail("could not create the M14 software surfaces");
    }
    if (!window_focus(&window_manager, demo_window_id, false)) {
        fail("could not focus Demo for M15 input routing");
    }

    surface_clear(&demo_surface, colour_background);
    surface_draw_string(&demo_surface, "DEMO", 12, 12, colour_accent, 2);
    surface_clear(&system_surface, colour_system);
    surface_fill_rect(&system_surface, 0, 0, SYSTEM_WIDTH, 28, colour_accent);
    surface_draw_string(&system_surface, "SYSTEM", 12, 8,
                        colour_desktop, 1);
    surface_draw_string(&system_surface, "WINDOW SURFACES", 20, 62,
                        colour_accent, 1);
    surface_draw_string(&system_surface, "OPAQUE COMPOSITOR", 20, 92,
                        colour_accent, 1);
    present_desktop();
    log_stage("M14 compositor ready with two overlapping windows");

    /* M1: the message, centred, exactly as the milestone specifies. */
    const uint64_t chars = str_len(M1_MESSAGE);
    const uint64_t scale = pick_scale(demo_surface.width, chars);
    const uint64_t text_h = FONT_HEIGHT * scale;
    const uint64_t y = text_h < demo_surface.height
        ? (demo_surface.height - text_h) / 2 : 0;

    surface_draw_string(&demo_surface, M1_MESSAGE,
                        (int64_t)centred_x(chars, scale), (int64_t)y,
                        colour_text, (uint32_t)scale);
    present_desktop();
    log_stage("drew the M1 message");

    /* M2: one line below, left blank until a key arrives. */
    key_line_scale = scale;
    key_line_y = y + text_h * 2;
    if (key_line_y + FONT_HEIGHT * key_line_scale >= demo_surface.height) {
        key_line_scale = 1;
        key_line_y = y + text_h + FONT_HEIGHT;
    }
    draw_key_line(M2_PROMPT);

    /* M3: a filled rectangle below the key line, or above the message if the
     * screen is too short for it to fit underneath. */
    const uint64_t rect_w = demo_surface.width / M3_RECT_WIDTH_DIVISOR;
    const uint64_t rect_h = demo_surface.height / M3_RECT_HEIGHT_DIVISOR;
    const uint64_t rect_x = rect_w < demo_surface.width
        ? (demo_surface.width - rect_w) / 2 : 0;
    uint64_t rect_y = key_line_y + FONT_HEIGHT * key_line_scale * 2;

    if (rect_y + rect_h >= demo_surface.height) {
        rect_y = y > rect_h * 2 ? y - rect_h * 2 : 0;
    }

    rect_state.x = (int64_t)rect_x;
    rect_state.y = (int64_t)rect_y;
    rect_state.width = rect_w;
    rect_state.height = rect_h;
    rect_state.speed = M5_RECT_SPEED;
    rect_state.direction = 1;
    rect_state.carried = 0;

    /* M9/M10: the corridor the arrow keys may move it within. It starts below
     * the key line and ends above the triangle, so steering cannot rub out any
     * text or any part of the shape that turns. Nothing else lives in between.
     * A wider range would need something that can repaint what was underneath,
     * which no milestone has asked for yet. */
    rect_min_y = (int64_t)(key_line_y + FONT_HEIGHT * key_line_scale + M9_CLEARANCE);
    rect_max_y = (int64_t)(demo_surface.height * M12_CENTRE_Y_PARTS /
                           M12_CENTRE_Y_DIVISOR)
               - (int64_t)(demo_surface.height / M12_RADIUS_DIVISOR)
               - (int64_t)rect_h - M9_CLEARANCE;
    if (rect_max_y < rect_min_y) {
        rect_max_y = rect_min_y;
    }
    /* Begin at the corridor's top so M9 can demonstrate three ordinary down
     * steps before the next press deliberately demonstrates M10 wrapping. */
    rect_state.y = rect_min_y;

    draw_rect();
    log_str("me-os: rectangle may be steered between y ");
    log_dec((uint64_t)rect_min_y);
    log_str(" and ");
    log_dec((uint64_t)rect_max_y);
    log_str("\n");
    log_str("me-os: drew the M3 rectangle ");
    log_dec(rect_w);
    log_str("x");
    log_dec(rect_h);
    log_str(" at ");
    log_dec((uint64_t)rect_state.x);
    log_str(",");
    log_dec((uint64_t)rect_state.y);
    log_str("\n");

    /* M6: the sum line, above the message, in the emptier half of the screen. */
    sum_line_y = y > FONT_HEIGHT * key_line_scale * M6_LINE_GAP
        ? y - FONT_HEIGHT * key_line_scale * M6_LINE_GAP
        : 0;
    vars_reset(&vars_state);
    calc_init(&calc_state, &vars_state);
    draw_sum_line();
    log_stage("drew the M6 sum line");

    kbd_init();
    log_stage("keyboard ready, waiting for keys");

    /* M4: a cursor the pointer state drives. M11 later uses the same pointer
     * state for dragging without making the mouse driver a drawing API. */
    pointer_init(&pointer_state,
                 (int64_t)(fb_width() / M4_CURSOR_START_X_DIVISOR),
                 (int64_t)(fb_height() / M4_CURSOR_START_Y_DIVISOR),
                 fb_width(), fb_height());

    const bool have_mouse = mouse_init();
    if (have_mouse) {
        log_stage("mouse ready");
    } else {
        log_stage("no mouse answered, the cursor will not move");
    }
    rect_drag_reset(&rect_drag_state);
    mouse_left_down = false;

    /* M5: a clock, so the rectangle moves at a rate rather than at whatever
     * speed this machine happens to run the loop. */
    timer_init();
    timer_poll();  /* discard the interval before the clock was started */
    log_stage("timer ready, the M5 rectangle is moving");

    /* M12: floating point, then a triangle that turns. The processor starts
     * with floating point instructions disabled, so this has to happen before
     * anything in geometry.c runs. If the processor will not do SSE the kernel
     * carries on without a triangle rather than faulting. */
    if (fpu_init()) {
        const int32_t triangle_x =
            (int32_t)(demo_surface.width / M12_CENTRE_X_DIVISOR);
        const int32_t triangle_y =
            (int32_t)(demo_surface.height * M12_CENTRE_Y_PARTS /
                      M12_CENTRE_Y_DIVISOR);
        const int32_t triangle_r =
            (int32_t)(demo_surface.height / M12_RADIUS_DIVISOR);

        triangle_init(triangle_x, triangle_y, triangle_r);
        redraw_triangle();

        log_str("me-os: floating point ready, drew the M12 triangle at ");
        log_dec((uint64_t)triangle_x);
        log_str(",");
        log_dec((uint64_t)triangle_y);
        log_str(" radius ");
        log_dec((uint64_t)triangle_r);
        log_str("\n");
    } else {
        log_stage("no SSE, running without the M12 triangle");
    }

    desktop_cursor_visible = true;
    present_desktop();
    log_str("me-os: drew the M4 cursor at ");
    log_dec((uint64_t)pointer_state.x);
    log_str(",");
    log_dec((uint64_t)pointer_state.y);
    log_str("\n");
    log_counters();

    for (;;) {
        struct kbd_key key;
        struct mouse_delta movement;

        if (kbd_poll(&key)) {
            const struct window_event event = {
                .type = WINDOW_EVENT_KEY_DOWN,
                .data.key = {
                    .ch = key.ch,
                    .code = window_key_code_for(&key),
                },
            };
            window_route_key(&window_manager, &event);
            drain_demo_events();
        }

        /* One reading of the clock, shared by everything that moves, so the
         * rectangle and the triangle cannot disagree about how much time has
         * passed. */
        const uint64_t elapsed = timer_poll();

        if (rect_advance(&rect_state, elapsed, TIMER_HZ, demo_surface.width)) {
            draw_rect();
        }
        if (fpu_ready() && triangle_advance(elapsed, TIMER_HZ)) {
            redraw_triangle();
        }

        /* Every packet the controller is holding, not one of them.
         *
         * The device reports a hundred times a second. The old loop took one
         * packet per pass, so once a pass cost more than ten milliseconds the
         * controller's buffer filled and the cursor fell further behind with
         * every report. That is what made the mouse feel slow: not a fixed
         * delay but a growing one, showing a position from a packet sent
         * seconds earlier. Draining the queue means a backlog collapses into
         * one step instead of being replayed at the speed of the display.
         *
         * Buttons are still handled packet by packet, with the pointer where it
         * was when that packet arrived, so a click and its release inside one
         * batch are two events in the right order at the right places. Only the
         * drawing is deferred to once per batch. */
        bool any_packet = false;
        bool cursor_moved = false;
        bool focus_changed = false;
        const struct region cursor_was =
            cursor_region(pointer_state.x, pointer_state.y);

        while (mouse_poll(&movement)) {
            counters.mouse_packets++;
            any_packet = true;

            const bool this_packet_moved =
                pointer_move(&pointer_state, movement.dx, movement.dy,
                             fb_width(), fb_height());
            cursor_moved = cursor_moved || this_packet_moved;

            const bool pressed = movement.left && !mouse_left_down;
            const bool released = !movement.left && mouse_left_down;
            uint8_t buttons = 0;
            if (movement.left) buttons |= WINDOW_MOUSE_LEFT;
            if (movement.right) buttons |= WINDOW_MOUSE_RIGHT;
            if (movement.middle) buttons |= WINDOW_MOUSE_MIDDLE;

            if (pressed) {
                const WindowId before = window_focused(&window_manager);
                const WindowId target = window_route_pointer(
                    &window_manager, WINDOW_EVENT_MOUSE_DOWN,
                    pointer_state.x, pointer_state.y, buttons);
                if (before != window_focused(&window_manager)) {
                    focus_changed = true;
                    log_str("me-os: focus moved to window ");
                    log_dec(target);
                    log_str("\n");
                }
            }
            if (this_packet_moved) {
                window_route_pointer(&window_manager, WINDOW_EVENT_MOUSE_MOVE,
                                     pointer_state.x, pointer_state.y, buttons);
            }
            if (released) {
                window_route_pointer(&window_manager, WINDOW_EVENT_MOUSE_UP,
                                     pointer_state.x, pointer_state.y, buttons);
            }
            mouse_left_down = movement.left;
        }

        if (any_packet) {
            counters.mouse_batches++;
            /* Before the cursor is presented, because a drag repaints the
             * rectangle and that repaint has to be underneath the cursor rather
             * than composed over it afterwards. */
            drain_demo_events();

            if (focus_changed) {
                /* Raising a window reorders everything it touches, and the
                 * cheap answer to which pixels changed is all of them. One
                 * click is rare enough to pay for. */
                present_desktop();
            } else if (cursor_moved) {
                counters.cursor_updates++;
                const uint64_t before_pixels = counters.pixels_presented;
                const struct region cursor_now =
                    cursor_region(pointer_state.x, pointer_state.y);
                /* One rectangle when the two overlap, two when they do not.
                 * Joining a cursor that has flicked across the screen would
                 * present the whole corridor between the ends, which for a fast
                 * movement is most of the display. */
                if (region_overlaps(cursor_was, cursor_now)) {
                    present_region(region_union(cursor_was, cursor_now));
                } else {
                    present_region(cursor_was);
                    present_region(cursor_now);
                }
                counters.cursor_pixels +=
                    counters.pixels_presented - before_pixels;
                /* Occasionally, so the numbers reach the boot log without the
                 * logging itself becoming the slow part of the input path. */
                if (counters.cursor_updates % 8 == 0) {
                    log_counters();
                }
            }
        }
        /* Hints to the processor that this is a spin loop. Interrupts are
         * masked, so hlt would never wake up again. */
        __asm__ volatile ("pause");
    }
}
