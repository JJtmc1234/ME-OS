/* A program that will not stop.
 *
 * It opens a window, says what it is, and then loops asking for input forever.
 * Nothing about it is illegal: it faults at no point, reads nothing it should
 * not, and would run happily on any machine that could take the processor back
 * from it.
 *
 * ME OS cannot, yet. There is no scheduler and no timer interrupt, so a
 * program that keeps the processor keeps it. What the kernel can do is notice,
 * on each system call, that a program has been running longer than any program
 * here has any business running for, and stop it. This is the program that
 * proves it does.
 *
 * It is a fixture rather than a program, like the two broken ones from M32.
 * The limit it runs into is a stopgap, and it only works because this program
 * keeps asking for events. A loop that made no system call at all would still
 * hang the machine, and the milestone notes say so.
 *
 * See M35 in docs/milestones.md.
 */
#include "lib/sys.h"

void _start(void)
{
    const long size = win_open("SPIN");
    if (size < 0) {
        write("SPIN COULD NOT OPEN A WINDOW\n");
        exit(1);
    }
    const long width = size >> 32;
    const long height = size & 0xFFFFFFFF;

    win_fill(0, 0, width, height, RGB(40, 16, 16));
    win_text(12, 12, "THIS PROGRAM WILL NOT STOP", RGB(255, 140, 140), 2);
    win_text(12, 44, "THE KERNEL SHOULD STOP IT", RGB(200, 160, 160), 1);
    win_flush();

    for (;;) {
        struct event e;
        win_event(&e);
    }
}
