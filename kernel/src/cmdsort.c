/* SORT, which needs to hold every line at once and so lives on its own.
 *
 * The other filters in `cmdtext.c` walk the text once and write as they go.
 * This one cannot: nothing can be printed until the last line has been read,
 * because the last line might be the first one out.
 *
 * See M25 in docs/milestones.md.
 */
#include "cmd.h"

#include "vfs.h"

/* How many lines can be sorted at once.
 *
 * A bound rather than the whole file, because the table of where the lines are
 * has to live somewhere and a file of six thousand one character lines would
 * want six thousand entries. Two hundred is more than fits on the screen and
 * more than anything this machine produces, and going over it is said rather
 * than quietly dropping the rest.
 */
#define SORT_MAX_LINES 200

static char upper(char ch)
{
    return ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
}

/* Which of two lines comes first. Compared a character at a time and ignoring
 * case, so a list reads the way somebody would expect rather than putting every
 * lower case letter after every upper case one. */
static bool before(const char *text, uint64_t a, uint64_t a_end,
                   uint64_t b, uint64_t b_end)
{
    while (a < a_end && b < b_end) {
        const char left = upper(text[a]);
        const char right = upper(text[b]);
        if (left != right) {
            return left < right;
        }
        a++;
        b++;
    }
    /* Everything matched as far as the shorter one goes, so the shorter one
     * comes first. */
    return (a_end - a) < (b_end - b);
}

void cmdsort_run(struct cmd_context *context, const char *path)
{
    static char text[VFS_FILE_MAX + 1];
    if (!cmd_input_text(context, path, text, sizeof text, "SORT")) {
        return;
    }

    uint64_t start[SORT_MAX_LINES];
    uint64_t end[SORT_MAX_LINES];
    uint64_t count = 0;
    bool cut = false;

    for (uint64_t at = 0; text[at] != '\0'; ) {
        uint64_t stop = at;
        while (text[stop] != '\0' && text[stop] != '\n') {
            stop++;
        }
        if (count >= SORT_MAX_LINES) {
            cut = true;
            break;
        }
        start[count] = at;
        end[count] = stop;
        count++;
        at = text[stop] == '\n' ? stop + 1 : stop;
    }

    /* Selection sort. It is the slow one, and with two hundred lines at most
     * that is forty thousand comparisons, which is nothing next to drawing a
     * single frame. A faster sort here would be more code to get wrong for a
     * saving nobody could measure. */
    for (uint64_t i = 0; i < count; i++) {
        uint64_t least = i;
        for (uint64_t j = i + 1; j < count; j++) {
            if (before(text, start[j], end[j], start[least], end[least])) {
                least = j;
            }
        }
        const uint64_t s = start[i], e = end[i];
        start[i] = start[least];
        end[i] = end[least];
        start[least] = s;
        end[least] = e;
    }

    for (uint64_t i = 0; i < count; i++) {
        for (uint64_t at = start[i]; at < end[i]; at++) {
            const char one[2] = { text[at], '\0' };
            cmd_print(context->out, one);
        }
        cmd_newline(context->out);
    }
    if (cut) {
        cmd_print(context->out, "MORE THAN ");
        cmd_print_number(context->out, SORT_MAX_LINES);
        cmd_println(context->out, " LINES, SO THE REST WERE NOT SORTED");
    }
}
