/* Drawing the pointer.
 *
 * There is no second buffer to draw into, so the cursor saves the pixels it
 * covers and puts them back before it moves. Anything else that draws must
 * hide the cursor first, or the saved pixels go stale and the cursor smears
 * the old picture back over the new one.
 */
#ifndef ME_CURSOR_H
#define ME_CURSOR_H

#include <stdbool.h>
#include <stdint.h>

#define CURSOR_WIDTH  8
#define CURSOR_HEIGHT 12

/* Draws the cursor at x, y, saving what was underneath. Does nothing if it is
 * already drawn somewhere: hide it first. */
void cursor_show(uint64_t x, uint64_t y, uint32_t fill, uint32_t outline);

/* Puts back what the cursor covered. Safe to call when nothing is drawn. */
void cursor_hide(void);

bool cursor_visible(void);

#endif /* ME_CURSOR_H */
