#include "cmd.h"

static bool same(const char *a, const char *b)
{
    for (uint64_t i = 0;; i++) {
        if (a[i] != b[i]) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }
}

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

static void say_size(struct term *term, const char *label, uint64_t bytes)
{
    char size[32];
    cmd_format_size(bytes, size, sizeof size);
    term_print(term, label);
    term_println(term, size);
}

static void command_help(struct term *term)
{
    term_println(term, "COMMANDS");
    term_println(term, "  HELP     THIS LIST");
    term_println(term, "  VER      WHICH ME OS THIS IS");
    term_println(term, "  CPU      WHAT THE PROCESSOR SAYS IT IS");
    term_println(term, "  MEM      MEMORY THE BOOTLOADER REPORTED");
    term_println(term, "  RES      SCREEN SIZE");
    term_println(term, "  UPTIME   HOW LONG SINCE BOOT");
    term_println(term, "  WINDOWS  HOW MANY ARE OPEN");
    term_println(term, "  ECHO     SAY SOMETHING BACK");
    term_println(term, "  CLEAR    EMPTY THIS SCREEN");
    term_println(term, "KEYS: CTRL ARROWS MOVE FOCUS, CTRL H HIDES,");
    term_println(term, "      CTRL S SHOWS ALL, CTRL N AND W RESIZE.");
}

static void command_uptime(struct cmd_context *context)
{
    const uint64_t seconds = context->uptime_seconds;
    term_print(context->term, "UP ");
    term_print_number(context->term, seconds / 3600);
    term_print(context->term, "H ");
    term_print_number(context->term, (seconds / 60) % 60);
    term_print(context->term, "M ");
    term_print_number(context->term, seconds % 60);
    term_println(context->term, "S");
}

static void command_mem(struct cmd_context *context)
{
    /* Said rather than printed as zero. A machine that could not be asked how
     * much memory it has is a different answer from one with none. */
    if (context->total_memory == 0) {
        term_println(context->term, "THE BOOTLOADER REPORTED NO MEMORY MAP");
        return;
    }
    say_size(context->term, "USABLE  ", context->usable_memory);
    say_size(context->term, "TOTAL   ", context->total_memory);
}

static void command_cpu(struct cmd_context *context)
{
    const bool has_vendor = context->cpu_vendor != NULL &&
                            context->cpu_vendor[0] != '\0';
    const bool has_brand = context->cpu_brand != NULL &&
                           context->cpu_brand[0] != '\0';
    if (!has_vendor && !has_brand) {
        term_println(context->term, "THE PROCESSOR WOULD NOT SAY");
        return;
    }
    if (has_vendor) {
        term_print(context->term, "VENDOR  ");
        term_println(context->term, context->cpu_vendor);
    }
    if (has_brand) {
        term_print(context->term, "BRAND   ");
        term_println(context->term, context->cpu_brand);
    }
}

static void command_windows(struct cmd_context *context)
{
    term_print(context->term, "OPEN    ");
    term_print_number(context->term, context->windows_open);
    term_newline(context->term);
    term_print(context->term, "SHOWING ");
    term_print_number(context->term, context->windows_visible);
    term_newline(context->term);
}

void cmd_run(struct cmd_context *context, const char *line)
{
    if (context == NULL || context->term == NULL) {
        return;
    }
    struct term *term = context->term;

    char echoed[TERM_MAX_COLS + 8];
    uint64_t written = 0;
    for (const char *p = "ME> "; *p != '\0' && written + 1 < sizeof echoed; p++) {
        echoed[written++] = *p;
    }
    for (uint64_t i = 0; line != NULL && line[i] != '\0' &&
                         written + 1 < sizeof echoed; i++) {
        echoed[written++] = line[i];
    }
    echoed[written] = '\0';
    /* The line goes into the history before it runs, so what a command printed
     * is underneath the command that printed it. */
    term_println(term, echoed);

    char name[32];
    const char *rest = "";
    if (cmd_split(line, name, sizeof name, &rest) == 0) {
        return;
    }

    if (same(name, "HELP")) {
        command_help(term);
    } else if (same(name, "VER")) {
        term_print(term, "ME OS ");
        term_println(term, context->version != NULL ? context->version : "UNKNOWN");
    } else if (same(name, "CPU")) {
        command_cpu(context);
    } else if (same(name, "MEM")) {
        command_mem(context);
    } else if (same(name, "RES")) {
        term_print_number(term, context->screen_width);
        term_print(term, "X");
        term_print_number(term, context->screen_height);
        term_println(term, " 32 BPP");
    } else if (same(name, "UPTIME")) {
        command_uptime(context);
    } else if (same(name, "WINDOWS")) {
        command_windows(context);
    } else if (same(name, "ECHO")) {
        term_println(term, rest);
    } else if (same(name, "CLEAR")) {
        term_clear(term);
    } else {
        term_print(term, name);
        term_println(term, ": NO SUCH COMMAND. TRY HELP.");
    }
}
