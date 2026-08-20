/* ME OS kernel entry.
 *
 * M1: boot over UEFI and draw a fixed message.
 * M2: read the keyboard and show the last key pressed, without disturbing
 *     the M1 message.
 * M3: draw one static rectangle, without disturbing either.
 *
 * Everything is drawn directly to the framebuffer. There is no console, no
 * scrolling, and no input buffer, on purpose: each milestone adds one small
 * capability that can be demonstrated on its own.
 */
#include <stddef.h>
#include <stdint.h>

#include "fb.h"
#include "font.h"
#include "kbd.h"
#include "limine.h"
#include "log.h"

#define M1_MESSAGE  "IF YOU SEE THIS IT WORKED"
#define M2_PROMPT   "PRESS A KEY"
#define M2_PREFIX   "LAST KEY "

/* M3: one static rectangle. Its size is a fraction of the screen so it stays
 * clearly visible at any resolution. tests/check_boot.py mirrors these two
 * numbers, so change them in both places or the check will fail. */
#define M3_RECT_WIDTH_DIVISOR  4
#define M3_RECT_HEIGHT_DIVISOR 14

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

    fb_fill_rect(0, key_line_y, fb_width(), height, colour_background);
    fb_draw_string(text, centred_x(str_len(text), key_line_scale),
                   key_line_y, colour_text, key_line_scale);
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

    kbd_init();
    log_stage("keyboard ready, waiting for keys");

    for (;;) {
        struct kbd_key key;

        if (kbd_poll(&key)) {
            show_key(&key);
        }
        /* Hints to the processor that this is a spin loop. Interrupts are
         * masked, so hlt would never wake up again. */
        __asm__ volatile ("pause");
    }
}
