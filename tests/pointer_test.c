/* Host unit test for mouse packet decoding and pointer movement.
 *
 * Both are pure functions on purpose, so the parts of M4 most likely to be
 * wrong can be checked on an ordinary machine in milliseconds, without an
 * emulator and without a mouse.
 */
#include <stdio.h>
#include <string.h>

#include "mouse.h"
#include "pointer.h"

#define SCREEN_W 1280
#define SCREEN_H 800

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

static int decodes(uint8_t flags, uint8_t dx, uint8_t dy, struct mouse_delta *out)
{
    const uint8_t packet[3] = { flags, dx, dy };
    if (out != NULL) {
        memset(out, 0, sizeof *out);
    }
    return mouse_decode(packet, out);
}

static void test_packet_decoding(void)
{
    struct mouse_delta d;

    printf("mouse_decode reads a movement packet\n");

    check(decodes(0x08, 5, 7, &d) && d.dx == 5 && d.dy == -7,
          "right and up: x positive, y negative because the screen counts down");

    check(decodes(0x08, 0, 0, &d) && d.dx == 0 && d.dy == 0,
          "no movement");

    /* 0x10 is the x sign bit, so 0xFB means -5. */
    check(decodes(0x18, 0xFB, 0, &d) && d.dx == -5 && d.dy == 0,
          "negative x");

    /* 0x20 is the y sign bit: -3 from the device means 3 down the screen. */
    check(decodes(0x28, 0, 0xFD, &d) && d.dx == 0 && d.dy == 3,
          "negative y becomes downward movement");

    check(decodes(0x38, 0xFF, 0xFF, &d) && d.dx == -1 && d.dy == 1,
          "both axes negative");

    check(decodes(0x08, 255, 0, &d) && d.dx == 255,
          "a large positive step is not mistaken for a negative one");

    printf("mouse_decode rejects packets it cannot trust\n");
    check(!decodes(0x00, 5, 5, &d), "sync bit missing");
    check(!decodes(0x48, 5, 5, &d), "x counter overflowed");
    check(!decodes(0x88, 5, 5, &d), "y counter overflowed");
    check(!decodes(0xC8, 5, 5, &d), "both counters overflowed");
    check(!mouse_decode(NULL, &d), "null packet");
    check(!decodes(0x08, 1, 1, NULL), "null output");

    printf("mouse_decode reports the buttons\n");
    check(decodes(0x09, 0, 0, &d) && d.left && !d.right && !d.middle, "left button");
    check(decodes(0x0A, 0, 0, &d) && d.right && !d.left, "right button");
    check(decodes(0x0C, 0, 0, &d) && d.middle, "middle button");
    check(decodes(0x0B, 0, 0, &d) && d.left && d.right, "two buttons at once");
}

static void test_pointer_movement(void)
{
    struct pointer p;

    printf("pointer_init places the pointer inside the screen\n");
    pointer_init(&p, 100, 200, SCREEN_W, SCREEN_H);
    check(p.x == 100 && p.y == 200, "an ordinary starting point");

    pointer_init(&p, -50, 5000, SCREEN_W, SCREEN_H);
    check(p.x == 0 && p.y == SCREEN_H - 1, "a starting point outside the screen is clamped");

    printf("pointer_move applies movement and clamps to the screen\n");
    pointer_init(&p, 100, 100, SCREEN_W, SCREEN_H);
    check(pointer_move(&p, 10, -20, SCREEN_W, SCREEN_H) && p.x == 110 && p.y == 80,
          "moves by the given amount and reports a change");

    check(!pointer_move(&p, 0, 0, SCREEN_W, SCREEN_H),
          "no movement reports no change");

    pointer_init(&p, 5, 5, SCREEN_W, SCREEN_H);
    pointer_move(&p, -100, -100, SCREEN_W, SCREEN_H);
    check(p.x == 0 && p.y == 0, "stops at the top left corner");

    check(!pointer_move(&p, -100, -100, SCREEN_W, SCREEN_H),
          "pushing further into the corner is not a change");

    pointer_init(&p, SCREEN_W - 5, SCREEN_H - 5, SCREEN_W, SCREEN_H);
    pointer_move(&p, 999, 999, SCREEN_W, SCREEN_H);
    check(p.x == SCREEN_W - 1 && p.y == SCREEN_H - 1, "stops at the bottom right corner");

    printf("pointer functions survive nonsense\n");
    pointer_init(NULL, 1, 1, SCREEN_W, SCREEN_H);
    check(!pointer_move(NULL, 1, 1, SCREEN_W, SCREEN_H), "null pointer state");
    pointer_init(&p, 10, 10, 0, 0);
    check(!pointer_move(&p, 1, 1, 0, 0), "a screen with no size");
}

static void test_a_whole_movement(void)
{
    /* What the kernel actually does: decode a packet, then apply it. */
    struct pointer p;
    struct mouse_delta d;

    printf("decoding and moving together\n");
    pointer_init(&p, 640, 400, SCREEN_W, SCREEN_H);

    decodes(0x08, 20, 10, &d);
    pointer_move(&p, d.dx, d.dy, SCREEN_W, SCREEN_H);
    check(p.x == 660 && p.y == 390, "moving right and up on the screen");

    decodes(0x38, 0xEC, 0xF6, &d);   /* -20, -10 from the device */
    pointer_move(&p, d.dx, d.dy, SCREEN_W, SCREEN_H);
    check(p.x == 640 && p.y == 400, "moving back returns to the start");
}

int main(void)
{
    test_packet_decoding();
    test_pointer_movement();
    test_a_whole_movement();

    if (failures > 0) {
        printf("\n%d pointer check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nmouse and pointer checks passed\n");
    return 0;
}
