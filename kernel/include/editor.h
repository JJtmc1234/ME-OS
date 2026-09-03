/* A text editor. Lines you can move around in and change, and a file to save to.
 *
 * The shell could read a file and replace it with one line. Nothing could change
 * the middle of one, which is most of what anybody actually does with a
 * filesystem. This is the piece that makes the filesystem worth having.
 *
 * The buffer and the editing are here. Drawing is here too, because an editor is
 * mostly the question of what is on screen and where the cursor is, and that is
 * not something a general text widget would answer the same way. Reading and
 * writing files is not here: this is handed a buffer and asked to change it, so
 * every edit can be checked without a filesystem underneath it.
 *
 * See M21 in docs/milestones.md.
 */
#ifndef ME_EDITOR_H
#define ME_EDITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "surface.h"
#include "vfs.h"

#define EDIT_MAX_LINES 48
#define EDIT_MAX_COLS  110
#define EDIT_PATH_MAX  128

/* A document this editor will hold has to be one the filesystem will take.
 *
 * Before M24 it was not. The editor held about five thousand characters and a
 * file held five hundred, so you could type a page and be told on Ctrl O that
 * none of it could be saved. Refusing was the right thing to do with what it
 * had, and the right fix was to make the two agree.
 *
 * Every line plus a newline, which is the worst case and the one that matters.
 */
_Static_assert(EDIT_MAX_LINES * (EDIT_MAX_COLS + 1) <= VFS_FILE_MAX,
               "the editor can hold a document the filesystem would refuse");

struct editor {
    char lines[EDIT_MAX_LINES][EDIT_MAX_COLS + 1];
    uint32_t line_count;
    uint32_t cursor_line;
    uint32_t cursor_column;
    /* The first line on screen. Kept so a file longer than the window can be
     * worked on, and moved only when the cursor would otherwise leave. */
    uint32_t top_line;
    uint32_t visible_lines;
    uint32_t visible_columns;
    char path[EDIT_PATH_MAX];
    /* Changed since it was opened or last saved. What makes it possible to say
     * so on the status line rather than leaving somebody to remember. */
    bool changed;
    char status[EDIT_MAX_COLS + 1];
};

/* An empty document with one empty line, because a document with no lines has
 * nowhere to put the cursor. */
void editor_init(struct editor *editor);
void editor_set_path(struct editor *editor, const char *path);
void editor_set_status(struct editor *editor, const char *status);

/* Replaces the whole document with `text`, splitting it on newlines. A line
 * longer than the buffer is cut and said so on the status line, because an
 * editor that silently shortened a line would save the short version back. */
void editor_load(struct editor *editor, const char *text);

/* The document as one string with newlines between the lines. Returns how much
 * was written, and sets `complete` to false when it did not all fit. */
uint64_t editor_text(const struct editor *editor, char *out, uint64_t capacity,
                     bool *complete);

/* One printable character at the cursor. */
void editor_insert(struct editor *editor, char ch);
void editor_backspace(struct editor *editor);
void editor_newline(struct editor *editor);
void editor_move(struct editor *editor, int32_t columns, int32_t lines);
void editor_home(struct editor *editor);
void editor_end(struct editor *editor);

/* How many lines and columns fit in a client area of this size, so the caller
 * does not have to know the font or the room the status line takes. */
void editor_fit(struct editor *editor, uint32_t width, uint32_t height);

void editor_draw(const struct editor *editor, struct surface *surface,
                 uint32_t text, uint32_t background, uint32_t accent,
                 uint32_t dim);

#endif /* ME_EDITOR_H */
