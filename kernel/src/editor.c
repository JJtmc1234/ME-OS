#include "editor.h"

#include "font.h"

#define ROW_PITCH (FONT_HEIGHT + 2)
/* The status line, and one line of gap above it. */
#define STATUS_ROWS 2

static uint64_t length_of(const char *text)
{
    uint64_t n = 0;
    while (text != NULL && text[n] != '\0') {
        n++;
    }
    return n;
}

static void copy_into(char *out, uint64_t capacity, const char *text)
{
    uint64_t i = 0;
    for (; text != NULL && text[i] != '\0' && i + 1 < capacity; i++) {
        out[i] = text[i];
    }
    out[i] = '\0';
}

void editor_init(struct editor *editor)
{
    if (editor == NULL) {
        return;
    }
    *editor = (struct editor){0};
    /* One empty line, because a document with no lines has nowhere to put the
     * cursor and every operation below would have to check for it. */
    editor->line_count = 1;
    editor->lines[0][0] = '\0';
    editor->visible_lines = 1;
    editor->visible_columns = EDIT_MAX_COLS;
}

void editor_set_path(struct editor *editor, const char *path)
{
    if (editor != NULL) {
        copy_into(editor->path, sizeof editor->path, path);
    }
}

void editor_set_status(struct editor *editor, const char *status)
{
    if (editor != NULL) {
        copy_into(editor->status, sizeof editor->status, status);
    }
}

void editor_load(struct editor *editor, const char *text)
{
    if (editor == NULL) {
        return;
    }
    /* Kept across the reset, because loading a file into an editor should not
     * forget which file it is. */
    char path[EDIT_PATH_MAX];
    copy_into(path, sizeof path, editor->path);

    editor_init(editor);
    copy_into(editor->path, sizeof editor->path, path);
    if (text == NULL) {
        return;
    }

    bool cut = false;
    uint32_t line = 0;
    uint32_t column = 0;
    for (uint64_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            editor->lines[line][column] = '\0';
            if (line + 1 >= EDIT_MAX_LINES) {
                cut = true;
                break;
            }
            line++;
            column = 0;
            continue;
        }
        if (column >= EDIT_MAX_COLS) {
            cut = true;
            continue;
        }
        editor->lines[line][column++] = text[i];
    }
    editor->lines[line][column] = '\0';
    editor->line_count = line + 1;
    editor->changed = false;

    /* Said, not swallowed. An editor that quietly shortened a line would save
     * the short version back over the long one. */
    editor_set_status(editor, cut ? "OPENED, AND IT DID NOT ALL FIT"
                                  : "OPENED");
}

uint64_t editor_text(const struct editor *editor, char *out, uint64_t capacity,
                     bool *complete)
{
    if (complete != NULL) {
        *complete = false;
    }
    if (out == NULL || capacity == 0) {
        return 0;
    }
    out[0] = '\0';
    if (editor == NULL) {
        return 0;
    }

    uint64_t written = 0;
    for (uint32_t line = 0; line < editor->line_count; line++) {
        if (line > 0) {
            if (written + 1 >= capacity) {
                return written;
            }
            out[written++] = '\n';
        }
        for (uint32_t i = 0; editor->lines[line][i] != '\0'; i++) {
            if (written + 1 >= capacity) {
                out[written] = '\0';
                return written;
            }
            out[written++] = editor->lines[line][i];
        }
    }
    out[written] = '\0';
    if (complete != NULL) {
        *complete = true;
    }
    return written;
}

/* Moves the view so the cursor is on it. Called after everything that moves the
 * cursor, so there is one rule about scrolling rather than one per key. */
static void follow_cursor(struct editor *editor)
{
    if (editor->visible_lines == 0) {
        editor->top_line = editor->cursor_line;
        return;
    }
    if (editor->cursor_line < editor->top_line) {
        editor->top_line = editor->cursor_line;
    } else if (editor->cursor_line >= editor->top_line + editor->visible_lines) {
        editor->top_line = editor->cursor_line - editor->visible_lines + 1;
    }
}

