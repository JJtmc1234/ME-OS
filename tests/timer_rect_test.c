/* Host unit test for the timer's wrap arithmetic and the moving rectangle.
 *
 * Both are pure functions. Time that jumps backwards once every wrap, or a
 * rectangle that creeps off the screen after ten minutes, are exactly the
 * faults that never show up in a thirty second look at an emulator.
 */
#include <stdio.h>
#include <string.h>

#include "rect.h"
#include "timer.h"

#define SCREEN_W 1280
#define PERIOD   65536u

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

static struct moving_rect make_rect(int64_t x, int32_t direction)
{
    struct moving_rect r;
    memset(&r, 0, sizeof r);
    r.x = x;
    r.y = 500;
    r.width = 320;
    r.height = 57;
    r.speed = 60;          /* pixels per second */
    r.direction = direction;
    return r;
}

static void test_timer_wrap(void)
{
    printf("timer_elapsed_between handles a counter that counts down and wraps\n");

    check(timer_elapsed_between(1000, 900, PERIOD) == 100, "an ordinary step");
    check(timer_elapsed_between(1000, 1000, PERIOD) == 0, "no time passing");

    /* Wrapped: it passed zero and started again from the top. */
    check(timer_elapsed_between(100, 65500, PERIOD) == 100u + PERIOD - 65500u,
          "a step across the wrap");
    check(timer_elapsed_between(0, 1, PERIOD) == PERIOD - 1, "wrapping from zero");
    check(timer_elapsed_between(65535, 0, PERIOD) == 65535, "reaching zero exactly");

    printf("elapsed time never goes backwards\n");
    unsigned long long total = 0;
    uint16_t previous = 40000;
    for (int i = 0; i < 5000; i++) {
        uint16_t current = (uint16_t)((previous - 37u) & 0xFFFF);
        unsigned long long step = timer_elapsed_between(previous, current, PERIOD);
        if (step != 37u) {
            printf("  FAIL  step %d measured %llu counts, expected 37\n", i, step);
            failures++;
            break;
        }
        total += step;
        previous = current;
    }
    check(total == 5000ull * 37ull, "five thousand steps across many wraps add up");
}

static void test_rect_movement(void)
{
    printf("rect_advance moves at its stated speed\n");

    struct moving_rect r = make_rect(100, 1);
    rect_advance(&r, TIMER_HZ, TIMER_HZ, SCREEN_W);       /* exactly one second */
    check(r.x == 160, "one second at sixty pixels a second");

    r = make_rect(100, -1);
    rect_advance(&r, TIMER_HZ / 2, TIMER_HZ, SCREEN_W);   /* half a second */
    check(r.x == 70, "half a second leftwards");

    printf("small steps add up to the same distance as one big one\n");
    struct moving_rect many = make_rect(100, 1);
    for (int i = 0; i < 1000; i++) {
        rect_advance(&many, TIMER_HZ / 1000, TIMER_HZ, SCREEN_W);
    }
    struct moving_rect once = make_rect(100, 1);
    rect_advance(&once, (TIMER_HZ / 1000) * 1000, TIMER_HZ, SCREEN_W);
    check(many.x == once.x, "a thousand small steps land where one large one does");

    printf("nothing moves when nothing should\n");
    r = make_rect(100, 1);
    check(!rect_advance(&r, 0, TIMER_HZ, SCREEN_W) && r.x == 100, "no time passed");
    check(!rect_advance(&r, 10, TIMER_HZ, SCREEN_W) && r.x == 100,
          "too little time for a whole pixel");
    r.speed = 0;
    check(!rect_advance(&r, TIMER_HZ, TIMER_HZ, SCREEN_W) && r.x == 100, "no speed");
    check(!rect_advance(NULL, TIMER_HZ, TIMER_HZ, SCREEN_W), "null rectangle");
    r = make_rect(100, 1);
    check(!rect_advance(&r, TIMER_HZ, 0, SCREEN_W), "a timer with no frequency");
}

static void test_rect_stays_on_screen(void)
{
    const int64_t limit = SCREEN_W - 320;

    printf("rect_advance reflects off both edges\n");

    struct moving_rect r = make_rect(limit - 10, 1);
    rect_advance(&r, TIMER_HZ, TIMER_HZ, SCREEN_W);   /* 60 pixels into the wall */
    check(r.x <= limit && r.direction == -1, "turns around at the right edge");
    check(r.x == limit - 50, "reflects rather than sticking");

    r = make_rect(10, -1);
    rect_advance(&r, TIMER_HZ, TIMER_HZ, SCREEN_W);
    check(r.x >= 0 && r.direction == 1, "turns around at the left edge");
    check(r.x == 50, "reflects off the left wall by the right amount");

    printf("it stays on screen for a long run\n");
    r = make_rect(0, 1);
    int64_t lowest = r.x, highest = r.x;
    for (int i = 0; i < 20000; i++) {
        rect_advance(&r, TIMER_HZ / 60, TIMER_HZ, SCREEN_W);   /* about five minutes */
        if (r.x < lowest) lowest = r.x;
        if (r.x > highest) highest = r.x;
    }
    check(lowest >= 0, "never crosses the left edge");
    check(highest <= limit, "never crosses the right edge");
    check(highest > limit / 2, "actually travelled across the screen");

    printf("a rectangle that cannot fit is left alone\n");
    struct moving_rect huge = make_rect(0, 1);
    huge.width = SCREEN_W + 100;
    check(!rect_advance(&huge, TIMER_HZ, TIMER_HZ, SCREEN_W), "wider than the screen");

    struct moving_rect exact = make_rect(0, 1);
    exact.width = SCREEN_W;
    rect_advance(&exact, TIMER_HZ, TIMER_HZ, SCREEN_W);
    check(exact.x == 0, "exactly as wide as the screen has nowhere to go");
}

int main(void)
{
    test_timer_wrap();
    test_rect_movement();
    test_rect_stays_on_screen();

    if (failures > 0) {
        printf("\n%d timer or rectangle check(s) FAILED\n", failures);
        return 1;
    }
    printf("\ntimer and moving rectangle checks passed\n");
    return 0;
}
