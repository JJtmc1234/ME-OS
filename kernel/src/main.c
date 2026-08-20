#include <stddef.h>
#include <stdint.h>

#include "fb.h"
#include "font.h"
#include "limine.h"

#define M1_MESSAGE "IF YOU SEE THIS IT WORKED"

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

/* QEMU's debug console. On real hardware this port is inert, so it is safe
 * to write to unconditionally and it is the only diagnostic channel we have
 * before the framebuffer works. */
static void debug_print(const char *s)
{
    for (; *s != '\0'; s++) {
        __asm__ volatile ("outb %0, %1" :: "a"(*s), "Nd"((uint16_t)0xE9));
    }
}

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
    debug_print("ME OS M1 failed: ");
    debug_print(reason);
    debug_print("\n");
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
    uint64_t scale = (screen_w * 3) / (4 * text_chars * FONT_WIDTH);

    if (scale < 1) {
        scale = 1;
    } else if (scale > 16) {
        scale = 16;
    }
    return scale;
}

void kmain(void)
{
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

    const uint32_t black = fb_rgb(0, 0, 0);
    const uint32_t white = fb_rgb(255, 255, 255);

    fb_clear(black);

    const uint64_t chars = str_len(M1_MESSAGE);
    const uint64_t scale = pick_scale(fb_width(), chars);
    const uint64_t text_w = chars * FONT_WIDTH * scale;
    const uint64_t text_h = FONT_HEIGHT * scale;

    const uint64_t x = text_w < fb_width() ? (fb_width() - text_w) / 2 : 0;
    const uint64_t y = text_h < fb_height() ? (fb_height() - text_h) / 2 : 0;

    fb_draw_string(M1_MESSAGE, x, y, white, scale);

    debug_print("ME OS M1 drew the message\n");
    halt();
}
