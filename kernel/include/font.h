/* Tiny bitmap font used for M1 boot proof text.
 *
 * Glyphs are 8x8. Only the leftmost 5 columns are used, so bit 7 of each
 * row byte is the leftmost pixel and bits 2..0 are always clear, giving a
 * natural 3 pixel gap between characters.
 */
#ifndef ME_FONT_H
#define ME_FONT_H

#include <stdbool.h>
#include <stdint.h>

#define FONT_WIDTH  8
#define FONT_HEIGHT 8

/* Returns 8 row bytes for `c`, top row first.
 * Unsupported characters return a hollow box so mistakes stay visible
 * instead of silently rendering as blank space.
 */
const uint8_t *font_glyph(char c);

/* Whether this font has a real glyph for that character.
 *
 * `font_glyph` answers with a box for anything it does not know, which is right
 * for drawing and useless for deciding. A terminal that stored a character it
 * cannot draw would show a line that reads back differently from what was
 * typed. See M19. */
bool font_knows(char c);

#endif /* ME_FONT_H */
