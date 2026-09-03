#include "term.h"

#include "termback.h"

#include "font.h"

/* One pixel of air between rows, so descenders and the block cursor do not
 * touch the line below. */
#define ROW_PITCH (FONT_HEIGHT + 2)
#define PROMPT "ME> "

static uint32_t clamp(uint32_t value, uint32_t low, uint32_t high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

uint32_t term_cols_for(uint32_t width)
{
    return width < FONT_WIDTH ? 1 : width / FONT_WIDTH;
}

uint32_t term_rows_for(uint32_t height)
{
    /* One row is kept back for the prompt, which is drawn under the grid rather
     * than in it. Without that the last line of output and the line being typed
     * would land on each other. */
    const uint32_t fits = height < ROW_PITCH ? 1 : height / ROW_PITCH;
    return fits > 1 ? fits - 1 : 1;
}

void term_init(struct term *term, uint32_t cols, uint32_t rows)
{
    if (term == NULL) {
        return;
    }
    *term = (struct term){0};
    term->cols = clamp(cols, 1, TERM_MAX_COLS);
    term->rows = clamp(rows, 1, TERM_MAX_ROWS);
    term_set_prompt(term, PROMPT);
    for (uint32_t y = 0; y < TERM_MAX_ROWS; y++) {
        for (uint32_t x = 0; x < TERM_MAX_COLS; x++) {
            term->cells[y][x] = ' ';
        }
    }
}

void term_set_prompt(struct term *term, const char *prompt)
{
    if (term == NULL || prompt == NULL) {
        return;
    }
    uint64_t length = 0;
    while (prompt[length] != '\0') {
        length++;
    }
    if (length + 1 >= sizeof term->prompt) {
        return;
    }
    for (uint64_t i = 0; i <= length; i++) {
        term->prompt[i] = prompt[i];
    }
}

void term_resize(struct term *term, uint32_t cols, uint32_t rows)
{
    if (term == NULL) {
        return;
    }
    term->cols = clamp(cols, 1, TERM_MAX_COLS);
    term->rows = clamp(rows, 1, TERM_MAX_ROWS);
    /* Pulled onto the last row rather than left one past it. One past the end
     * is a legal resting place during normal printing, where it means the next
     * character scrolls, but after a resize the rows above have been cut about
     * anyway and leaving the cursor pending would throw away the line that is
     * now at the bottom. */
    if (term->row >= term->rows) {
        term->row = term->rows - 1;
    }
    if (term->column >= term->cols) {
        term->column = term->cols - 1;
    }
}

void term_clear(struct term *term)
{
    if (term == NULL) {
        return;
    }
    for (uint32_t y = 0; y < TERM_MAX_ROWS; y++) {
        for (uint32_t x = 0; x < TERM_MAX_COLS; x++) {
            term->cells[y][x] = ' ';
        }
    }
    term->row = 0;
    term->column = 0;
    /* Back to the newest line. What was kept stays kept, which is what every
     * terminal does: CLEAR empties the screen, it does not burn the past. But
     * leaving the view up in the scrollback of a screen that is now blank would
     * look like the machine had stopped answering. */
    (void)termback_to_bottom(term);
}

/* Moves every row up one and blanks the last, which is what makes the grid a
 * scrollback rather than a page that stops when it is full. */
static void scroll(struct term *term)
{
    /* Kept before it is written over. This is the whole difference between
     * scrolling, which the terminal has always done, and scrollback, which the
     * header claimed and did not have. */
    termback_keep(term, term->cells[0]);

    for (uint32_t y = 0; y + 1 < term->rows; y++) {
        for (uint32_t x = 0; x < term->cols; x++) {
            term->cells[y][x] = term->cells[y + 1][x];
        }
    }
    for (uint32_t x = 0; x < TERM_MAX_COLS; x++) {
        term->cells[term->rows - 1][x] = ' ';
    }
}

/* Makes room for a character before writing one.
 *
 * The scroll happens here rather than in `term_newline` on purpose. A newline
 * only says the cursor has left this row; whether the next row exists is not
 * known until something is actually written to it. Scrolling eagerly meant that
 * printing a line ending in a newline immediately pushed that line up, so the
 * newest output was never on the bottom row where a person looks for it. */
static void make_room(struct term *term)
{
    if (term->row >= term->rows) {
        scroll(term);
        term->row = term->rows - 1;
    }
}

void term_newline(struct term *term)
{
    if (term == NULL) {
        return;
    }
    term->column = 0;
    term->row++;
}

void term_print(struct term *term, const char *text)
{
    if (term == NULL || text == NULL) {
        return;
    }
    for (uint64_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            term_newline(term);
            continue;
        }
        /* Wrapped rather than cut, because a line that runs off the right edge
         * of a narrow tile is a line a person cannot read at all. */
        if (term->column >= term->cols) {
            term_newline(term);
        }
        make_room(term);
        term->cells[term->row][term->column++] = text[i];
    }
}

