#include "cmdout.h"

void cmd_out_to_term(struct cmd_out *out, struct term *term)
{
    if (out != NULL) {
        *out = (struct cmd_out){0};
        out->term = term;
    }
}

void cmd_out_to_buffer(struct cmd_out *out, char *buffer, uint64_t capacity)
{
    if (out == NULL) {
        return;
    }
    *out = (struct cmd_out){0};
    out->buffer = buffer;
    out->capacity = capacity;
    if (buffer != NULL && capacity > 0) {
        buffer[0] = '\0';
    }
}

const char *cmd_out_text(const struct cmd_out *out)
{
    if (out == NULL || out->buffer == NULL) {
        return "";
    }
    return out->buffer;
}

/* One character, which everything else here is built from.
 *
 * The terminal is written a whole string at a time by `cmd_print` rather than
 * through this, because `term_print` already handles the line wrapping and the
 * scroll, and going through it a character at a time would do that work per
 * character for no gain.
 */
static void put(struct cmd_out *out, char ch)
{
    if (out->buffer == NULL || out->capacity == 0) {
        return;
    }
    /* One byte kept back for the terminator, always. A captured buffer is read
     * as a string by whatever runs next, and one that ran to the end of its
     * room without a terminator would be read past. */
    if (out->written + 1 >= out->capacity) {
        out->overflowed = true;
        return;
    }
    out->buffer[out->written++] = ch;
    out->buffer[out->written] = '\0';
}

void cmd_print(struct cmd_out *out, const char *text)
{
    if (out == NULL || text == NULL) {
        return;
    }
    if (out->term != NULL) {
        term_print(out->term, text);
        return;
    }
    for (uint64_t i = 0; text[i] != '\0'; i++) {
        put(out, text[i]);
    }
}

void cmd_newline(struct cmd_out *out)
{
    if (out == NULL) {
        return;
    }
    if (out->term != NULL) {
        term_newline(out->term);
        return;
    }
    put(out, '\n');
}

void cmd_println(struct cmd_out *out, const char *text)
{
    cmd_print(out, text);
    cmd_newline(out);
}

void cmd_print_number(struct cmd_out *out, uint64_t value)
{
    if (out == NULL) {
        return;
    }
    if (out->term != NULL) {
        term_print_number(out->term, value);
        return;
    }
    /* Backwards into a small buffer and then forwards out of it, because the
     * digits come out least significant first and nobody reads them that way.
     * Twenty is enough for every value a sixty four bit number can hold. */
    char digits[20];
    uint64_t at = 0;
    do {
        digits[at++] = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0 && at < sizeof digits);
    while (at > 0) {
        put(out, digits[--at]);
    }
}

void cmd_print_padded(struct cmd_out *out, uint64_t value, uint64_t width)
{
    uint64_t digits = 1;
    for (uint64_t v = value; v >= 10; v /= 10) {
        digits++;
    }
    while (digits < width) {
        cmd_print(out, " ");
        digits++;
    }
    cmd_print_number(out, value);
}
