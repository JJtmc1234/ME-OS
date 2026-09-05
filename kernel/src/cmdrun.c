/* RUN, which reads a file and does what it says.
 *
 * The machine could make files, change them, search them and keep them. It
 * could not do anything with one except read it back. This is the difference
 * between a filesystem and a thing you can teach.
 *
 * A script is a file of the same lines you would type. No variables, no loops,
 * no conditions. Those are a language, and a shell that grows one accidentally
 * grows it badly. What this adds is the ability to write down a sequence you
 * do often and run it again, which is what most scripts anybody writes are.
 *
 * See M27 in docs/milestones.md.
 */
#include "cmd.h"

#include "cmdexec.h"
#include "elf.h"
#include "vfs.h"

/* One copy of the script for each depth.
 *
 * A script may run another script, so this is read into while an outer one is
 * still being walked. One buffer shared between them would have the inner
 * script's text replace the outer one's halfway through running it, and the
 * outer script would carry on reading lines out of a file it never opened.
 */
static char script[CMD_MAX_DEPTH][VFS_FILE_MAX + 1];
static uint64_t running;

void cmdrun_script(struct cmd_context *context, const char *path)
{
    if (path == NULL || path[0] == '\0') {
        cmd_println(context->out, "RUN NEEDS THE NAME OF A FILE");
        return;
    }
    if (running >= CMD_MAX_DEPTH) {
        /* `cmd_run` has its own guard on the same limit and would catch this
         * too. Caught here as well so the file is never read: a script that
         * runs itself would otherwise load a copy per level on the way down to
         * being refused. */
        cmd_println(context->out, "THAT IS TOO MANY SCRIPTS INSIDE SCRIPTS");
        return;
    }

    char *const text = script[running];
    uint64_t length = 0;
    const enum vfs_result found =
        vfs_read(context->fs, path, text, VFS_FILE_MAX + 1, &length);
    if (found != VFS_OK) {
        cmd_print(context->out, path);
        cmd_print(context->out, ": ");
        cmd_println(context->out, vfs_explain(found));
        return;
    }
    /* What kind of file this is, decided by looking at it rather than at its
     * name. A name is a claim and the first four bytes are evidence, which is
     * why every Unix does it this way. */
    if (elf_looks_like_elf((const uint8_t *)text, length)) {
        cmdexec_program(context, path, (const uint8_t *)text, length);
        return;
    }

    running++;

    char line[TERM_INPUT_MAX];
    uint64_t at = 0;
    while (at < length) {
        uint64_t end = at;
        while (end < length && text[end] != '\n') {
            end++;
        }

        uint64_t written = 0;
        for (uint64_t i = at; i < end && written + 1 < sizeof line; i++) {
            line[written++] = text[i];
        }
        line[written] = '\0';
        at = end < length ? end + 1 : length;

        /* Blank lines and comments are skipped rather than run. A script wants
         * to be readable by whoever wrote it a month ago, and the `#` is what
         * every other machine uses for saying so. */
        uint64_t first = 0;
        while (line[first] == ' ') {
            first++;
        }
        if (line[first] == '\0' || line[first] == '#') {
            continue;
        }

        cmd_run(context, line + first);
    }

    running--;
}
