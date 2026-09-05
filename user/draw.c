/* A program you can use, rather than one you can look at.
 *
 * Hold the left mouse button and move: it draws. Press a number from one to
 * six to change colour. Press Escape to stop.
 *
 * This is what M35 adds and M34 could not do. A window with pixels in it is a
 * picture. A window that answers the mouse is a program, and the difference is
 * that the keyboard and the mouse now reach something that is not the kernel.
 *
 * See M35 in docs/milestones.md.
 */
#include "lib/sys.h"

static const long palette[] = {
    RGB(230, 90, 90),
    RGB(90, 230, 140),
    RGB(250, 190, 60),
    RGB(140, 120, 250),
    RGB(60, 200, 210),
    RGB(240, 140, 200),
};

#define BRUSH 10

void _start(void)
{
    const long size = win_open("DRAW");
    if (size < 0) {
        write("DRAW COULD NOT OPEN A WINDOW\n");
        exit(1);
    }
    const long width = size >> 32;
    const long height = size & 0xFFFFFFFF;
    const long count = (long)(sizeof palette / sizeof palette[0]);

    win_fill(0, 0, width, height, RGB(14, 18, 24));
    win_text(12, 10, "HOLD THE LEFT BUTTON AND MOVE TO DRAW", RGB(255, 255, 255), 1);
    win_text(12, 26, "1 TO 6 CHANGES COLOUR, ESCAPE STOPS", RGB(160, 176, 192), 1);

    /* A strip of the colours, so what the number keys do is visible without
     * anybody having read this file. */
    for (long i = 0; i < count; i++) {
        win_fill(12 + i * 26, 44, 22, 12, palette[i]);
    }
    win_flush();

    long colour = 0;
    long drawn = 0;
    long since_flush = 0;

    for (;;) {
        struct event e;
        if (win_event(&e) != 1) {
            /* Nothing waiting. Asking again immediately is the whole loop,
             * because there is nothing else this machine could be doing. */
            continue;
        }

        if (e.kind == EV_KEY) {
            if (e.key == (unsigned int)KEY_ESCAPE) {
                break;
            }
            if (e.key >= '1' && e.key <= '6') {
                colour = (long)(e.key - '1');
                if (colour >= count) {
                    colour = count - 1;
                }
            }
            continue;
        }

        if (e.kind == EV_POINTER && (e.buttons & 1u) != 0) {
            /* Below the strip, so drawing cannot rub out the instructions. */
            if (e.y > 62) {
                win_fill(e.x - BRUSH / 2, e.y - BRUSH / 2, BRUSH, BRUSH,
                         palette[colour]);
                drawn++;
                since_flush++;
            }
        }

        /* Putting every dot on the screen the moment it is drawn would compose
         * the whole screen per mouse packet. A few at a time is smooth enough
         * and costs a fraction of that. */
        if (since_flush >= 4) {
            win_flush();
            since_flush = 0;
        }
    }

    win_flush();
    write(drawn > 0 ? "DRAW: SOMETHING WAS DRAWN\n" : "DRAW: NOTHING WAS DRAWN\n");
    exit(drawn > 0 ? 0 : 1);
}
