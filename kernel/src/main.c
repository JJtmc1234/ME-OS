/* ME OS kernel entry.
 *
 * M1: boot over UEFI and draw a fixed message.
 * M2: read the keyboard and show the last key pressed, without disturbing
 *     the M1 message.
 * M3: draw one static rectangle, without disturbing either.
 * M4: show a mouse cursor that moves, without disturbing any of them.
 * M5: move the rectangle across the screen over time, still without
 *     disturbing any of them.
 * M6: add and subtract whole numbers typed on the keyboard, and show the
 *     result, still without disturbing any of them.
 *
 * Everything is drawn directly to the framebuffer. There is no console, no
 * scrolling, and no input buffer, on purpose: each milestone adds one small
 * capability that can be demonstrated on its own.
 */
#include <stddef.h>
#include <stdint.h>

#include "calc.h"
#include "cursor.h"
#include "fb.h"
#include "font.h"
#include "kbd.h"
#include "limine.h"
#include "log.h"
#include "mouse.h"
#include "pointer.h"
#include "rect.h"
#include "timer.h"

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

/* M4: where the cursor starts. Clear of the message, the key line and the
 * rectangle, so it never has to overlap them just by existing.
 * tests/check_boot.py mirrors these divisors. */
#define M4_CURSOR_START_X_DIVISOR 4
#define M4_CURSOR_START_Y_DIVISOR 6

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
static struct calc calc_state;
static uint32_t colour_cursor;
static struct pointer pointer_state;
static struct moving_rect rect_state;

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
    return width < fb_width() ? (fb_width() - width) / 2 : 0;
}

/* Replaces the key line. The whole line is cleared first, so a shorter
 * message cannot leave the tail of a longer one behind. */
static void draw_key_line(const char *text)
{
    const uint64_t height = FONT_HEIGHT * key_line_scale;
    const bool had_cursor = cursor_visible();

    /* The cursor holds a copy of the pixels underneath it. Drawing over them
     * while it is up would make that copy stale, and hiding it later would
     * smear the old line back over the new one. */
    if (had_cursor) {
        cursor_hide();
    }

    fb_fill_rect(0, key_line_y, fb_width(), height, colour_background);
    fb_draw_string(text, centred_x(str_len(text), key_line_scale),
                   key_line_y, colour_text, key_line_scale);

    if (had_cursor) {
        cursor_show((uint64_t)pointer_state.x, (uint64_t)pointer_state.y,
                    colour_cursor, colour_background);
    }
}

/* Erases the rectangle's old row and draws it at its current position. Only
 * the strip the rectangle lives in is touched, and it sits below every line of
 * text, so nothing else has to be redrawn. */
static void draw_rect(void)
{
    const bool had_cursor = cursor_visible();

    if (had_cursor) {
        cursor_hide();
    }

    fb_fill_rect(0, (uint64_t)rect_state.y, fb_width(), rect_state.height,
                 colour_background);
    fb_fill_rect((uint64_t)rect_state.x, (uint64_t)rect_state.y,
                 rect_state.width, rect_state.height, colour_rect);

    if (had_cursor) {
        cursor_show((uint64_t)pointer_state.x, (uint64_t)pointer_state.y,
                    colour_cursor, colour_background);
    }
}

/* Redraws the sum line. Same rule as every other drawing routine: the cursor
 * holds a copy of the pixels underneath it, so it comes down first. */
