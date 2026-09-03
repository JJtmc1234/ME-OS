/* Cutting a typed line into the pieces the shell acts on.
 *
 * The command word, the arrow that sends output to a file, the bar that sends
 * it to another command, and a size in bytes written so a person can read it.
 * All of it is pure: nothing here touches the terminal, the filesystem or the
 * machine, so every rule about where the blanks go can be checked directly.
 *
 * See M19 and M25 in docs/milestones.md.
 */
#include "cmd.h"


static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

uint64_t cmd_split(const char *line, char *name, uint64_t capacity,
                   const char **rest)
{
    if (name == NULL || capacity == 0) {
        return 0;
    }
    name[0] = '\0';
    if (rest != NULL) {
        *rest = "";
    }
    if (line == NULL) {
        return 0;
    }

    uint64_t at = 0;
    while (line[at] == ' ') {
        at++;
    }

    uint64_t written = 0;
    while (line[at] != '\0' && line[at] != ' ' && written + 1 < capacity) {
        name[written++] = upper(line[at++]);
    }
    name[written] = '\0';

    /* Past the rest of the word if it did not fit, so the arguments are the
     * arguments and not the tail of a command name that was too long. */
    while (line[at] != '\0' && line[at] != ' ') {
        at++;
    }
    while (line[at] == ' ') {
        at++;
    }
    if (rest != NULL) {
        *rest = line + at;
    }
    return written;
}

void cmd_format_size(uint64_t bytes, char *out, uint64_t capacity)
{
    if (out == NULL || capacity == 0) {
        return;
    }
    /* Whole units with one decimal, worked out with integers. There is no
     * floating point in this file on purpose: the kernel builds with SSE off
     * everywhere except the one file that turns a triangle. */
    static const char *const units[] = { "B", "KB", "MB", "GB", "TB" };
    uint64_t unit = 0;
    uint64_t whole = bytes;
    uint64_t tenths = 0;

    while (whole >= 1024 && unit + 1 < sizeof units / sizeof units[0]) {
        tenths = ((whole % 1024) * 10) / 1024;
        whole /= 1024;
        unit++;
    }

    char digits[24];
    uint64_t n = 0;
    uint64_t value = whole;
    if (value == 0) {
        digits[n++] = '0';
    }
    while (value > 0 && n < sizeof digits) {
        digits[n++] = (char)('0' + (value % 10));
        value /= 10;
    }

    uint64_t written = 0;
    while (n > 0 && written + 1 < capacity) {
        out[written++] = digits[--n];
    }
    if (unit > 0 && written + 3 < capacity) {
        out[written++] = '.';
        out[written++] = (char)('0' + tenths);
    }
    if (written + 1 < capacity) {
        out[written++] = ' ';
    }
    for (const char *p = units[unit]; *p != '\0' && written + 1 < capacity; p++) {
        out[written++] = *p;
    }
    out[written] = '\0';
}

bool cmd_split_redirect(const char *line, char *command, uint64_t command_capacity,
                        char *target, uint64_t target_capacity)
{
    if (line == NULL || command == NULL || target == NULL ||
        command_capacity == 0 || target_capacity == 0) {
        return false;
    }
    uint64_t arrow = 0;
    bool found = false;
    for (uint64_t i = 0; line[i] != '\0'; i++) {
        if (line[i] == '>') {
            arrow = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }

    /* Trailing blanks trimmed from the command, leading ones from the name, so
     * `ECHO HI > NOTES` writes to NOTES and not to " NOTES". */
    uint64_t end = arrow;
    while (end > 0 && line[end - 1] == ' ') {
        end--;
    }
    uint64_t written = 0;
    for (uint64_t i = 0; i < end && written + 1 < command_capacity; i++) {
        command[written++] = line[i];
    }
    command[written] = '\0';

    uint64_t at = arrow + 1;
    while (line[at] == ' ') {
        at++;
    }
    written = 0;
    while (line[at] != '\0' && line[at] != ' ' && written + 1 < target_capacity) {
        target[written++] = line[at++];
    }
    target[written] = '\0';
    return target[0] != '\0' && command[0] != '\0';
}

/* One command, already told where to write and what it was handed.
 *
 * Split out from `cmd_run` so a pipeline can call it more than once. Nothing in
 * here knows whether its output is going to the screen, into a file, or into
 * the next command, which is what makes all three work without every command
 * having a case for each.
 */

bool cmd_split_pipe(const char *line, char *first, uint64_t first_capacity,
                    const char **rest)
{
    if (line == NULL || first == NULL || rest == NULL || first_capacity == 0) {
        return false;
    }
    uint64_t bar = 0;
    bool found = false;
    for (uint64_t i = 0; line[i] != '\0'; i++) {
        if (line[i] == '|') {
            bar = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }

    uint64_t end = bar;
    while (end > 0 && line[end - 1] == ' ') {
        end--;
    }
    uint64_t written = 0;
    for (uint64_t i = 0; i < end && written + 1 < first_capacity; i++) {
        first[written++] = line[i];
    }
    first[written] = '\0';

    uint64_t at = bar + 1;
    while (line[at] == ' ') {
        at++;
    }
    *rest = line + at;
    return true;
}

/* The two buffers a pipeline passes its output through.
 *
 * Static rather than on the stack. Two of them is twelve kilobytes, which is
 * more than a kernel stack should be asked for, and nothing here runs twice at
 * once: there is one terminal and it runs one command at a time.
 *
 * Two is enough however long the pipeline is. A stage reads one and writes the
 * other, and the one it read is free again as soon as it has finished.
 */
