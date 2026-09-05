/* A program with a window.
 *
 * It opens one, fills it, draws some bars and some text, puts it on the screen
 * and waits long enough to be looked at. Then it exits, and the window goes
 * with it.
 *
 * The point is what is absent. There is no terminal in this program, no
 * printing, and no line of text going to somebody else's window. It asked the
 * kernel for a rectangle and put pixels in it, which is the thing every real
 * application does and the thing ME OS could not do until now.
 *
 * Written in C, which is also new. Up to M33 the only programs were assembly,
 * because assembly needs nothing underneath it. This needs nothing underneath
 * it either: no C library, no runtime, no startup code. `_start` is the first
 * instruction and `exit` is the last.
 *
 * See M34 in docs/milestones.md.
 */
#include "lib/sys.h"

/* A row of colours, so the window is obviously drawn rather than cleared.
 *
 * Deliberately none of the six the ME OS theme uses. The first version of this
 * reused them, and a test that looked for those pixels on the screen would
 * have been satisfied by the desktop's own borders and cursor. These are the
 * program's and nothing else on the machine draws them, which is what lets a
 * check say the pixels came from here. */
static const long bars[] = {
    RGB(230, 90, 90),
    RGB(90, 230, 140),
    RGB(250, 190, 60),
    RGB(140, 120, 250),
    RGB(60, 200, 210),
    RGB(240, 140, 200),
};

void _start(void)
{
    const long size = win_open("PAINT");
    if (size < 0) {
        /* No window. Say so where a person will see it and stop. */
        write("PAINT COULD NOT OPEN A WINDOW\n");
        exit(1);
    }
    const long width = size >> 32;
    const long height = size & 0xFFFFFFFF;

    win_fill(0, 0, width, height, RGB(14, 18, 24));

    const long count = (long)(sizeof bars / sizeof bars[0]);

    /* Laid out from the size the desktop gave, not from numbers written here.
     *
     * The first version spaced the bars a fixed distance apart, and the last
     * one fell below the bottom of the window and was clipped away. The window
     * was not the size the program assumed, and a program is never told what
     * size it will be until it asks. This is what asking is for. */
    const long top = 40;
    const long bottom = height > 46 ? height - 46 : height;
    const long room = bottom > top ? bottom - top : 0;
    const long step = count > 0 ? room / count : 0;
    const long thickness = step > 10 ? step - 8 : 2;

    for (long i = 0; i < count && step > 0; i++) {
        /* Each one longer than the last, so a mistake in the clipping shows up
         * as a bar of the wrong length rather than as nothing at all. */
        win_fill(24, top + i * step, 60 + i * 46, thickness, bars[i]);
    }

    /* Deliberately drawn partly outside the window. The surface layer has
     * clipped since M14, so what should appear is the part that is inside and
     * nothing anywhere else. */
    win_fill(width - 40, top, 120, thickness, RGB(255, 255, 255));

    win_text(24, 12, "DRAWN BY A PROGRAM", RGB(255, 255, 255), 2);
    win_text(24, height - 36, "THIS WINDOW BELONGS TO A FILE", RGB(160, 176, 192), 1);
    win_text(24, height - 20, "ON THE DISK, NOT TO ME OS", RGB(160, 176, 192), 1);

    win_flush();

    /* Long enough to be seen. Nothing else on the machine runs while this
     * happens, which is honest rather than good, and is what a scheduler
     * fixes. */
    hold_ms(2500);

    exit(0);
}
