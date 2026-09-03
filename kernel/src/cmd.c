/* Running a line: which command it names, and where its output goes.
 *
 * Two things live here. `run_one` is the list of every command the shell knows,
 * which is the one place to look when asking what it can do. `cmd_run` is the
 * pipeline around it: the arrow, the bars, and the buffers the output passes
 * through between stages.
 *
 * Cutting the line up is in `cmdline.c`, the machine's own answers are in
 * `cmdinfo.c`, files are in `cmdfs.c`, and the text filters are in `cmdtext.c`
 * and `cmdsort.c`.
 *
 * See M19 and M25 in docs/milestones.md.
 */
#include "cmd.h"

#include "cmdinfo.h"

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


static void run_one(struct cmd_context *context, const char *line)
{
    struct cmd_out *out = context->out;
    char name[32];
    const char *rest = "";
    if (cmd_split(line, name, sizeof name, &rest) == 0) {
        return;
    }

    if (context->fs != NULL) {
        if (same(name, "PWD")) {
            cmdfs_pwd(context);
            return;
        }
        if (same(name, "LS") || same(name, "DIR")) {
            cmdfs_ls(context, rest);
            return;
        }
        if (same(name, "CD")) {
            cmdfs_cd(context, rest);
            return;
        }
        if (same(name, "MKDIR")) {
            cmdfs_mkdir(context, rest);
            return;
        }
        if (same(name, "TOUCH")) {
            cmdfs_touch(context, rest);
            return;
        }
        if (same(name, "CAT")) {
            cmdfs_cat(context, rest);
            return;
        }
        if (same(name, "RM") || same(name, "RMDIR")) {
            cmdfs_rm(context, rest);
            return;
        }
        if (same(name, "DF")) {
            cmdfs_df(context);
            return;
        }
        if (same(name, "MV")) {
            cmdfs_mv(context, rest);
            return;
        }
        if (same(name, "CP")) {
            cmdfs_cp(context, rest);
            return;
        }
        if (same(name, "WC")) {
            cmdfs_wc(context, rest);
            return;
        }
        if (same(name, "GREP") || same(name, "FIND")) {
            cmdtext_grep(context, rest);
            return;
        }
        if (same(name, "HEAD")) {
            cmdtext_head(context, rest);
            return;
        }
        if (same(name, "TAIL")) {
            cmdtext_tail(context, rest);
            return;
        }
        if (same(name, "SORT")) {
            cmdsort_run(context, rest);
            return;
        }
        if (same(name, "RUN")) {
            cmdrun_script(context, rest);
            return;
        }
        if (same(name, "TREE")) {
            cmdfs_tree(context, rest);
            return;
        }
        if (same(name, "EDIT")) {
            if (rest[0] == '\0') {
                cmd_println(out, "EDIT NEEDS A NAME");
                return;
            }
            /* Said rather than done. The shell cannot open a window, so it
             * writes down what it wants and the caller, which can, acts on it
             * after this returns. */
            uint64_t i = 0;
            for (; rest[i] != '\0' && rest[i] != ' ' &&
                   i + 1 < sizeof context->open_editor; i++) {
                context->open_editor[i] = rest[i];
            }
            context->open_editor[i] = '\0';
            return;
        }
        if (same(name, "WRITE")) {
            /* The name, then everything after it, so a line with spaces in it
             * lands in the file whole. */
            char path[VFS_PATH_MAX];
            const char *text = "";
            cmd_split(rest, path, sizeof path, &text);
            cmdfs_write(context, path, text);
            return;
        }
    }

    if (same(name, "HELP")) {
        cmdinfo_help(out);
    } else if (same(name, "VER")) {
        cmd_print(out, "ME OS ");
        cmd_println(out, context->version != NULL ? context->version : "UNKNOWN");
    } else if (same(name, "CPU")) {
        cmdinfo_cpu(context);
    } else if (same(name, "MEM")) {
        cmdinfo_mem(context);
    } else if (same(name, "RES")) {
        cmd_print_number(out, context->screen_width);
        cmd_print(out, "X");
        cmd_print_number(out, context->screen_height);
        cmd_println(out, " 32 BPP");
    } else if (same(name, "UPTIME")) {
        cmdinfo_uptime(context);
    } else if (same(name, "DATE") || same(name, "TIME")) {
        /* A machine that could not be asked says so. A wrong clock is worse
         * than a missing one, because nothing downstream can tell. */
        if (context->date == NULL || context->date[0] == '\0') {
            cmd_println(out, "THE CLOCK CHIP WOULD NOT ANSWER");
        } else {
            cmd_print(out, context->date);
            cmd_print(out, " ");
            cmd_println(out, context->time);
        }
    } else if (same(name, "WINDOWS")) {
        cmdinfo_windows(context);
    } else if (same(name, "ECHO")) {
        cmd_println(out, rest);
    } else if (same(name, "CLEAR")) {
        /* The one command that is about the screen rather than about output.
         * There is nothing for it to write, so it reaches past the sink to the
         * terminal, and piping it does nothing, which is right. */
        term_clear(context->term);
    } else {
        cmd_print(out, name);
        cmd_println(out, ": NO SUCH COMMAND. TRY HELP.");
    }
}

/* The buffers a pipeline passes its output through, two for each depth.
 *
 * Two is enough however long one pipeline is: a stage reads one and writes the
 * other, and the one it read is free again as soon as it has finished.
 *
 * A set for each depth is what M27 needed. A script runs its lines through
 * `cmd_run`, so `cmd_run` calls itself, and one pair shared between the outer
 * pipeline and the inner one would have the script writing over the output of
 * the command that started it. Nothing would report an error: the outer command
 * would simply produce the inner one's answer.
 *
 * Static rather than on the stack, because a pair is twelve kilobytes and a
 * kernel stack should not be asked for that.
 */
