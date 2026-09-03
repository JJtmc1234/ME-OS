/* The lines that have scrolled off the top, and the view onto them.
 *
 * `term.h` used to say the terminal had scrollback and it did not. What it had
 * was scrolling, which is the opposite: a line reaching the top was written
 * over and gone. Run TREE on anything with a few directories in it and the
 * start of the answer could not be got back.
 *
 * A ring rather than a list. It fills up and then keeps going, throwing away
 * the oldest line to make room, which is what every terminal does and is why
 * the buffer has a size at all.
 *
 * Kept apart from `term.c` because the grid is about what is on the screen now
 * and this is about what used to be. See M26 in docs/milestones.md.
 */
#ifndef ME_TERMBACK_H
#define ME_TERMBACK_H

#include "term.h"

/* Takes a copy of the row about to be scrolled away. */
void termback_keep(struct term *term, const char *row);

/* Moves the view by whole screens. A positive `pages` looks further back.
 *
 * Returns true when the view actually moved, so the caller only repaints when
 * there is something different to draw. Asking to go further back than the
 * buffer holds stops at the oldest line rather than refusing, because a person
 * pressing the key again at the top means "as far as you go".
 */
bool termback_scroll(struct term *term, int32_t pages);

/* Back to the newest line. Called on every key that is not a scroll, because
 * typing while looking at the past should show you what you are typing. */
bool termback_to_bottom(struct term *term);

/* The row to draw at screen line `line`, given where the view is.
 *
 * This is the whole of what the view does. When the view is at the bottom it
 * hands back the grid unchanged, so nothing pays for scrollback until somebody
 * uses it. Otherwise it hands back a kept line, or the part of the grid that is
 * still visible above the fold.
 */
const char *termback_row(const struct term *term, uint32_t line);

/* How many lines back the view is, so the terminal can say so. A view that had
 * silently moved would look like output that had stopped arriving. */
uint32_t termback_offset(const struct term *term);
uint32_t termback_held(const struct term *term);

#endif /* ME_TERMBACK_H */
