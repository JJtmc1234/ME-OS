/* A character grid with scrollback and a line editor.
 *
 * The thing that makes a machine feel like a computer rather than a picture of
 * one is being able to type at it and have it answer. This is the half that
 * holds the text and knows where the cursor is. What the commands mean is in
 * `cmd.h`, because a terminal that knew what HELP meant could not be tested
 * without also testing every command. See M19 in docs/milestones.md.
 *
 * No escape sequences, no colours per cell, no alternate screen. Those are what
 * a terminal emulator does for programs that expect one, and nothing here
 * expects one yet.
 */
#ifndef ME_TERM_H
#define ME_TERM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "surface.h"

/* Big enough for the largest tile at the resolutions this boots in, at the one
 * font size it has. An explicit bound rather than an allocator, like every other
 * store in this kernel so far. */
#define TERM_MAX_COLS 160
#define TERM_MAX_ROWS 80
#define TERM_INPUT_MAX 96

struct term {
    char cells[TERM_MAX_ROWS][TERM_MAX_COLS];
    uint32_t cols;
    uint32_t rows;
    /* The row the next output line goes on. Once it reaches the bottom the grid
     * scrolls rather than the row growing, which is what scrollback is. */
    uint32_t row;
    uint32_t column;
    /* The line being typed. Kept apart from the grid so that editing it does
     * not have to rewrite history, and so a backspace at the start of the line
     * cannot eat the prompt or the output above it. */
    char input[TERM_INPUT_MAX];
    uint32_t input_length;
};

/* `cols` and `rows` are clamped to what the grid can hold and to at least one
 * of each, so a terminal in a tile too small to show anything still has a
 * consistent state rather than a zero sized one. */
void term_init(struct term *term, uint32_t cols, uint32_t rows);

/* Resizes and keeps what is on screen where it can. Text that no longer fits
 * across is cut rather than reflowed: reflowing needs to know where the real
 * line breaks were, and this grid does not record them. */
void term_resize(struct term *term, uint32_t cols, uint32_t rows);

void term_print(struct term *term, const char *text);
void term_println(struct term *term, const char *text);
void term_newline(struct term *term);
void term_clear(struct term *term);

/* Writes a whole number, so callers do not each carry their own conversion. */
void term_print_number(struct term *term, uint64_t value);

/* One key. Returns true when Enter completed a line, which is then copied into
 * `out` and the input is emptied. Anything the font cannot draw is refused
 * rather than stored, so what is on the screen is what was typed. */
bool term_key(struct term *term, char ch, bool enter, bool backspace,
              char *out, uint64_t capacity);

/* The prompt and what has been typed so far, as one string, so the drawing and
 * any test agree about what the bottom line says. */
uint64_t term_prompt_line(const struct term *term, char *out, uint64_t capacity);

/* Paints the whole grid, the prompt and a block cursor. */
void term_draw(const struct term *term, struct surface *surface,
               uint32_t text, uint32_t background, uint32_t accent);

/* How many columns and rows fit in a client area of this size. */
uint32_t term_cols_for(uint32_t width);
uint32_t term_rows_for(uint32_t height);

#endif /* ME_TERM_H */