void editor_insert(struct editor *editor, char ch)
{
    if (editor == NULL || ch == '\0' || !font_knows(ch)) {
        return;
    }
    char *line = editor->lines[editor->cursor_line];
    const uint64_t length = length_of(line);
    if (length >= EDIT_MAX_COLS) {
        editor_set_status(editor, "THAT LINE IS FULL");
        return;
    }
    /* Everything after the cursor moves right by one, so typing in the middle of
     * a line inserts rather than overwriting. */
    for (uint64_t i = length + 1; i > editor->cursor_column; i--) {
        line[i] = line[i - 1];
    }
    line[editor->cursor_column++] = ch;
    editor->changed = true;
    follow_cursor(editor);
}

void editor_newline(struct editor *editor)
{
    if (editor == NULL) {
        return;
    }
    if (editor->line_count >= EDIT_MAX_LINES) {
        editor_set_status(editor, "THIS FILE HAS AS MANY LINES AS IT CAN HOLD");
        return;
    }

    for (uint32_t line = editor->line_count; line > editor->cursor_line + 1; line--) {
        copy_into(editor->lines[line], EDIT_MAX_COLS + 1, editor->lines[line - 1]);
    }
    editor->line_count++;

    /* What was after the cursor becomes the new line, and what was before it
     * stays where it was. Splitting the other way round would put the tail of
     * every line above its own head. */
    char *here = editor->lines[editor->cursor_line];
    char *below = editor->lines[editor->cursor_line + 1];
    copy_into(below, EDIT_MAX_COLS + 1, here + editor->cursor_column);
    here[editor->cursor_column] = '\0';

    editor->cursor_line++;
    editor->cursor_column = 0;
    editor->changed = true;
    follow_cursor(editor);
}

void editor_backspace(struct editor *editor)
{
    if (editor == NULL) {
        return;
    }
    char *line = editor->lines[editor->cursor_line];

    if (editor->cursor_column > 0) {
        const uint64_t length = length_of(line);
        for (uint64_t i = editor->cursor_column - 1; i < length; i++) {
            line[i] = line[i + 1];
        }
        editor->cursor_column--;
        editor->changed = true;
        follow_cursor(editor);
        return;
    }

    /* At the start of a line, backspace joins it to the one above, which is what
     * it does everywhere else. Nothing to join at the top of the file. */
    if (editor->cursor_line == 0) {
        return;
    }
    char *above = editor->lines[editor->cursor_line - 1];
    const uint64_t above_length = length_of(above);
    const uint64_t here_length = length_of(line);
    if (above_length + here_length > EDIT_MAX_COLS) {
        editor_set_status(editor, "THOSE TWO LINES WILL NOT FIT AS ONE");
        return;
    }
    for (uint64_t i = 0; i <= here_length; i++) {
        above[above_length + i] = line[i];
    }
    for (uint32_t at = editor->cursor_line; at + 1 < editor->line_count; at++) {
        copy_into(editor->lines[at], EDIT_MAX_COLS + 1, editor->lines[at + 1]);
    }
    editor->line_count--;
    editor->cursor_line--;
    editor->cursor_column = (uint32_t)above_length;
    editor->changed = true;
    follow_cursor(editor);
}

void editor_move(struct editor *editor, int32_t columns, int32_t lines)
{
    if (editor == NULL) {
        return;
    }
    if (lines < 0 && editor->cursor_line >= (uint32_t)(-lines)) {
        editor->cursor_line -= (uint32_t)(-lines);
    } else if (lines < 0) {
        editor->cursor_line = 0;
    } else if (lines > 0) {
        editor->cursor_line += (uint32_t)lines;
        if (editor->cursor_line >= editor->line_count) {
            editor->cursor_line = editor->line_count - 1;
        }
    }

    if (columns < 0 && editor->cursor_column >= (uint32_t)(-columns)) {
        editor->cursor_column -= (uint32_t)(-columns);
    } else if (columns < 0) {
        editor->cursor_column = 0;
    } else if (columns > 0) {
        editor->cursor_column += (uint32_t)columns;
    }

    /* The cursor cannot sit past the end of the line it is on, so moving down
     * from a long line onto a short one brings it in rather than leaving it in
     * space where typing would write past the terminator. */
    const uint64_t length = length_of(editor->lines[editor->cursor_line]);
    if (editor->cursor_column > length) {
        editor->cursor_column = (uint32_t)length;
    }
    follow_cursor(editor);
}

