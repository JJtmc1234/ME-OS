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
#include "cmd.h"
#include "ata.h"
#include "cpu.h"
#include "editor.h"
#include "rtc.h"
#include "vfs.h"
#include "vfsdisk.h"
#include "desktop.h"
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
#include "term.h"
#include "termback.h"
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

/* M14 and M18 fixed backing stores. The desktop store supports the resolutions
 * this project boots in, and every app gets one the same size, because the
 * largest tile the layout can hand out is the whole workspace and an app must
 * never be given a tile its store cannot hold. Explicit bounded pools until a
 * real allocator exists. */
#define DESKTOP_MAX_WIDTH  1920u
#define DESKTOP_MAX_HEIGHT 1080u
/* One store shared by every tile, because tiles never overlap and so their
 * areas together are never more than the workspace. Four private stores, each
 * big enough to be the only window, cost four times this for a case that cannot
 * happen, and zeroing them at boot took long enough to see on the screen. */
#define TILE_ARENA_PIXELS  ((size_t)DESKTOP_MAX_WIDTH * DESKTOP_MAX_HEIGHT)

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

/* Asked for so the terminal can answer MEM with what this machine actually has
 * rather than a number written down when the code was compiled. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
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
static struct desktop desktop;
static WindowId demo_window_id;
static struct surface desktop_surface;
/* A copy of the Demo app's client view. Sharing the frame's pixels, so Demo
 * draws in its own coordinates and the result is already inside its tile with
 * no second buffer and no second blit. Refreshed on every relayout, because a
 * new tile means a new view. */
static struct surface demo_surface;
static bool desktop_cursor_visible;
static uint64_t uptime_seconds;
static uint64_t timer_carried;
static struct term terminal;
static WindowId terminal_window_id;
static char cpu_vendor_text[CPU_VENDOR_CAPACITY];
static char cpu_brand_text[CPU_BRAND_CAPACITY];
static uint64_t usable_memory_bytes;
static uint64_t total_memory_bytes;
static uint64_t memory_regions;
static bool launcher_open;
static struct vfs filesystem;

/* The disk the filesystem is saved to, and the drive underneath it.
 *
 * Both are left empty when there is nothing to find, and `disk_present` is
 * false then, so SAVE and LOAD say there is no disk rather than doing nothing
 * and looking like they worked. */
static struct ata_drive drive;
static struct disk storage;

/* The biggest file there is, said out loud after loading a disk.
 *
 * Useful to anybody reading the log, and it is the one line that can show a
 * file really spans more than one block. A count of blocks across the whole
 * filesystem cannot: a hundred single block files add up to the same number as
 * fifty files of two.
 */
static void log_largest_file(void)
{
    int16_t biggest = VFS_NONE;
    uint32_t most = 0;
    for (int16_t at = 0; at < VFS_MAX_NODES; at++) {
        const struct vfs_node *node = vfs_get(&filesystem, at);
        if (node == NULL || node->kind != VFS_FILE || node->length < most) {
            continue;
        }
        most = node->length;
        biggest = at;
    }
    if (biggest == VFS_NONE) {
        return;
    }
    char path[VFS_PATH_MAX];
    vfs_path_of(&filesystem, biggest, path, sizeof path);

    /* And a checksum of what it holds.
     *
     * The block count says a file spanned two blocks. It does not say the bytes
     * came back in the right order, and a file whose second block was read as
     * its first is exactly as long as one that was not. This is position
     * sensitive, so the two would differ. The run before a restart and the run
     * after it can be compared without either of them knowing what the file is
     * supposed to say. FNV-1a, chosen because it is four lines. */
    static char text[VFS_FILE_MAX + 1];
    uint64_t length = 0;
    uint32_t sum = 2166136261u;
    if (vfs_read(&filesystem, path, text, sizeof text, &length) == VFS_OK) {
        for (uint64_t i = 0; i < length; i++) {
            sum = (sum ^ (uint8_t)text[i]) * 16777619u;
        }
    }

    log_str("me-os: largest file ");
    log_str(path);
    log_str(", ");
    log_dec(most);
    log_str(" bytes in ");
    log_dec(vfs_blocks_for(most));
    log_str(" blocks, sum ");
    log_dec(sum);
    log_str("\n");
}

/* Defined further down, next to the terminal that calls it most. */
static void save_the_filesystem(void);

/* Looks in the four places a legacy IDE disk can be, and takes the first one
 * that is not the CD the machine booted from.
 *
 * Four rather than one because which socket a disk lands on is up to whoever
 * set the machine up, and a driver that only looks at the primary master is a
 * driver that works on one person's configuration. */
static void find_the_disk(void)
{
    static const struct { uint16_t io; uint16_t control; bool slave; } sockets[] = {
        { ATA_PRIMARY_IO,   ATA_PRIMARY_CONTROL,   false },
        { ATA_PRIMARY_IO,   ATA_PRIMARY_CONTROL,   true  },
        { ATA_SECONDARY_IO, ATA_SECONDARY_CONTROL, false },
        { ATA_SECONDARY_IO, ATA_SECONDARY_CONTROL, true  },
    };

    for (uint64_t i = 0; i < sizeof sockets / sizeof sockets[0]; i++) {
        if (!ata_probe(&drive, sockets[i].io, sockets[i].control,
                       sockets[i].slave)) {
            continue;
        }
        ata_as_disk(&drive, &storage);
        log_str("me-os: disk ");
        log_str(drive.model[0] != '\0' ? drive.model : "UNNAMED");
        log_str(", ");
        log_dec(drive.sectors);
        log_str(" sectors of ");
        log_dec(DISK_SECTOR);
        log_str(" bytes\n");
        return;
    }
    /* Said out loud. A machine with no disk is a perfectly good machine and
     * this is the same one M20 through M22 ran on, but somebody reading the log
     * after a SAVE that would not work needs to find the reason in it. */
    log_str("me-os: no disk found, so the filesystem is memory only\n");
}

static struct editor text_editor;
static WindowId editor_window_id;
static struct rtc_time clock_time;
static bool clock_answered;
static char clock_date[16];
static char clock_time_text[16];
static uint32_t desktop_pixels[DESKTOP_MAX_WIDTH * DESKTOP_MAX_HEIGHT];
static uint32_t tile_arena[TILE_ARENA_PIXELS];

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

/* A rectangle drawn in Demo's own coordinates, said in screen coordinates.
 *
 * Asked of the desktop rather than worked out here, because where Demo sits is
 * the layout's business and it changes every time a window opens or closes. */
