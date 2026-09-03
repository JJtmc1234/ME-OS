/* A character grid, the lines that have scrolled off it, and a line editor.
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
/* How many lines back the arrows reach. Enough to get at what you just did
 * wrong, which is what history is actually for. */
#define TERM_HISTORY 12
/* How many scrolled off lines are kept, which is a different thing from the
 * command history above: this is what the machine said, that is what you typed.
 * Two hundred is a few screens, which is as far back as anybody looks for
 * something they can simply run again. */
#define TERM_BACK 200

struct term {
    char cells[TERM_MAX_ROWS][TERM_MAX_COLS];
    uint32_t cols;
    uint32_t rows;
    /* The row the next output line goes on. Once it reaches the bottom the grid
     * scrolls rather than the row growing, and the line that leaves the top is
     * kept in `back` rather than written over. */
    uint32_t row;
    uint32_t column;
    /* The line being typed. Kept apart from the grid so that editing it does
     * not have to rewrite history, and so a backspace at the start of the line
     * cannot eat the prompt or the output above it. */
    char input[TERM_INPUT_MAX];
    uint32_t input_length;
    /* What goes before the input. A shell that did not say where it was would
     * make CD a command with no visible effect. Set by the caller, which is the
     * only thing that knows there is a filesystem. */
    char prompt[TERM_INPUT_MAX];
    /* The lines already run, newest last, and where the arrows currently are.
     * `history_at` equal to the count means the line being typed now, which is
     * what pressing down at the newest entry goes back to. */
    char history[TERM_HISTORY][TERM_INPUT_MAX];
    uint32_t history_count;
    uint32_t history_at;

    /* The lines that have scrolled off the top, oldest first, as a ring. Full,
     * it keeps going and drops the oldest, which is what makes the buffer a
     * size rather than a limit somebody meets.
     *
     * `back_count` stops at TERM_BACK. `back_next` is where the next one goes,
     * and once the ring has wrapped it is also where the oldest one is. */
    char back[TERM_BACK][TERM_MAX_COLS];
    uint32_t back_count;
    uint32_t back_next;
    /* How many lines above the newest the view is. Zero is the bottom, which is
     * where a terminal spends nearly all its life, and where nothing about
     * scrollback costs anything. */
    uint32_t back_view;
};

/* `cols` and `rows` are clamped to what the grid can hold and to at least one
 * of each, so a terminal in a tile too small to show anything still has a
 * consistent state rather than a zero sized one. */
void term_init(struct term *term, uint32_t cols, uint32_t rows);

/* Sets what comes before the typed line. Anything too long for the buffer is
 * refused rather than cut, because a prompt cut in the middle of a path says
 * you are somewhere you are not. */
void term_set_prompt(struct term *term, const char *prompt);

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

/* Walks the history and puts what it finds on the input line. `back` is true for
 * the older direction. Returns false when there is nowhere to go, so the caller
 * knows whether anything needs redrawing. */
bool term_history_step(struct term *term, bool back);

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
