/* Somewhere for a command to write, which is not necessarily the screen.
 *
 * Every command used to call `term_println` directly, so its output could only
 * ever go to the terminal. That is why `ECHO HI > NOTES` was the only
 * redirection this shell had: ECHO was the one command whose output the shell
 * could work out for itself without running it.
 *
 * With this, a command does not know where it is writing. The shell points it
 * at the terminal, or at a buffer, and that one change is what makes `LS >
 * FILES.TXT` and `CAT NOTES | GREP TODO` both possible without any command
 * knowing either exists.
 *
 * Not a file descriptor, and not a stream. There are no processes here to give
 * descriptors to, and a stream implies something to flush. This is a place to
 * put characters, and it says when they did not fit.
 *
 * See M25 in docs/milestones.md.
 */
#ifndef ME_CMDOUT_H
#define ME_CMDOUT_H

#include <stdbool.h>
#include <stdint.h>

#include "term.h"

struct cmd_out {
    /* The terminal, when the output is for a person to read now. NULL when it
     * is being captured. Exactly one of these two is set. */
    struct term *term;
    char *buffer;
    uint64_t capacity;
    uint64_t written;
    /* True once something did not fit.
     *
     * Said rather than swallowed. A file quietly holding the first half of a
     * listing is worse than one that was not written at all, because only the
     * second looks like a failure. */
    bool overflowed;
};

void cmd_out_to_term(struct cmd_out *out, struct term *term);
void cmd_out_to_buffer(struct cmd_out *out, char *buffer, uint64_t capacity);

/* What the captured output says so far, always terminated. Empty when this is
 * going to the terminal, which has no way to be read back. */
const char *cmd_out_text(const struct cmd_out *out);

void cmd_print(struct cmd_out *out, const char *text);
void cmd_println(struct cmd_out *out, const char *text);
void cmd_newline(struct cmd_out *out);
void cmd_print_number(struct cmd_out *out, uint64_t value);

/* A number right aligned in a field, so a listing lines its sizes up. */
void cmd_print_padded(struct cmd_out *out, uint64_t value, uint64_t width);

#endif /* ME_CMDOUT_H */
