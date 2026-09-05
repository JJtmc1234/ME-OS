/* Windows for programs.
 *
 * Up to M33 every window on this desktop was made by the kernel at boot and
 * never went away, and the only thing a program could do with a screen was
 * print a line of text into somebody else's terminal. This is the part that
 * lets a program have a rectangle of its own and put pixels in it.
 *
 * A program gets one window. Not because more is hard, but because one is
 * enough to answer whether a program can draw at all, and a limit that is
 * obviously too small is easier to raise later than one that is nearly right
 * is to reason about now. Since there is exactly one, no call takes a window
 * handle: the window a program means is the only window it has.
 *
 * **A program does not choose its size.** ME OS is tiling first, and the
 * desktop has said since M18 that an app is given a tile rather than asking
 * for one. So opening a window returns the size it was given, and a program
 * that wants to know how much room it has asks after the fact rather than
 * before. Letting a program pass a width and a height would be inventing a
 * negotiation that this desktop does not have.
 *
 * Nothing here trusts a coordinate. Every drawing call goes through the
 * surface layer, which has clipped since M14 and is tested for it, so a
 * rectangle reaching outside a window draws the part that is inside and
 * nothing else. That is a stronger guarantee than checking here would be,
 * because it is the same clip the compositor already depends on.
 *
 * See M34 in docs/milestones.md.
 */
#ifndef ME_WINSYS_H
#define ME_WINSYS_H

#include <stdint.h>

#include "desktop.h"

struct process;

/* What the window system needs from the rest of the kernel.
 *
 * Passed in rather than reached for, because main.c owns the desktop, the
 * compositor and the repaint of every built in app, and none of that belongs
 * in a system call handler. */
struct winsys_hooks {
    struct desktop *desktop;
    /* The layout changed: recompute every tile, repaint every frame and every
     * built in app, and present. Adding or removing a window resizes all the
     * others, and with no scheduler the main loop is stopped inside the
     * program, so nothing else is going to repaint them. */
    void (*relayout)(void);
    /* Compose and present what is already drawn. */
    void (*present)(void);
};

void winsys_attach(const struct winsys_hooks *hooks);

/* Opens the program's window. Returns the client area as width in the top 32
 * bits and height in the bottom 32, or a negative error. */
int64_t winsys_open(struct process *proc, uint64_t title);

int64_t winsys_fill(struct process *proc, uint64_t x, uint64_t y,
                    uint64_t width, uint64_t height, uint64_t colour);

int64_t winsys_text(struct process *proc, uint64_t x, uint64_t y,
                    uint64_t text, uint64_t colour, uint64_t scale);

/* Puts what has been drawn on the screen. */
int64_t winsys_flush(struct process *proc);

int64_t winsys_close(struct process *proc);

/* Closes a program's window whether or not it asked. Called when a process
 * ends, including when it faults, because a window belonging to a program that
 * is no longer running is a window nobody can close. */
void winsys_release(struct process *proc);

#endif /* ME_WINSYS_H */