static char pipe_a[CMD_MAX_DEPTH][VFS_FILE_MAX + 1];
static char pipe_b[CMD_MAX_DEPTH][VFS_FILE_MAX + 1];

/* How deep the shell is running commands inside commands. Only RUN increases
 * it, and it is the bound on both the buffers above and on a script that runs
 * itself. */
static uint64_t depth;

void cmd_run(struct cmd_context *context, const char *line)
{
    if (context == NULL || context->term == NULL || context->out == NULL) {
        return;
    }
    if (depth >= CMD_MAX_DEPTH) {
        /* A script that runs itself, most likely. Said and stopped rather than
         * followed down until the stack runs out, which on a machine with no
         * memory protection is not an error message but a dead machine. */
        term_println(context->term,
                     "THAT IS TOO MANY COMMANDS INSIDE COMMANDS. STOPPED.");
        return;
    }
    const uint64_t level = depth++;

    struct cmd_out *const screen = context->out;

    /* The prompt the line was typed at, not a fixed one, so the history shows
     * which directory each command was run in. */
    char echoed[TERM_MAX_COLS + TERM_INPUT_MAX + 8];
    uint64_t written = 0;
    for (const char *p = context->term->prompt;
         *p != '\0' && written + 1 < sizeof echoed; p++) {
        echoed[written++] = *p;
    }
    for (uint64_t i = 0; line != NULL && line[i] != '\0' &&
                         written + 1 < sizeof echoed; i++) {
        echoed[written++] = line[i];
    }
    echoed[written] = '\0';
    /* The line goes into the history before it runs, so what a command printed
     * is underneath the command that printed it. */
    term_println(context->term, echoed);

    /* The arrow is taken off first, because it applies to the whole pipeline
     * rather than to the last stage of it. `LS | GREP TXT > FOUND` writes what
     * came out of the far end. */
    char work[TERM_INPUT_MAX];
    char target[VFS_PATH_MAX];
    const bool to_file = context->fs != NULL &&
                         cmd_split_redirect(line, work, sizeof work,
                                            target, sizeof target);
    if (!to_file) {
        uint64_t at = 0;
        for (; line != NULL && line[at] != '\0' && at + 1 < sizeof work; at++) {
            work[at] = line[at];
        }
        work[at] = '\0';
    }
    /* An arrow that did not split is an arrow with nothing usable either side
     * of it. Saying so beats running the line as it stands, which would take
     * the arrow itself as an argument and report a file called `>`. */
    if (!to_file) {
        for (uint64_t i = 0; line[i] != '\0'; i++) {
            if (line[i] == '>') {
                term_println(context->term,
                             "AN ARROW NEEDS A COMMAND AND A NAME FOR THE FILE");
                depth--;
                return;
            }
        }
    }

    struct cmd_out *const caller_out = context->out;
    const char *const caller_input = context->input;

    const char *remaining = work;
    const char *carried = NULL;
    /* The buffer the last captured stage wrote into, so the newline can be
     * trimmed off it in place. NULL when nothing was captured. */
    char *held = NULL;
    char stage[TERM_INPUT_MAX];
    struct cmd_out capture;
    bool cut = false;

    for (uint64_t n = 0; ; n++) {
        const char *next = NULL;
        const bool more = cmd_split_pipe(remaining, stage, sizeof stage, &next);
        if (!more) {
            uint64_t at = 0;
            for (; remaining[at] != '\0' && at + 1 < sizeof stage; at++) {
                stage[at] = remaining[at];
            }
            stage[at] = '\0';
        }

        /* The last stage writes to the screen, unless the whole line was
         * pointed at a file, in which case it is captured like the others and
         * what it captured is written out afterwards. */
        const bool last = !more;
        if (last && !to_file) {
            context->out = screen;
        } else {
            held = n % 2 == 0 ? pipe_a[level] : pipe_b[level];
            cmd_out_to_buffer(&capture, held, VFS_FILE_MAX + 1);
            context->out = &capture;
        }
        context->input = carried;
        run_one(context, stage);

        if (context->out != screen) {
            cut = cut || capture.overflowed;
            carried = cmd_out_text(&capture);
        }
        if (last) {
            break;
        }
        remaining = next;
    }

    context->out = caller_out;
    context->input = caller_input;

    /* Said, not swallowed. A file holding the first part of a listing is worse
     * than one that was never written, because only the second looks wrong. */
    if (cut) {
        term_println(context->term,
                     "THAT PRODUCED MORE THAN ONE FILE CAN HOLD, SO IT WAS CUT");
    }
    if (to_file && held != NULL) {
        /* One trailing newline taken off.
         *
         * Every command ends its last line, so captured output always has a
         * newline on the end that nobody asked for. This filesystem holds a
         * file as lines with nothing after the last one, which is what WRITE
         * puts in and what CAT expects to find, and a spare newline would show
         * as a blank line every time the file was read. Only one comes off, so
         * a file that really does end in a blank line still can.
         */
        uint64_t at = 0;
        while (held[at] != '\0') {
            at++;
        }
        if (at > 0 && held[at - 1] == '\n') {
            held[at - 1] = '\0';
        }
        const enum vfs_result done = vfs_write(context->fs, target, held);
        if (done != VFS_OK) {
            term_print(context->term, target);
            term_print(context->term, ": ");
            term_println(context->term, vfs_explain(done));
        }
    }

    depth--;
}