void term_println(struct term *term, const char *text)
{
    term_print(term, text);
    term_newline(term);
}

void term_print_number(struct term *term, uint64_t value)
{
    char digits[21];
    uint64_t n = 0;

    if (value == 0) {
        digits[n++] = '0';
    }
    while (value > 0 && n < sizeof digits - 1) {
        digits[n++] = (char)('0' + (value % 10));
        value /= 10;
    }

    char out[21];
    uint64_t written = 0;
    while (n > 0) {
        out[written++] = digits[--n];
    }
    out[written] = '\0';
    term_print(term, out);
}

bool term_key(struct term *term, char ch, bool enter, bool backspace,
              char *out, uint64_t capacity)
{
    if (term == NULL) {
        return false;
    }
    /* Typing puts you back at the bottom. Reading the past is worth doing, and
     * typing into a screen that is not showing what you type is not. */
    (void)termback_to_bottom(term);

    if (backspace) {
        if (term->input_length > 0) {
            term->input[--term->input_length] = '\0';
        }
        return false;
    }

    if (enter) {
        term->input[term->input_length] = '\0';
        /* Kept before the line is handed over, and only when there is something
         * to keep. A history full of blank entries is a history you have to
         * scroll past to reach anything. */
        if (term->input_length > 0) {
            if (term->history_count == TERM_HISTORY) {
                for (uint32_t i = 0; i + 1 < TERM_HISTORY; i++) {
                    for (uint32_t c = 0; c < TERM_INPUT_MAX; c++) {
                        term->history[i][c] = term->history[i + 1][c];
                    }
                }
                term->history_count--;
            }
            for (uint32_t c = 0; c < TERM_INPUT_MAX; c++) {
                term->history[term->history_count][c] = term->input[c];
            }
            term->history_count++;
        }
        term->history_at = term->history_count;
        if (out != NULL && capacity > 0) {
            uint64_t i = 0;
            for (; i + 1 < capacity && term->input[i] != '\0'; i++) {
                out[i] = term->input[i];
            }
            out[i] = '\0';
        }
        term->input_length = 0;
        term->input[0] = '\0';
        return true;
    }

    if (ch == '\0' || term->input_length + 1 >= TERM_INPUT_MAX) {
        return false;
    }
    /* Refused rather than stored. The font draws a box for anything it does not
     * know, and a line that reads back differently from what is on the screen is
     * worse than a key that did nothing. */
    if (!font_knows(ch)) {
        return false;
    }
    term->input[term->input_length++] = ch;
    term->input[term->input_length] = '\0';
    /* Typing puts you back on the line you are writing rather than in the
     * middle of the history, which is where the next arrow should start from. */
    term->history_at = term->history_count;
    return false;
}