static struct region demo_region(int64_t x, int64_t y, int64_t width, int64_t height)
{
    return desktop_client_region(&desktop, demo_window_id, x, y, width, height);
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
/* Declared here because composition has to put the launcher back over the
 * windows it composed, and the menu itself belongs down with the rest of the
 * click handling rather than up here with the drawing. */
static struct region launcher_region(void);
static void draw_launcher(void);
static void close_launcher(void);

static void compose_and_present(struct region region)
{
    region = region_clip(region, (int64_t)fb_width(), (int64_t)fb_height());
    if (region_empty(&region)) {
        return;
    }
    if (!compositor_compose_region(&window_manager, &desktop_surface,
                                   desktop.theme.desktop, region)) {
        fail("could not compose the desktop");
    }
    /* After the windows, because the compositor clears what it composes and
     * would wipe a bar drawn before it. Only when the region reaches one, so a
     * cursor moving across the middle of the screen redraws neither. */
    if (region_overlaps(region, desktop_top_bar_region(&desktop)) ||
        region_overlaps(region, desktop_taskbar_region(&desktop))) {
        desktop_draw_bars(&desktop, &desktop_surface,
                          clock_answered ? clock_time_text : NULL,
                          uptime_seconds);
    }
    if (launcher_open && region_overlaps(region, launcher_region())) {
        draw_launcher();
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
/* Set while a relayout is in flight. Every tile has moved, so the small region
 * each drawing call would present names a place nothing is any more, and the
 * whole screen is presented once at the end instead. */
static bool presenting_suppressed;

static void present_region(struct region region)
{
    if (presenting_suppressed) {
        return;
    }
    counters.region_presents++;
    compose_and_present(region);
}

static void present_desktop(void)
{
    if (presenting_suppressed) {
        return;
    }
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
static char key_line_text[32] = M2_PROMPT;

static void draw_key_line(const char *text)
{
    const uint64_t height = FONT_HEIGHT * key_line_scale;

    /* Kept, because a relayout redraws every line from scratch and the last key
     * a person pressed is still the true answer to what the last key was. */
    if (text != key_line_text) {
        uint64_t i = 0;
        for (; text[i] != '\0' && i + 1 < sizeof key_line_text; i++) {
            key_line_text[i] = text[i];
        }
        key_line_text[i] = '\0';
    }

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
    if (same_name(key->name, "PAGEUP")) return WINDOW_KEY_PAGE_UP;
    if (same_name(key->name, "PAGEDOWN")) return WINDOW_KEY_PAGE_DOWN;
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
    case WINDOW_KEY_PAGE_UP: return "PAGEUP";
    case WINDOW_KEY_PAGE_DOWN: return "PAGEDOWN";
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



/* Declared here because the shell can ask for a file to be opened and the
 * editor that opens it lives below, next to the rest of the editor. */
static void open_in_editor(const char *path);

/* --- the Terminal app -------------------------------------------------------
 *
 * The thing that makes a machine feel like a computer rather than a picture of
 * one is being able to type at it and have it answer. Every command reports
 * something this kernel actually knows: the processor's own answer to CPUID,
 * the memory map the bootloader handed over, the resolution Limine chose, the
 * clock, and how many windows are open. Nothing invents a filesystem or a
 * process list, because there are none. See M19 in docs/milestones.md.
 */

#define ME_OS_VERSION "0.20"

/* Adds up what the bootloader said is usable, and what it saw altogether. */
static void read_memory_map(void)
{
    const struct limine_memmap_response *map = memmap_request.response;
    if (map == NULL) {
        return;
    }
    for (uint64_t i = 0; i < map->entry_count; i++) {
        const struct limine_memmap_entry *entry = map->entries[i];
        if (entry == NULL) {
            continue;
        }
        /* Usable and bootloader reclaimable both become memory this kernel
         * could use. Counting only the first would understate a machine by the
         * size of everything Limine is still holding. */
        if (entry->type == LIMINE_MEMMAP_USABLE ||
            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            usable_memory_bytes += entry->length;
        }
        /* Memory that is really there, whoever owns it. The framebuffer and the
         * reserved ranges above it are address space rather than memory: adding
         * those made a machine with half a gigabyte report twelve, because the
         * hole between the top of RAM and the devices is enormous and empty. */
        if (entry->type != LIMINE_MEMMAP_FRAMEBUFFER &&
            entry->type != LIMINE_MEMMAP_RESERVED) {
            total_memory_bytes += entry->length;
        }
        memory_regions++;
    }
}

static struct surface *terminal_client(void)
{
    const size_t index = desktop_index_of(&desktop, terminal_window_id);
    if (!desktop_on_screen(&desktop, index)) {
        return NULL;
    }
    return &desktop_app_at(&desktop, index)->client;
}

static void terminal_paint(void)
{
    struct surface *client = terminal_client();
    if (client == NULL) {
        return;
    }
    term_draw(&terminal, client, desktop.theme.chrome_text,
              desktop.theme.window, desktop.theme.accent);
    present_region(desktop_client_region(&desktop, terminal_window_id, 0, 0,
                                         (int64_t)client->width,
                                         (int64_t)client->height));
}

static void terminal_key(const struct window_event *event)
{
    char line[TERM_INPUT_MAX];
    const bool enter = event->data.key.code == WINDOW_KEY_ENTER;
    const bool backspace = event->data.key.code == WINDOW_KEY_BACKSPACE;

    /* Up and down walk what was typed before, which is the one thing a shell
     * without it is most obviously missing. */
    /* Scrollback first, because these keys mean nothing else here and a person
     * looking at the past should not also be typing into the line editor. */
    if (event->data.key.code == WINDOW_KEY_PAGE_UP ||
        event->data.key.code == WINDOW_KEY_PAGE_DOWN) {
        const bool back = event->data.key.code == WINDOW_KEY_PAGE_UP;
        if (termback_scroll(&terminal, back ? 1 : -1)) {
            log_str("me-os: terminal scrolled to ");
            log_dec(termback_offset(&terminal));
            log_str(" lines back of ");
            log_dec(termback_held(&terminal));
            log_str("\n");
            terminal_paint();
        }
        return;
    }
    if (event->data.key.code == WINDOW_KEY_UP ||
        event->data.key.code == WINDOW_KEY_DOWN) {
        if (term_history_step(&terminal, event->data.key.code == WINDOW_KEY_UP)) {
            terminal_paint();
        }
        return;
    }

    if (!term_key(&terminal, event->data.key.ch, enter, backspace,
                  line, sizeof line)) {
        terminal_paint();
        return;
    }

    /* Straight to the terminal to begin with. A pipe or an arrow in the line
     * makes `cmd_run` point it somewhere else and put it back afterwards. */
    struct cmd_out output;
    cmd_out_to_term(&output, &terminal);

    struct cmd_context context = {
        .out = &output,
        .input = NULL,
        .term = &terminal,
        .uptime_seconds = uptime_seconds,
        .screen_width = fb_width(),
        .screen_height = fb_height(),
        .usable_memory = usable_memory_bytes,
        .total_memory = total_memory_bytes,
        .windows_open = desktop.app_count,
        .windows_visible = desktop_visible_count(&desktop),
        .cpu_vendor = cpu_vendor_text,
        .cpu_brand = cpu_brand_text,
        .disk_model = drive.present ? drive.model : "",
        .disk_sectors = drive.sectors,
        .disk_sector_bytes = DISK_SECTOR,
        .version = ME_OS_VERSION,
        .fs = &filesystem,
        .date = clock_date,
        .time = clock_time_text,
    };
    cmd_run(&context, line);

    /* EDIT cannot open a window, so it says which file it wants and this does
     * it. One direction: the shell asks, the desktop acts. */
    if (context.open_editor[0] != '\0') {
        open_in_editor(context.open_editor);
    }
    /* The prompt follows the working directory, so CD has something to show for
     * itself and a person can see where they are without asking. */
    char prompt[VFS_PATH_MAX + 4];
    const uint64_t at = vfs_path_of(&filesystem, filesystem.cwd, prompt,
                                    sizeof prompt - 3);
    prompt[at] = ' ';
    prompt[at + 1] = '>';
    prompt[at + 2] = ' ';
    prompt[at + 3] = '\0';
    term_set_prompt(&terminal, prompt);

    log_str("me-os: terminal ran ");
    log_str(line);
    log_str("\n");
    save_the_filesystem();
    terminal_paint();
}

/* Writes the filesystem out, but only when there is something to write.
 *
 * Called after anything that could have changed a file, rather than left to a
 * SYNC nobody remembers to type. A machine that loses an afternoon's work
 * because you shut it down without a magic word is a machine nobody should have
 * to learn, and the counter in `struct vfs` is what makes doing it this way
 * cost nothing when nothing changed.
 *
 * Failures are reported and not hidden. A save that quietly did not happen is
 * the same as no disk at all, except that you find out later.
 */
static uint32_t saved_at;

static void save_the_filesystem(void)
{
    if (!disk_present(&storage) || filesystem.changes == saved_at) {
        return;
    }
    const enum vfsdisk_result done = vfsdisk_save(&filesystem, &storage);
    if (done == VFSDISK_OK) {
        saved_at = filesystem.changes;
        log_str("me-os: filesystem saved, ");
        log_dec(vfs_used_nodes(&filesystem));
        log_str(" entries in ");
        log_dec(vfs_used_blocks(&filesystem));
        log_str(" blocks\n");
        return;
    }
    log_str("me-os: FAILED to save the filesystem: ");
    log_str(vfsdisk_explain(done));
    log_str("\n");
}

static void drain_terminal_events(void)
{
    struct window_event event;
    while (window_next_event(&window_manager, terminal_window_id, &event)) {
        if (event.type == WINDOW_EVENT_KEY_DOWN) {
            terminal_key(&event);
        }
    }
}


/* Declared here because the editor opens itself, which means putting a window
 * back into the layout and moving focus to it, and both of those live further
 * down with the rest of the desktop. */
static void relayout_desktop(void);
static void refresh_focus(void);

/* --- the Editor app --------------------------------------------------------
 *
 * The shell could read a file and replace it with one line. Nothing could change
 * the middle of one, which is most of what anybody does with a filesystem.
 */

static struct surface *editor_client(void)
{
    const size_t index = desktop_index_of(&desktop, editor_window_id);
    if (!desktop_on_screen(&desktop, index)) {
        return NULL;
    }
    return &desktop_app_at(&desktop, index)->client;
}

static void editor_paint(void)
{
    struct surface *client = editor_client();
    if (client == NULL) {
        return;
    }
    editor_fit(&text_editor, client->width, client->height);
    editor_draw(&text_editor, client, desktop.theme.chrome_text,
                desktop.theme.window, desktop.theme.accent, desktop.theme.bar_dim);
    present_region(desktop_client_region(&desktop, editor_window_id, 0, 0,
                                         (int64_t)client->width,
                                         (int64_t)client->height));
}

/* Writes the buffer back to the file it came from. */
static void editor_save(void)
{
    if (text_editor.path[0] == '\0') {
        editor_set_status(&text_editor, "NO FILE TO SAVE TO. USE EDIT NAME");
        return;
    }
    char text[VFS_FILE_MAX + 1];
    bool complete = false;
    editor_text(&text_editor, text, sizeof text, &complete);
    /* Refused rather than truncated. Saving part of a document over the whole of
     * one is the worst thing an editor can do. */
    if (!complete) {
        editor_set_status(&text_editor, "TOO BIG FOR ONE FILE. NOTHING WAS SAVED");
        return;
    }
    const enum vfs_result done = vfs_write(&filesystem, text_editor.path, text);
    if (done != VFS_OK) {
        editor_set_status(&text_editor, vfs_explain(done));
        return;
    }
    text_editor.changed = false;
    editor_set_status(&text_editor, "SAVED");
    log_str("me-os: editor saved ");
    log_str(text_editor.path);
    log_str("\n");
    save_the_filesystem();
}

/* Opens a file in the editor and puts the editor in front of somebody. */
static void open_in_editor(const char *path)
{
    char text[VFS_FILE_MAX + 1];
    const enum vfs_result found =
        vfs_read(&filesystem, path, text, sizeof text, NULL);

    editor_set_path(&text_editor, path);
    if (found == VFS_OK) {
        editor_load(&text_editor, text);
    } else if (found == VFS_NOT_FOUND) {
        /* A file that is not there yet is a new one, which is what every editor
         * does when asked to open a name nothing has used. */
        editor_load(&text_editor, "");
        editor_set_status(&text_editor, "NEW FILE");
    } else {
        editor_load(&text_editor, "");
        editor_set_status(&text_editor, vfs_explain(found));
    }

    if (desktop_set_hidden(&desktop, editor_window_id, false)) {
        relayout_desktop();
    }
    window_focus(&window_manager, editor_window_id, false);
    refresh_focus();
    editor_paint();
    log_str("me-os: editor opened ");
    log_str(path);
    log_str("\n");
}

static void drain_editor_events(void)
{
    struct window_event event;
    while (window_next_event(&window_manager, editor_window_id, &event)) {
        if (event.type != WINDOW_EVENT_KEY_DOWN) {
            continue;
        }
        /* Control and a letter belongs to the app when the window manager did
         * not claim it, which is how an app gets a shortcut of its own. */
        if (event.data.key.ch == 'O' && event.data.key.ctrl) {
            editor_save();
        } else {
            switch (event.data.key.code) {
            case WINDOW_KEY_UP:        editor_move(&text_editor, 0, -1); break;
            case WINDOW_KEY_DOWN:      editor_move(&text_editor, 0, 1); break;
            case WINDOW_KEY_LEFT:      editor_move(&text_editor, -1, 0); break;
            case WINDOW_KEY_RIGHT:     editor_move(&text_editor, 1, 0); break;
            case WINDOW_KEY_ENTER:     editor_newline(&text_editor); break;
            case WINDOW_KEY_BACKSPACE: editor_backspace(&text_editor); break;
            case WINDOW_KEY_TAB:       editor_end(&text_editor); break;
            case WINDOW_KEY_ESCAPE:    editor_home(&text_editor); break;
            default:                   editor_insert(&text_editor,
                                                     event.data.key.ch); break;
            }
        }
        editor_paint();
    }
}

/* --- the ME OS Default desktop ---------------------------------------------
 *
 * Everything M1 to M12 drew now lives in one window called Demo, in that
 * window's own coordinates, and the layout decides where that window is. Demo
 * lays itself out inside whatever client area it was given, so the same code
 * works whether it has a quarter of the screen or all of it.
 *
 * Three small windows sit beside it. They exist to prove the tiling, and they
 * are deliberately tiny: inventing applications to fill a screen would be
 * building something the milestone did not ask for.
 */

static uint64_t message_y;
static uint64_t message_scale;
static bool demo_announced;

/* One line of the layout, so `tests/check_boot.py` can find a window without
 * having to know the tiling arithmetic. It checks the rectangles against each
 * other for overlap, so the kernel saying where it put something is checked
 * rather than believed. */
static void log_layout(void)
{
    for (size_t i = 0; i < desktop.app_count; i++) {
        const struct desktop_app *app = desktop_app_at(&desktop, i);
        const struct window *window =
            window_get_const(&window_manager, app->id);
        log_str("me-os: tile ");
        log_str(app->title);
        /* A window that is not on the screen has no tile, and says so rather
         * than reporting the rectangle it held last time. That rectangle
         * belongs to a different window now, and printing it would have this
         * one claiming room it does not own. */
        if (window == NULL || !desktop_on_screen(&desktop, i)) {
            if (app->hidden) {
                log_str(" hidden\n");
            } else {
                log_str(" on workspace ");
                log_dec((uint64_t)app->workspace);
                log_str("\n");
            }
            continue;
        }
        log_str(" at ");
        log_dec((uint64_t)window->geometry.x);
        log_str(",");
        log_dec((uint64_t)window->geometry.y);
        log_str(" size ");
        log_dec(window->geometry.width);
        log_str("x");
        log_dec(window->geometry.height);
        log_str(" client ");
        log_dec((uint64_t)(window->geometry.x + desktop.layout.border));
        log_str(",");
        log_dec((uint64_t)(window->geometry.y + desktop.layout.border +
                           SHELL_TITLE_HEIGHT));
        log_str(" ");
        log_dec(app->client.width);
        log_str("x");
        log_dec(app->client.height);
        log_str(app->id == window_focused(&window_manager) ? " focused\n" : "\n");
    }
}

/* Works out where everything in Demo goes, for the client area it has now.
 *
 * Called again after every relayout rather than once at boot, because a tile
 * that changed size is a different amount of room and the message has to be
 * centred in the room there is, not the room there was. */
static void demo_layout(void)
{
    const uint64_t chars = str_len(M1_MESSAGE);
    message_scale = pick_scale(demo_surface.width, chars);
    const uint64_t text_h = FONT_HEIGHT * message_scale;
    message_y = text_h < demo_surface.height
        ? (demo_surface.height - text_h) / 2 : 0;

    key_line_scale = message_scale;
    key_line_y = message_y + text_h * 2;
    if (key_line_y + FONT_HEIGHT * key_line_scale >= demo_surface.height) {
        key_line_scale = 1;
        key_line_y = message_y + text_h + FONT_HEIGHT;
    }

    sum_line_y = message_y > FONT_HEIGHT * key_line_scale * M6_LINE_GAP
        ? message_y - FONT_HEIGHT * key_line_scale * M6_LINE_GAP
        : 0;

    const uint64_t rect_w = demo_surface.width / M3_RECT_WIDTH_DIVISOR;
    const uint64_t rect_h = demo_surface.height / M3_RECT_HEIGHT_DIVISOR;
    const uint64_t rect_x = rect_w < demo_surface.width
        ? (demo_surface.width - rect_w) / 2 : 0;
    uint64_t rect_y = key_line_y + FONT_HEIGHT * key_line_scale * 2;
    if (rect_y + rect_h >= demo_surface.height) {
        rect_y = message_y > rect_h * 2 ? message_y - rect_h * 2 : 0;
    }

    rect_state.width = rect_w;
    rect_state.height = rect_h;
    rect_state.x = (int64_t)rect_x;

    /* M9/M10: the corridor the arrow keys may move it within. It starts below
     * the key line and ends above the triangle, so steering cannot rub out any
     * text or any part of the shape that turns. */
    rect_min_y = (int64_t)(key_line_y + FONT_HEIGHT * key_line_scale + M9_CLEARANCE);
    rect_max_y = (int64_t)(demo_surface.height * M12_CENTRE_Y_PARTS /
                           M12_CENTRE_Y_DIVISOR)
               - (int64_t)(demo_surface.height / M12_RADIUS_DIVISOR)
               - (int64_t)rect_h - M9_CLEARANCE;
    if (rect_max_y < rect_min_y) {
        rect_max_y = rect_min_y;
    }
    /* Kept where it was when the corridor still holds it, so a relayout does
     * not undo the steering somebody just did. Put back at the top when the new
     * corridor cannot reach where it used to be. */
    if (rect_state.y < rect_min_y || rect_state.y > rect_max_y) {
        rect_state.y = rect_min_y;
    }

    if (fpu_ready()) {
        triangle_init((int32_t)(demo_surface.width / M12_CENTRE_X_DIVISOR),
                      (int32_t)(demo_surface.height * M12_CENTRE_Y_PARTS /
                                M12_CENTRE_Y_DIVISOR),
                      (int32_t)(demo_surface.height / M12_RADIUS_DIVISOR));
    }

    if (!demo_announced) {
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
    }
}

/* Draws all of Demo into its client area. Nothing is presented from here: the
 * caller has just moved every tile and presents the screen once at the end. */
static void demo_repaint(void)
{
    surface_clear(&demo_surface, colour_background);
    rect_showing = false;
    triangle_showing = false;

    surface_draw_string(&demo_surface, M1_MESSAGE,
                        (int64_t)centred_x(str_len(M1_MESSAGE), message_scale),
                        (int64_t)message_y, colour_text, (uint32_t)message_scale);
    if (!demo_announced) {
        log_stage("drew the M1 message");
    }

    draw_key_line(key_line_text);
    draw_sum_line();
    if (!demo_announced) {
        log_stage("drew the M6 sum line");
    }
    draw_rect();
    if (fpu_ready()) {
        redraw_triangle();
        if (!demo_announced) {
            log_stage("floating point ready, drew the M12 triangle");
        }
    }
    demo_announced = true;
}

/* One line of a labelled value, so the System Info window reads as a table
 * rather than as a paragraph. */
static void draw_field(struct surface *surface, int64_t y,
                       const char *label, const char *value, uint32_t colour)
{
    surface_draw_string(surface, label, 8, y, desktop.theme.bar_dim, 1);
    surface_draw_string(surface, value, 8 + 10 * FONT_WIDTH, y, colour, 1);
}

static void number_to_text(uint64_t value, char *out, uint64_t capacity)
{
    char digits[21];
    uint64_t n = 0;
    if (value == 0) {
        digits[n++] = '0';
    }
    while (value > 0 && n < sizeof digits) {
        digits[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    uint64_t written = 0;
    while (n > 0 && written + 1 < capacity) {
        out[written++] = digits[--n];
    }
    out[written] = '\0';
}

/* What this machine is, asked of the machine.
 *
 * Every line here comes from somewhere real: CPUID for the processor, the
 * bootloader's memory map for the memory, Limine's answer for the resolution,
 * and the clock for the uptime. A system information window that reported
 * anything else would be a picture of one. */
static void paint_system_info(struct surface *client)
{
    char text[64];
    int64_t y = 8;
    const int64_t line = FONT_HEIGHT + 6;

    draw_field(client, y, "VENDOR",
               cpu_vendor_text[0] != '\0' ? cpu_vendor_text : "UNKNOWN",
               desktop.theme.chrome_text);
    y += line;
    if (cpu_brand_text[0] != '\0') {
        surface_draw_string(client, cpu_brand_text, 8, y,
                            desktop.theme.chrome_text, 1);
        y += line;
    }

    number_to_text(fb_width(), text, sizeof text);
    uint64_t at = 0;
    while (text[at] != '\0') at++;
    text[at++] = 'X';
    number_to_text(fb_height(), text + at, sizeof text - at);
    draw_field(client, y, "SCREEN", text, colour_accent);
    y += line;

    if (total_memory_bytes > 0) {
        cmd_format_size(usable_memory_bytes, text, sizeof text);
        draw_field(client, y, "USABLE", text, desktop.theme.chrome_text);
        y += line;
        cmd_format_size(total_memory_bytes, text, sizeof text);
        draw_field(client, y, "MEMORY", text, desktop.theme.chrome_text);
        y += line;
        number_to_text(memory_regions, text, sizeof text);
        draw_field(client, y, "REGIONS", text, desktop.theme.chrome_text);
    } else {
        draw_field(client, y, "MEMORY", "NOT REPORTED", desktop.theme.bar_dim);
    }
    y += line;

    number_to_text(uptime_seconds / 60, text, sizeof text);
    at = 0;
    while (text[at] != '\0') at++;
    text[at++] = 'M';
    text[at++] = ' ';
    number_to_text(uptime_seconds % 60, text + at, sizeof text - at);
    at = 0;
    while (text[at] != '\0') at++;
    text[at++] = 'S';
    text[at] = '\0';
    draw_field(client, y, "UPTIME", text, desktop.theme.chrome_text);
    y += line;

    number_to_text(desktop_visible_count(&desktop), text, sizeof text);
    draw_field(client, y, "TILES", text, desktop.theme.chrome_text);
}

static void paint_about(struct surface *client)
{
    static const char *const lines[] = {
        "ME OS DEFAULT",
        "",
        "TILING FIRST. NORMAL WINDOWS DO NOT",
        "OVERLAP AND DO NOT CHOOSE WHERE THEY",
        "SIT. OPENING OR CLOSING ONE LAYS THE",
        "REST OUT AGAIN.",
        "",
        "CTRL ARROWS   MOVE FOCUS",
        "CTRL H        HIDE THIS WINDOW",
        "CTRL S        SHOW EVERY WINDOW",
        "CTRL N AND W  MOVE THE DIVIDER",
        "CTRL 1 TO 4   GO TO A WORKSPACE",
        "CTRL M        SEND THIS ONE ALONG",
        "",
        "TYPE HELP IN THE TERMINAL.",
    };
    for (uint64_t i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        surface_draw_string(client, lines[i], 8,
                            8 + (int64_t)i * (FONT_HEIGHT + 4),
                            i == 0 ? colour_accent : desktop.theme.chrome_text, 1);
    }
}

/* Paints one app's contents into the client area it has now. Demo is not here
 * because it owns far more state than the others and lays itself out. */
static void paint_app(size_t index)
{
    struct desktop_app *app = desktop_app_at(&desktop, index);
    if (app == NULL || !desktop_on_screen(&desktop, index)) {
        return;
    }
    if (index == 1) {
        paint_system_info(&app->client);
    } else if (index == 2) {
        paint_about(&app->client);
    } else if (index == 3) {
        term_resize(&terminal, term_cols_for(app->client.width),
                    term_rows_for(app->client.height));
        term_draw(&terminal, &app->client, desktop.theme.chrome_text,
                  desktop.theme.window, desktop.theme.accent);
    }
}

static void paint_info_apps(void)
{
    for (size_t i = 1; i < desktop.app_count; i++) {
        if (desktop_on_screen(&desktop, i)) {
            paint_app(i);
        }
    }
}

/* Recomputes the whole environment: tiles, frames, and every app's contents.
 *
 * One function, because those three always change together. A layout that moved
 * the windows without repainting them would show each app's last drawing
 * stretched across a tile it no longer fits. */
static void relayout_desktop(void)
{
    if (!desktop_relayout(&desktop)) {
        fail("the screen is too small to tile the visible windows");
    }
    struct desktop_app *demo =
        desktop_app_at(&desktop, desktop_index_of(&desktop, demo_window_id));
    if (demo == NULL) {
        fail("Demo is not on the desktop");
    }
    demo_surface = demo->client;

    presenting_suppressed = true;
    desktop_paint_frames(&desktop);
    paint_info_apps();
    /* Only when Demo is on the screen. Its surface is emptied when it is not,
     * so this would be a long series of no operations, and laying it out
     * against a surface with no size would leave nonsense behind for when it
     * comes back. */
    if (desktop_on_screen(&desktop, 0)) {
        demo_layout();
        demo_repaint();
    }
    presenting_suppressed = false;

    present_desktop();
    log_layout();
}


/* Repaints only the borders, for a focus change that moved no window.
 *
 * Tiles do not overlap, so focus changes nothing about where anything is. The
 * only pixels that differ are four thin strips on two tiles and the marker on
 * the taskbar, and repainting whole frames to change those would wipe every
 * app's content on every key press. */
static void refresh_focus(void)
{
    const WindowId focused = window_focused(&window_manager);
    for (size_t i = 0; i < desktop.app_count; i++) {
        struct desktop_app *app = desktop_app_at(&desktop, i);
        if (app == NULL || app->hidden) {
            continue;
        }
        shell_focus_border(&app->frame, &desktop.theme,
                           app->id == focused, desktop.layout.border);
    }
    present_desktop();
}

/* The window manager's own keys.
 *
 * Returns true when the key was one of them, so it never reaches the focused
 * app. A shortcut that also types into whatever has focus is a shortcut nobody
 * can use, and the Demo window types every letter into a calculator.
 *
 * Control rather than Super, which is what a tiling desktop would normally use.
 * The keyboard would decode Super perfectly well. Neither QEMU nor VirtualBox
 * reliably delivers it, because the host's own window manager takes it first,
 * and a shortcut that works here and silently does nothing on the next machine
 * is worse than a different shortcut. See M18.
 */
static bool handle_shortcut(const struct kbd_key *key)
{
    if (launcher_open && key->name != NULL && same_name(key->name, "ESCAPE")) {
        close_launcher();
        return true;
    }
    if (!key->ctrl) {
        return false;
    }

    if (key->name != NULL) {
        const bool forward = same_name(key->name, "RIGHT") ||
                             same_name(key->name, "DOWN");
        const bool backward = same_name(key->name, "LEFT") ||
                              same_name(key->name, "UP");
        if (!forward && !backward) {
            return false;
        }
        if (desktop_focus_step(&desktop, forward ? 1 : -1)) {
            log_str("me-os: focus moved to window ");
            log_dec(window_focused(&window_manager));
            log_str("\n");
            refresh_focus();
        }
        return true;
    }

    /* A digit goes to that workspace. One key each, which is why there are four
     * of them and not more. */
    if (key->ch >= '1' && key->ch <= '0' + DESKTOP_WORKSPACES) {
        if (desktop_switch_workspace(&desktop, key->ch - '0')) {
            log_str("me-os: workspace ");
            log_dec((uint64_t)desktop.workspace);
            log_str("\n");
            relayout_desktop();
        }
        return true;
    }

    switch (key->ch) {
    case 'H':
        /* Out of the layout, still on the taskbar. The others grow into the
         * space it leaves, which is the whole reason to hide one. */
        if (desktop_set_hidden(&desktop, window_focused(&window_manager), true)) {
            log_stage("window hidden, the layout reflowed");
            relayout_desktop();
        }
        return true;

    case 'S': {
        bool any = false;
        for (size_t i = 0; i < desktop.app_count; i++) {
            const struct desktop_app *app = desktop_app_at(&desktop, i);
            if (app != NULL && app->hidden &&
                desktop_set_hidden(&desktop, app->id, false)) {
                any = true;
            }
        }
        if (any) {
            log_stage("hidden windows shown, the layout reflowed");
            relayout_desktop();
        }
        return true;
    }

    case 'M': {
        /* Sends the focused window to the next workspace and stays where you
         * are. Moving a window away and following it are two different wishes,
         * and this is the one that lets a workspace be cleared. */
        const int64_t next = desktop.workspace % DESKTOP_WORKSPACES + 1;
        if (desktop_move_to_workspace(&desktop, window_focused(&window_manager),
                                      next)) {
            log_str("me-os: window moved to workspace ");
            log_dec((uint64_t)next);
            log_str("\n");
            relayout_desktop();
        }
        return true;
    }

    /* The divider between the two columns. This is what tile resizing is in a
     * tiling desktop: a proportion, not a window dragged by its corner. */
    case 'N':
    case 'W': {
        const int64_t step = key->ch == 'W' ? 5 : -5;
        const int64_t before = desktop.layout.master_percent;
        desktop.layout.master_percent += step;
        if (desktop.layout.master_percent < 20) desktop.layout.master_percent = 20;
        if (desktop.layout.master_percent > 80) desktop.layout.master_percent = 80;
        if (desktop.layout.master_percent != before) {
            log_str("me-os: master column now ");
            log_dec((uint64_t)desktop.layout.master_percent);
            log_str(" percent\n");
            relayout_desktop();
        }
        return true;
    }

    default:
        /* Anything the desktop does not use goes on to whatever has focus, so an
         * app can have a shortcut of its own. This used to swallow every
         * control combination, which meant no app could ever have one. */
        return false;
    }
}


/* Where the launcher menu is drawn, when it is open. Above the taskbar and
 * against the left edge, which is where the button that opens it is. */
#define LAUNCHER_WIDTH  200
#define LAUNCHER_ROW    22

static struct region launcher_region(void)
{
    if (!launcher_open) {
        return region_none();
    }
    const int64_t height = (int64_t)(desktop.app_count + 1) * LAUNCHER_ROW + 12;
    return region_make(6,
                       (int64_t)fb_height() - desktop.layout.bottom_bar - height,
                       LAUNCHER_WIDTH, height);
}

/* The menu behind the ME OS mark. One entry per window, saying whether it is
 * showing, plus a line about the machine. Small on purpose: a searchable
 * launcher is a milestone of its own and this one has to be honest about what
 * it can do. */
static void draw_launcher(void)
{
    const struct region where = launcher_region();
    if (region_empty(&where)) {
        return;
    }
    surface_fill_rect(&desktop_surface, where.x, where.y,
                      (uint32_t)where.width, (uint32_t)where.height,
                      desktop.theme.chrome);
    surface_fill_rect(&desktop_surface, where.x, where.y,
                      (uint32_t)where.width, 2, desktop.theme.accent);

    surface_draw_string(&desktop_surface, "ME OS " ME_OS_VERSION,
                        where.x + 8, where.y + 8, desktop.theme.accent, 1);

    for (size_t i = 0; i < desktop.app_count; i++) {
        const struct desktop_app *app = desktop_app_at(&desktop, i);
        const int64_t y = where.y + 8 + (int64_t)(i + 1) * LAUNCHER_ROW;
        surface_draw_string(&desktop_surface, app->title, where.x + 8, y,
                            app->hidden ? desktop.theme.bar_dim
                                        : desktop.theme.bar_text, 1);
        surface_draw_string(&desktop_surface, app->hidden ? "OPEN" : "SHOWN",
                            where.x + LAUNCHER_WIDTH - 6 * FONT_WIDTH, y,
                            app->hidden ? desktop.theme.accent
                                        : desktop.theme.bar_dim, 1);
    }
}

/* Which launcher entry a point is on, or DESKTOP_MAX_APPS for none. */
static size_t launcher_entry_at(int64_t x, int64_t y)
{
    const struct region where = launcher_region();
    if (region_empty(&where) || x < where.x || x >= where.x + where.width) {
        return DESKTOP_MAX_APPS;
    }
    for (size_t i = 0; i < desktop.app_count; i++) {
        const int64_t top = where.y + 8 + (int64_t)(i + 1) * LAUNCHER_ROW;
        if (y >= top - 4 && y < top + FONT_HEIGHT + 4) {
            return i;
        }
    }
    return DESKTOP_MAX_APPS;
}

static void close_launcher(void)
{
    if (!launcher_open) {
        return;
    }
    const struct region where = launcher_region();
    launcher_open = false;
    present_region(where);
}

/* A press on a tile's own title bar, in that window's coordinates.
 *
 * The buttons are found through the same two functions that drew them, so a
 * click can never land somewhere a button was not painted. */
static bool title_bar_press(WindowId id, int64_t local_x, int64_t local_y)
{
    const struct window *window = window_get_const(&window_manager, id);
    if (window == NULL) {
        return false;
    }
    const int64_t border = desktop.layout.border;
    if (local_y < border || local_y >= border + SHELL_TITLE_HEIGHT) {
        return false;
    }

    struct tile_area button;
    if (shell_close_button(window->geometry.width, border, &button) &&
        local_x >= button.x && local_x < button.x + button.width) {
        /* Close hides the window and leaves it on the taskbar and in the
         * launcher. Nothing here can free a window and build it again from
         * nothing, and a close that lost an app for the rest of the run would
         * be worse than one that puts it away. Said plainly in the log. */
        if (desktop_set_hidden(&desktop, id, true)) {
            log_stage("window closed to the taskbar, the layout reflowed");
            relayout_desktop();
        }
        return true;
    }
    if (shell_hide_button(window->geometry.width, border, &button) &&
        local_x >= button.x && local_x < button.x + button.width) {
        if (desktop_set_hidden(&desktop, id, true)) {
            log_stage("window hidden, the layout reflowed");
            relayout_desktop();
        }
        return true;
    }
    return false;
}

/* Everything a press on the desktop itself can mean: the launcher, a taskbar
 * button, or a tile's own title bar. Returns true when it was one of them, so
 * the press is not also delivered to an app as an ordinary click. */
static bool desktop_press(int64_t x, int64_t y)
{
    if (launcher_open) {
        const size_t entry = launcher_entry_at(x, y);
        close_launcher();
        if (entry < desktop.app_count) {
            const struct desktop_app *app = desktop_app_at(&desktop, entry);
            const WindowId id = app->id;
            bool moved = desktop_set_hidden(&desktop, id, false);
            moved = desktop_switch_workspace(&desktop, app->workspace) || moved;
            if (moved) {
                relayout_desktop();
            }
            window_focus(&window_manager, id, false);
            refresh_focus();
            return true;
        }
        /* A click anywhere else closes the menu and does nothing more, which is
         * what a menu does everywhere else. */
        return true;
    }

    const struct tile_area launcher =
        shell_launcher_button((int64_t)fb_height(), desktop.layout.bottom_bar);
    if (x >= launcher.x && x < launcher.x + launcher.width &&
        y >= launcher.y && y < launcher.y + launcher.height) {
        launcher_open = true;
        present_desktop();
        return true;
    }

    const size_t task = desktop_taskbar_hit(&desktop, x, y);
    if (task < desktop.app_count) {
        const struct desktop_app *app = desktop_app_at(&desktop, task);
        const WindowId id = app->id;
        /* One button doing the obvious thing whatever state the window is in.
         * A hidden one comes back, one on another workspace takes you there,
         * and one already on screen simply takes focus. */
        bool moved = desktop_set_hidden(&desktop, id, false);
        moved = desktop_switch_workspace(&desktop, app->workspace) || moved;
        if (moved) {
            relayout_desktop();
        }
        window_focus(&window_manager, id, false);
        refresh_focus();
        return true;
    }

    const WindowId hit = window_hit_test(&window_manager, x, y);
    if (hit != WINDOW_ID_NONE) {
        const struct window *window = window_get_const(&window_manager, hit);
        if (window != NULL &&
            title_bar_press(hit, x - window->geometry.x, y - window->geometry.y)) {
            return true;
        }
    }
    return false;
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

    /* M13 window objects become the M18 tiles. Nothing is given a position
     * here: the layout decides, and it decides again every time a window is
     * hidden or shown. */
    window_manager_init(&window_manager);
    colour_background = fb_rgb(17, 20, 25);   /* the theme's window ground */
    colour_text = fb_rgb(255, 255, 255);
    colour_rect = fb_rgb(60, 170, 220);
    colour_cursor = fb_rgb(255, 214, 64);
    colour_triangle = fb_rgb(80, 220, 120);
    colour_accent = fb_rgb(72, 214, 224);

    if (!desktop_init(&desktop, &window_manager,
                      (int64_t)fb_width(), (int64_t)fb_height(),
                      tile_arena, TILE_ARENA_PIXELS, fb_rgb)) {
        fail("could not start the ME OS desktop");
    }

    static const char *const titles[DESKTOP_MAX_APPS] = {
        "DEMO", "SYSTEM INFO", "ABOUT ME OS", "TERMINAL", "EDITOR",
    };
    for (size_t i = 0; i < DESKTOP_MAX_APPS; i++) {
        if (desktop_add(&desktop, titles[i]) != i) {
            fail("could not create the M18 windows");
        }
    }
    demo_window_id = desktop_app_at(&desktop, 0)->id;
    terminal_window_id = desktop_app_at(&desktop, 3)->id;
    editor_window_id = desktop_app_at(&desktop, 4)->id;
    editor_init(&text_editor);
    editor_set_status(&text_editor, "CTRL O SAVES");

    /* The clock, once at boot. It is read again on the tick that redraws the
     * bar, so what is on screen is never more than a second old. */
    clock_answered = rtc_read(&clock_time);
    if (clock_answered) {
        rtc_format_date(&clock_time, clock_date, sizeof clock_date);
        rtc_format_time(&clock_time, clock_time_text, sizeof clock_time_text);
        log_str("me-os: clock says ");
        log_str(clock_date);
        log_str(" ");
        log_str(clock_time_text);
        log_str("\n");
    } else {
        log_stage("the clock chip would not answer, so the bar shows uptime");
    }

    /* Asked of the machine before anything reports it. */
    cpu_vendor(cpu_vendor_text, sizeof cpu_vendor_text);
    cpu_brand(cpu_brand_text, sizeof cpu_brand_text);
    read_memory_map();
    /* The disk first, because what is on it decides whether the tree below gets
     * built at all. A machine that has been used before comes back as it was,
     * and only a machine that has not gets the starting one. */
    find_the_disk();
    vfs_init(&filesystem);

    /* What was here last time, if there is a disk and it holds ours.
     *
     * Anything else falls through to the tree below. A disk that is blank, or
     * somebody else's, or written by a build with different limits, is not a
     * reason to refuse to start: it is a reason to start with nothing on it and
     * say so. The one thing not done here is writing over it, because a disk
     * this build cannot read may still be one somebody wants. */
    const enum vfsdisk_result opened = vfsdisk_load(&filesystem, &storage);
    if (opened == VFSDISK_OK) {
        log_str("me-os: filesystem loaded from disk, ");
        log_dec(vfs_used_nodes(&filesystem));
        log_str(" entries in ");
        log_dec(vfs_used_blocks(&filesystem));
        log_str(" blocks\n");
        /* What is actually on it, not only how much. A count proves a disk was
         * read. Names prove it is the disk this machine wrote, which is the
         * thing worth knowing after a restart. */
        log_str("me-os: disk holds");
        for (int16_t child = vfs_first_child(&filesystem, 0); child != VFS_NONE;
             child = vfs_next_sibling(&filesystem, child)) {
            log_str(" ");
            log_str(vfs_get(&filesystem, child)->name);
        }
        log_str("\n");
        log_largest_file();
        /* Where the last session was standing is not saved, so start somewhere
         * that is certainly there. */
        if (vfs_chdir(&filesystem, "/HOME") != VFS_OK) {
            (void)vfs_chdir(&filesystem, "/");
        }
    } else {
        if (disk_present(&storage)) {
            log_str("me-os: nothing loaded from the disk: ");
            log_str(vfsdisk_explain(opened));
            log_str("\n");
        }
        if (vfs_mkdir(&filesystem, "/HOME") != VFS_OK ||
            vfs_mkdir(&filesystem, "/DOCS") != VFS_OK ||
            vfs_mkdir(&filesystem, "/TMP") != VFS_OK ||
            vfs_write(&filesystem, "/DOCS/README.TXT",
                      "ME OS " ME_OS_VERSION ". A TILING DESKTOP AND A SHELL.") != VFS_OK ||
            vfs_write(&filesystem, "/DOCS/KEYS.TXT",
                      "CTRL ARROWS MOVE FOCUS. CTRL H HIDES. CTRL S SHOWS ALL.") != VFS_OK ||
            /* Deliberately longer than one block. A file that fits in a single
             * block would never show whether the second one was written, read
             * back, or joined on in the right order. */
            vfs_write(&filesystem, "/DOCS/GUIDE.TXT",
                      "ME OS KEEPS ITS FILES ON A DISK NOW. ANYTHING YOU MAKE HERE IS STILL "
                  "HERE THE NEXT TIME THE MACHINE STARTS, AND THERE IS NOTHING TO "
                  "TYPE TO MAKE THAT HAPPEN. THE SHELL SAVES AFTER EVERY COMMAND "
                  "THAT CHANGED SOMETHING, AND THE EDITOR SAVES WHEN YOU PRESS "
                  "CTRL O.\n"
                  "\n"
                  "A FILE IS MADE OF BLOCKS OF 512 BYTES, TAKEN FROM A POOL THAT "
                  "EVERY FILE SHARES. TYPE DF TO SEE HOW MUCH OF IT IS LEFT. ONE "
                  "FILE CAN HOLD TWELVE BLOCKS, WHICH IS MORE THAN THE EDITOR CAN "
                  "HOLD, SO ANYTHING YOU CAN TYPE IN THE EDITOR IS SOMETHING YOU "
                  "CAN SAVE.\n"
                  "\n"
                  "THIS FILE IS ITSELF LONGER THAN ONE BLOCK, WHICH IS WHY IT IS "
                  "HERE. IF YOU CAN READ ALL OF IT AFTER A RESTART THEN THE PARTS "
                  "THAT MATTER ARE WORKING.") != VFS_OK ||
            vfs_chdir(&filesystem, "/HOME") != VFS_OK) {
            fail("could not lay out the filesystem");
        }
        log_largest_file();
        /* Onto the disk straight away, so a fresh machine has a filesystem on
         * it rather than one that only appears after the first command. */
        save_the_filesystem();
    }

    term_init(&terminal, TERM_MAX_COLS, TERM_MAX_ROWS);
    term_println(&terminal, "ME OS " ME_OS_VERSION);
    term_println(&terminal, "TYPE HELP FOR A LIST OF COMMANDS.");
    term_newline(&terminal);
    term_set_prompt(&terminal, "/HOME > ");

    /* Demo alone to begin with. ME OS Default is tiling first, and one window
     * filling the workspace is what that layout does with one window: the tiling
     * is in the rule, not in how many tiles happen to be on screen. The other
     * three are on the taskbar and Ctrl+S brings them in, which is what makes
     * the two, three and four tile layouts reachable without a launcher. */
    for (size_t i = 1; i < desktop.app_count; i++) {
        if (!desktop_set_hidden(&desktop, desktop_app_at(&desktop, i)->id, true)) {
            fail("could not start with only Demo showing");
        }
    }

    log_str("me-os: window model ready, created IDs ");
    for (size_t i = 0; i < desktop.app_count; i++) {
        if (i > 0) {
            log_str(", ");
        }
        log_dec(desktop_app_at(&desktop, i)->id);
    }
    log_str("\n");

    if (fb_width() > DESKTOP_MAX_WIDTH || fb_height() > DESKTOP_MAX_HEIGHT ||
        !surface_init(&desktop_surface, desktop_pixels,
                      DESKTOP_MAX_WIDTH * DESKTOP_MAX_HEIGHT,
                      (uint32_t)fb_width(), (uint32_t)fb_height())) {
        fail("could not create the M14 software surfaces");
    }
    if (!window_focus(&window_manager, demo_window_id, false)) {
        fail("could not focus Demo for M15 input routing");
    }

    /* The calculator has to exist before Demo paints, because the sum line is
     * one of the things it paints. */
    vars_reset(&vars_state);
    calc_init(&calc_state, &vars_state);
    rect_state.speed = M5_RECT_SPEED;
    rect_state.direction = 1;
    rect_state.carried = 0;

    /* M12: floating point before anything in geometry.c runs, because the
     * processor starts with those instructions disabled. If it will not do SSE
     * the kernel carries on without a triangle rather than faulting. */
    if (!fpu_init()) {
        log_stage("no SSE, running without the M12 triangle");
    }

    /* Everything above only described the desktop. This is what puts it on the
     * screen: tiles, frames, bars and every app's contents. */
    relayout_desktop();
    log_stage("M14 compositor ready with tiled window surfaces");
    log_stage("M18 ME OS Default desktop ready, tiling first");

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

        if (kbd_poll(&key) && !handle_shortcut(&key)) {
            const struct window_event event = {
                .type = WINDOW_EVENT_KEY_DOWN,
                .data.key = {
                    .ch = key.ch,
                    .code = window_key_code_for(&key),
                    .ctrl = key.ctrl,
                },
            };
            window_route_key(&window_manager, &event);
            /* Both, because routing puts the event in the focused window's own
             * queue and only that window's drain will find it. An app whose
             * queue is empty does nothing, which is what an unfocused app
             * should do with a key press. */
            drain_demo_events();
            drain_terminal_events();
            drain_editor_events();
        }

        /* One reading of the clock, shared by everything that moves, so the
         * rectangle and the triangle cannot disagree about how much time has
         * passed. */
        const uint64_t elapsed = timer_poll();

        /* The top bar's uptime. Counted from the same clock as everything else,
         * and only redrawn when the second changes, so a bar that says the time
         * does not become a reason to repaint the screen 60 times a second. */
        timer_carried += elapsed;
        if (timer_carried >= TIMER_HZ) {
            uptime_seconds += timer_carried / TIMER_HZ;
            timer_carried %= TIMER_HZ;
            /* Read again rather than counted up from the boot reading. The
             * chip is the clock; anything else here would be a second one that
             * could drift away from it. */
            if (clock_answered && rtc_read(&clock_time)) {
                rtc_format_time(&clock_time, clock_time_text, sizeof clock_time_text);
                rtc_format_date(&clock_time, clock_date, sizeof clock_date);
            }
            present_region(desktop_top_bar_region(&desktop));
            /* System Info reports the uptime, so it is redrawn on the same tick
             * as the bar that reports it. A window showing a clock that stopped
             * at boot is worse than one with no clock in it. */
            struct desktop_app *info = desktop_app_at(&desktop, 1);
            if (info != NULL && desktop_on_screen(&desktop, 1)) {
                surface_fill_rect(&info->client, 0, 0, info->client.width,
                                  info->client.height, desktop.theme.window);
                paint_app(1);
                present_region(desktop_client_region(
                    &desktop, info->id, 0, 0, (int64_t)info->client.width,
                    (int64_t)info->client.height));
            }
            /* Once a second, where the drifting rectangle has reached. The M5
             * drift means its position is a function of how long the machine has
             * been up, so a test that wants to click on it cannot work the place
             * out in advance. This is the kernel saying where it is, which is
             * something only the kernel knows. */
            log_str("me-os: rectangle at ");
            log_dec((uint64_t)rect_state.x);
            log_str(",");
            log_dec((uint64_t)rect_state.y);
            log_str(" size ");
            log_dec(rect_state.width);
            log_str("x");
            log_dec(rect_state.height);
            log_str("\n");
        }

        /* Demo animates only while it is on the screen. Its surface is emptied
         * when it is not, so drawing would be harmless, and there is no reason
         * to spend the time working out a picture nobody can see. */
        if (desktop_on_screen(&desktop, 0)) {
            if (rect_advance(&rect_state, elapsed, TIMER_HZ, demo_surface.width)) {
                draw_rect();
            }
            if (fpu_ready() && triangle_advance(elapsed, TIMER_HZ)) {
                redraw_triangle();
            }
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
        bool handled_by_desktop = false;
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
                /* The desktop looks first: the launcher, the taskbar and a
                 * tile's own title bar belong to the window manager, not to the
                 * app inside the tile. Anything it does not claim goes on to the
                 * window under the pointer as an ordinary click. */
                if (desktop_press(pointer_state.x, pointer_state.y)) {
                    handled_by_desktop = true;
                } else {
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
            drain_terminal_events();
            drain_editor_events();

            if (focus_changed || handled_by_desktop) {
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
