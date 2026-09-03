/* The commands that take text and give back less of it.
 *
 * GREP, HEAD, TAIL and SORT. They are what makes a pipe worth having: on their
 * own they read a file, and with no file they read whatever came down the pipe,
 * so `LS | GREP TXT` and `GREP TXT NOTES.TXT` are the same command asked the
 * same question about text from two different places.
 *
 * None of them knows where its answer goes. They write to the sink in
 * `cmdout.h`, which is the terminal, or a file, or the next command along.
 *
 * See M25 in docs/milestones.md.
 */
#include "cmd.h"

#include "vfs.h"

static char upper(char ch)
{
    return ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
}

static uint64_t length_of(const char *text)
{
    uint64_t n = 0;
    while (text != NULL && text[n] != '\0') {
        n++;
    }
    return n;
}

bool cmd_input_text(struct cmd_context *context, const char *path,
                    char *out, uint64_t capacity, const char *what)
{
    out[0] = '\0';

    /* A name wins over the pipe. `CAT NOTES` means that file even when
     * something was piped in, which is what every other shell does. */
    if (path != NULL && path[0] != '\0') {
        uint64_t length = 0;
        const enum vfs_result done =
            vfs_read(context->fs, path, out, capacity, &length);
        if (done != VFS_OK) {
            cmd_print(context->out, path);
            cmd_print(context->out, ": ");
            cmd_println(context->out, vfs_explain(done));
            return false;
        }
        return true;
    }

    if (context->input == NULL) {
        cmd_print(context->out, what);
        cmd_println(context->out, " NEEDS A NAME, OR SOMETHING PIPED INTO IT");
        return false;
    }
    uint64_t at = 0;
    for (; context->input[at] != '\0' && at + 1 < capacity; at++) {
        out[at] = context->input[at];
    }
    out[at] = '\0';
    return true;
}

/* Where the line starting at `at` ends. The terminator counts as an end, so the
 * last line of a file with no newline on it is still a line. */
static uint64_t line_end(const char *text, uint64_t at)
{
    while (text[at] != '\0' && text[at] != '\n') {
        at++;
    }
    return at;
}

static uint64_t count_lines(const char *text)
{
    uint64_t lines = 0;
    for (uint64_t at = 0; text[at] != '\0'; ) {
        at = line_end(text, at);
        lines++;
        if (text[at] == '\n') {
            at++;
        }
    }
    return lines;
}

static void put_line(struct cmd_out *out, const char *text, uint64_t from,
                     uint64_t to)
{
    for (uint64_t i = from; i < to; i++) {
        const char one[2] = { text[i], '\0' };
        cmd_print(out, one);
    }
    cmd_newline(out);
}

/* Whether `line` holds `needle` anywhere in it, ignoring the difference between
 * upper and lower case. Everything this machine draws is upper case, so a
 * search that cared would find nothing most of the time. */
static bool holds(const char *text, uint64_t from, uint64_t to,
                  const char *needle)
{
    const uint64_t wanted = length_of(needle);
    if (wanted == 0) {
        return true;
    }
    for (uint64_t start = from; start + wanted <= to; start++) {
        uint64_t i = 0;
        while (i < wanted && upper(text[start + i]) == upper(needle[i])) {
            i++;
        }
        if (i == wanted) {
            return true;
        }
    }
    return false;
}

void cmdtext_grep(struct cmd_context *context, const char *rest)
{
    char needle[TERM_INPUT_MAX];
    const char *path = "";
    cmd_split(rest, needle, sizeof needle, &path);
    if (needle[0] == '\0') {
        cmd_println(context->out, "GREP NEEDS SOMETHING TO LOOK FOR");
        return;
    }

    static char text[VFS_FILE_MAX + 1];
    if (!cmd_input_text(context, path, text, sizeof text, "GREP")) {
        return;
    }

    uint64_t found = 0;
    for (uint64_t at = 0; text[at] != '\0'; ) {
        const uint64_t end = line_end(text, at);
        if (holds(text, at, end, needle)) {
            put_line(context->out, text, at, end);
            found++;
        }
        at = text[end] == '\n' ? end + 1 : end;
    }
    if (found == 0) {
        cmd_print(context->out, "NOTHING MATCHED ");
        cmd_println(context->out, needle);
    }
}

/* How many lines HEAD and TAIL show when nobody said. Ten, which is what every
 * other machine does, and small enough to be worth typing. */
#define DEFAULT_LINES 10

/* Reads an optional count off the front of the arguments. `HEAD 5 NOTES` and
 * `HEAD NOTES` both have to work, so a first word that is all digits is the
 * count and anything else is the name. */
static uint64_t count_and_path(const char *rest, const char **path)
{
    char first[TERM_INPUT_MAX];
    const char *after = "";
    cmd_split(rest, first, sizeof first, &after);

    uint64_t value = 0;
    for (uint64_t i = 0; first[i] != '\0'; i++) {
        if (first[i] < '0' || first[i] > '9') {
            *path = rest;
            return DEFAULT_LINES;
        }
        value = value * 10 + (uint64_t)(first[i] - '0');
    }
    if (first[0] == '\0') {
        *path = rest;
        return DEFAULT_LINES;
    }
    *path = after;
    return value;
}

void cmdtext_head(struct cmd_context *context, const char *rest)
{
    const char *path = "";
    const uint64_t wanted = count_and_path(rest, &path);

    static char text[VFS_FILE_MAX + 1];
    if (!cmd_input_text(context, path, text, sizeof text, "HEAD")) {
        return;
    }
    uint64_t shown = 0;
    for (uint64_t at = 0; text[at] != '\0' && shown < wanted; shown++) {
        const uint64_t end = line_end(text, at);
        put_line(context->out, text, at, end);
        at = text[end] == '\n' ? end + 1 : end;
    }
}

void cmdtext_tail(struct cmd_context *context, const char *rest)
{
    const char *path = "";
    const uint64_t wanted = count_and_path(rest, &path);

    static char text[VFS_FILE_MAX + 1];
    if (!cmd_input_text(context, path, text, sizeof text, "TAIL")) {
        return;
    }
    const uint64_t total = count_lines(text);
    const uint64_t skip = total > wanted ? total - wanted : 0;

    uint64_t line = 0;
    for (uint64_t at = 0; text[at] != '\0'; line++) {
        const uint64_t end = line_end(text, at);
        if (line >= skip) {
            put_line(context->out, text, at, end);
        }
        at = text[end] == '\n' ? end + 1 : end;
    }
}
