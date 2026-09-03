/* The commands that answer questions about the machine itself.
 *
 * HELP, UPTIME, MEM, CPU and WINDOWS. Every one of them reports something the
 * kernel actually measured. Nothing here invents a process list or a network,
 * because there are none, and a shell that answers a question the machine
 * cannot answer is a mock up.
 *
 * See M19 in docs/milestones.md.
 */
#include "cmdinfo.h"

static void say_size(struct cmd_out *out, const char *label, uint64_t bytes)
{
    char size[32];
    cmd_format_size(bytes, size, sizeof size);
    cmd_print(out, label);
    cmd_println(out, size);
}

void cmdinfo_help(struct cmd_out *out)
{
    cmd_println(out, "THE MACHINE");
    cmd_println(out, "  VER      WHICH ME OS THIS IS");
    cmd_println(out, "  CPU      WHAT THE PROCESSOR SAYS IT IS");
    cmd_println(out, "  MEM      MEMORY THE BOOTLOADER REPORTED");
    cmd_println(out, "  RES      SCREEN SIZE");
    cmd_println(out, "  UPTIME   HOW LONG SINCE BOOT");
    cmd_println(out, "  DATE     THE TIME OF DAY, FROM THE CLOCK CHIP");
    cmd_println(out, "  WINDOWS  HOW MANY ARE OPEN");
    cmd_println(out, "FILES");
    cmd_println(out, "  PWD      WHERE YOU ARE");
    cmd_println(out, "  LS       WHAT IS HERE");
    cmd_println(out, "  CD       GO SOMEWHERE");
    cmd_println(out, "  MKDIR    MAKE A DIRECTORY");
    cmd_println(out, "  TOUCH    MAKE AN EMPTY FILE");
    cmd_println(out, "  CAT      SHOW A FILE");
    cmd_println(out, "  WRITE    PUT A LINE IN A FILE");
    cmd_println(out, "  RM       DELETE A FILE OR EMPTY DIRECTORY");
    cmd_println(out, "  MV       MOVE OR RENAME");
    cmd_println(out, "  CP       COPY A FILE");
    cmd_println(out, "  WC       COUNT LINES, WORDS AND BYTES");
    cmd_println(out, "  TREE     THE SHAPE OF A DIRECTORY");
    cmd_println(out, "TEXT");
    cmd_println(out, "  GREP     LINES HOLDING A WORD");
    cmd_println(out, "  HEAD     THE FIRST LINES, 10 BY DEFAULT");
    cmd_println(out, "  TAIL     THE LAST LINES");
    cmd_println(out, "  SORT     LINES IN ORDER");
    cmd_println(out, "  RUN      DO WHAT A FILE OF COMMANDS SAYS");
    cmd_println(out, "TAB FINISHES A NAME YOU HAVE STARTED TYPING.");
    cmd_println(out, "  EDIT     OPEN A FILE IN THE EDITOR");
    cmd_println(out, "  DF       HOW MUCH ROOM IS LEFT");
    cmd_println(out, "THIS TERMINAL");
    cmd_println(out, "  ECHO     SAY SOMETHING BACK");
    cmd_println(out, "  CLEAR    EMPTY THIS SCREEN");
    cmd_println(out, "  HELP     THIS LIST");
    cmd_println(out, "ANY COMMAND > FILE WRITES INSTEAD OF PRINTING,");
    cmd_println(out, "AND A | B GIVES A'S OUTPUT TO B. GREP, HEAD,");
    cmd_println(out, "TAIL, SORT, CAT AND WC READ WHAT THEY ARE GIVEN");
    cmd_println(out, "WHEN YOU NAME NO FILE. TRY LS | SORT > NAMES.TXT");
    cmd_println(out, "KEYS: CTRL ARROWS MOVE FOCUS, CTRL H HIDES,");
    cmd_println(out, "      CTRL S SHOWS ALL, CTRL N AND W RESIZE,");
    cmd_println(out, "      CTRL 1 TO 4 SWITCH WORKSPACE, CTRL M SENDS");
    cmd_println(out, "      THIS WINDOW TO THE NEXT ONE.");
}

void cmdinfo_uptime(struct cmd_context *context)
{
    const uint64_t seconds = context->uptime_seconds;
    cmd_print(context->out, "UP ");
    cmd_print_number(context->out, seconds / 3600);
    cmd_print(context->out, "H ");
    cmd_print_number(context->out, (seconds / 60) % 60);
    cmd_print(context->out, "M ");
    cmd_print_number(context->out, seconds % 60);
    cmd_println(context->out, "S");
}

void cmdinfo_mem(struct cmd_context *context)
{
    /* Said rather than printed as zero. A machine that could not be asked how
     * much memory it has is a different answer from one with none. */
    if (context->total_memory == 0) {
        cmd_println(context->out, "THE BOOTLOADER REPORTED NO MEMORY MAP");
        return;
    }
    say_size(context->out, "USABLE  ", context->usable_memory);
    say_size(context->out, "TOTAL   ", context->total_memory);
}

void cmdinfo_cpu(struct cmd_context *context)
{
    const bool has_vendor = context->cpu_vendor != NULL &&
                            context->cpu_vendor[0] != '\0';
    const bool has_brand = context->cpu_brand != NULL &&
                           context->cpu_brand[0] != '\0';
    if (!has_vendor && !has_brand) {
        cmd_println(context->out, "THE PROCESSOR WOULD NOT SAY");
        return;
    }
    if (has_vendor) {
        cmd_print(context->out, "VENDOR  ");
        cmd_println(context->out, context->cpu_vendor);
    }
    if (has_brand) {
        cmd_print(context->out, "BRAND   ");
        cmd_println(context->out, context->cpu_brand);
    }
}

void cmdinfo_windows(struct cmd_context *context)
{
    cmd_print(context->out, "OPEN    ");
    cmd_print_number(context->out, context->windows_open);
    cmd_newline(context->out);
    cmd_print(context->out, "SHOWING ");
    cmd_print_number(context->out, context->windows_visible);
    cmd_newline(context->out);
}