bool term_history_step(struct term *term, bool back)
{
    if (term == NULL || term->history_count == 0) {
        return false;
    }
    if (back) {
        if (term->history_at == 0) {
            return false;
        }
        term->history_at--;
    } else {
        if (term->history_at >= term->history_count) {
            return false;
        }
        term->history_at++;
    }

    /* Past the newest entry is the empty line you were typing, which is what
     * pressing down at the end goes back to. */
    if (term->history_at >= term->history_count) {
        term->input[0] = '\0';
        term->input_length = 0;
        return true;
    }

    uint32_t length = 0;
    while (term->history[term->history_at][length] != '\0' &&
           length + 1 < TERM_INPUT_MAX) {
        term->input[length] = term->history[term->history_at][length];
        length++;
    }
    term->input[length] = '\0';
    term->input_length = length;
    return true;
}

uint64_t term_prompt_line(const struct term *term, char *out, uint64_t capacity)
{
    if (out == NULL || capacity == 0) {
        return 0;
    }
    uint64_t written = 0;
    const char *prompt = term != NULL && term->prompt[0] != '\0'
        ? term->prompt : PROMPT;
    for (const char *p = prompt; *p != '\0' && written + 1 < capacity; p++) {
        out[written++] = *p;
    }
    if (term != NULL) {
        for (uint32_t i = 0; i < term->input_length && written + 1 < capacity; i++) {
            out[written++] = term->input[i];
        }
    }
    out[written] = '\0';
    return written;
}

void term_draw(const struct term *term, struct surface *surface,
               uint32_t text, uint32_t background, uint32_t accent)
{
    if (term == NULL || !surface_valid(surface)) {
        return;
    }
    surface_fill_rect(surface, 0, 0, surface->width, surface->height, background);

    char line[TERM_MAX_COLS + 1];
    for (uint32_t y = 0; y < term->rows && y < TERM_MAX_ROWS; y++) {
        /* Through the view rather than out of the grid, so scrolling back shows
         * what used to be here. At the bottom this hands back the grid itself
         * and costs nothing. */
        const char *cells = termback_row(term, y);
        if (cells == NULL) {
            continue;
        }
        uint32_t end = term->cols;
        /* Trailing blanks are not drawn. Every one of them would be a glyph
         * lookup and a loop over eight rows of nothing. */
        while (end > 0 && cells[end - 1] == ' ') {
            end--;
        }
        if (end == 0) {
            continue;
        }
        for (uint32_t x = 0; x < end; x++) {
            line[x] = cells[x];
        }
        line[end] = '\0';
        surface_draw_string(surface, line, 0, (int64_t)(y * ROW_PITCH), text, 1);
    }

    char prompt[TERM_MAX_COLS + 1];
    const int64_t prompt_y = (int64_t)(term->rows * ROW_PITCH);

    /* Looking at the past, the prompt line says so instead.
     *
     * A terminal showing old output with a live prompt under it looks like a
     * machine that has stopped answering. Saying how far back the view is, and
     * that a key brings it back, is the difference between a feature and a
     * fault somebody reports. */
    if (termback_offset(term) > 0) {
        char note[TERM_MAX_COLS + 1];
        uint64_t at = 0;
        const char *say = "-- ";
        while (*say != '\0' && at + 1 < sizeof note) {
            note[at++] = *say++;
        }
        uint32_t lines = termback_offset(term);
        char digits[12];
        uint64_t d = 0;
        do {
            digits[d++] = (char)('0' + lines % 10);
            lines /= 10;
        } while (lines > 0 && d < sizeof digits);
        while (d > 0 && at + 1 < sizeof note) {
            note[at++] = digits[--d];
        }
        say = " LINES BACK, PAGE DOWN TO RETURN --";
        while (*say != '\0' && at + 1 < sizeof note) {
            note[at++] = *say++;
        }
        note[at] = '\0';
        surface_draw_string(surface, note, 0, prompt_y, accent, 1);
        return;
    }

    const uint64_t length = term_prompt_line(term, prompt, sizeof prompt);
    surface_draw_string(surface, prompt, 0, prompt_y, accent, 1);
    /* A block after the text, so it is clear the machine is waiting for more
     * rather than having stopped. */
    surface_fill_rect(surface, (int64_t)(length * FONT_WIDTH), prompt_y,
                      FONT_WIDTH, FONT_HEIGHT, accent);
}