void editor_home(struct editor *editor)
{
    if (editor != NULL) {
        editor->cursor_column = 0;
    }
}

void editor_end(struct editor *editor)
{
    if (editor != NULL) {
        editor->cursor_column =
            (uint32_t)length_of(editor->lines[editor->cursor_line]);
    }
}

void editor_fit(struct editor *editor, uint32_t width, uint32_t height)
{
    if (editor == NULL) {
        return;
    }
    const uint32_t rows = height < ROW_PITCH ? 1 : height / ROW_PITCH;
    editor->visible_lines = rows > STATUS_ROWS ? rows - STATUS_ROWS : 1;
    editor->visible_columns = width < FONT_WIDTH ? 1 : width / FONT_WIDTH;
    if (editor->visible_columns > EDIT_MAX_COLS) {
        editor->visible_columns = EDIT_MAX_COLS;
    }
    follow_cursor(editor);
}

void editor_draw(const struct editor *editor, struct surface *surface,
                 uint32_t text, uint32_t background, uint32_t accent,
                 uint32_t dim)
{
    if (editor == NULL || !surface_valid(surface)) {
        return;
    }
    surface_fill_rect(surface, 0, 0, surface->width, surface->height, background);

    for (uint32_t row = 0; row < editor->visible_lines; row++) {
        const uint32_t line = editor->top_line + row;
        if (line >= editor->line_count) {
            break;
        }
        surface_draw_string(surface, editor->lines[line], 0,
                            (int64_t)(row * ROW_PITCH), text, 1);
    }

    /* The cursor, as a block on the character it is on. Drawn even at the end of
     * a line, where there is no character, because that is where the next one
     * goes and a cursor that vanished there would look like a hang. */
    if (editor->cursor_line >= editor->top_line &&
        editor->cursor_line < editor->top_line + editor->visible_lines &&
        editor->cursor_column < editor->visible_columns) {
        const int64_t x = (int64_t)(editor->cursor_column * FONT_WIDTH);
        const int64_t y =
            (int64_t)((editor->cursor_line - editor->top_line) * ROW_PITCH);
        surface_fill_rect(surface, x, y, FONT_WIDTH, FONT_HEIGHT, accent);
        const char under[2] = { editor->lines[editor->cursor_line]
                                    [editor->cursor_column], '\0' };
        if (under[0] != '\0') {
            surface_draw_string(surface, under, x, y, background, 1);
        }
    }

    const int64_t status_y =
        (int64_t)((editor->visible_lines + 1) * ROW_PITCH);
    if (status_y + FONT_HEIGHT > (int64_t)surface->height) {
        return;
    }
    surface_draw_string(surface, editor->path[0] != '\0' ? editor->path
                                                         : "(NO FILE)",
                        0, status_y, editor->changed ? accent : dim, 1);
    if (editor->changed) {
        surface_draw_string(surface, "*",
                            (int64_t)(length_of(editor->path) + 1) * FONT_WIDTH,
                            status_y, accent, 1);
    }
    if (editor->status[0] != '\0') {
        const int64_t right =
            (int64_t)surface->width - (int64_t)length_of(editor->status) * FONT_WIDTH;
        if (right > (int64_t)(length_of(editor->path) + 3) * FONT_WIDTH) {
            surface_draw_string(surface, editor->status, right, status_y, dim, 1);
        }
    }
}