static void draw_sum_line(void)
{
    char line[CALC_MAX_INPUT + CALC_MAX_NUMBER + 2];
    const uint64_t height = FONT_HEIGHT * key_line_scale;
    const bool had_cursor = cursor_visible();

    calc_line(&calc_state, line, sizeof line);

    if (had_cursor) {
        cursor_hide();
    }

    fb_fill_rect(0, sum_line_y, fb_width(), height, colour_background);
    fb_draw_string(line, centred_x(str_len(line), key_line_scale),
                   sum_line_y, colour_text, key_line_scale);

    if (had_cursor) {
        cursor_show((uint64_t)pointer_state.x, (uint64_t)pointer_state.y,
                    colour_cursor, colour_background);
    }
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

    colour_background = fb_rgb(0, 0, 0);
    colour_text = fb_rgb(255, 255, 255);
    colour_rect = fb_rgb(60, 170, 220);
    colour_cursor = fb_rgb(255, 214, 64);
    fb_clear(colour_background);

    /* M1: the message, centred, exactly as the milestone specifies. */
    const uint64_t chars = str_len(M1_MESSAGE);
    const uint64_t scale = pick_scale(fb_width(), chars);
    const uint64_t text_h = FONT_HEIGHT * scale;
    const uint64_t y = text_h < fb_height() ? (fb_height() - text_h) / 2 : 0;

    fb_draw_string(M1_MESSAGE, centred_x(chars, scale), y, colour_text, scale);
    log_stage("drew the M1 message");

    /* M2: one line below, left blank until a key arrives. */
    key_line_scale = scale;
    key_line_y = y + text_h * 2;
    if (key_line_y + FONT_HEIGHT * key_line_scale >= fb_height()) {
        key_line_scale = 1;
        key_line_y = y + text_h + FONT_HEIGHT;
    }
    draw_key_line(M2_PROMPT);

    /* M3: a filled rectangle below the key line, or above the message if the
     * screen is too short for it to fit underneath. */
    const uint64_t rect_w = fb_width() / M3_RECT_WIDTH_DIVISOR;
    const uint64_t rect_h = fb_height() / M3_RECT_HEIGHT_DIVISOR;
    const uint64_t rect_x = rect_w < fb_width() ? (fb_width() - rect_w) / 2 : 0;
    uint64_t rect_y = key_line_y + FONT_HEIGHT * key_line_scale * 2;

    if (rect_y + rect_h >= fb_height()) {
        rect_y = y > rect_h * 2 ? y - rect_h * 2 : 0;
    }

    rect_state.x = (int64_t)rect_x;
    rect_state.y = (int64_t)rect_y;
    rect_state.width = rect_w;
    rect_state.height = rect_h;
    rect_state.speed = M5_RECT_SPEED;
    rect_state.direction = 1;
    rect_state.carried = 0;

    fb_fill_rect(rect_x, rect_y, rect_w, rect_h, colour_rect);
    log_str("me-os: drew the M3 rectangle ");
    log_dec(rect_w);
    log_str("x");
    log_dec(rect_h);
    log_str(" at ");
    log_dec(rect_x);
    log_str(",");
    log_dec(rect_y);
    log_str("\n");

    /* M6: the sum line, above the message, in the emptier half of the screen. */
    sum_line_y = y > FONT_HEIGHT * key_line_scale * M6_LINE_GAP
        ? y - FONT_HEIGHT * key_line_scale * M6_LINE_GAP
        : 0;
    calc_reset(&calc_state);
    draw_sum_line();
    log_stage("drew the M6 sum line");

    kbd_init();
    log_stage("keyboard ready, waiting for keys");

    /* M4: a cursor the pointer state drives. No dragging, nothing follows it,
     * and it moves nothing on the screen. */
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

    /* M5: a clock, so the rectangle moves at a rate rather than at whatever
     * speed this machine happens to run the loop. */
    timer_init();
    timer_poll();  /* discard the interval before the clock was started */
    log_stage("timer ready, the M5 rectangle is moving");

    cursor_show((uint64_t)pointer_state.x, (uint64_t)pointer_state.y,
                colour_cursor, colour_background);
    log_str("me-os: drew the M4 cursor at ");
    log_dec((uint64_t)pointer_state.x);
    log_str(",");
    log_dec((uint64_t)pointer_state.y);
    log_str("\n");

    for (;;) {
        struct kbd_key key;
        struct mouse_delta movement;

        if (kbd_poll(&key)) {
            show_key(&key);

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

        if (rect_advance(&rect_state, timer_poll(), TIMER_HZ, fb_width())) {
            draw_rect();
        }

        if (mouse_poll(&movement)) {
            if (pointer_move(&pointer_state, movement.dx, movement.dy,
                             fb_width(), fb_height())) {
                cursor_hide();
                cursor_show((uint64_t)pointer_state.x, (uint64_t)pointer_state.y,
                            colour_cursor, colour_background);
            }
        }
        /* Hints to the processor that this is a spin loop. Interrupts are
         * masked, so hlt would never wake up again. */
        __asm__ volatile ("pause");
    }
}
