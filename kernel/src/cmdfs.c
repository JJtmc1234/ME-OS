/* The filesystem half of the shell: PWD, LS, CD, MKDIR, TOUCH, CAT, RM.
 *
 * Split from `cmd.c` because the two halves answer different questions. One
 * asks the machine what it is, the other moves around a tree. Together they
 * would be one file nobody could read in a sitting.
 *
 * Every one of these does the real thing to the real filesystem in `vfs.c`.
 * None of them prints an answer somebody wrote down.
 */
#include "cmd.h"

#include "vfs.h"

/* A number right aligned in a field, so a listing lines up its sizes. */
static void print_padded(struct term *term, uint64_t value, uint64_t width)
{
    uint64_t digits = 1;
    for (uint64_t v = value; v >= 10; v /= 10) {
        digits++;
    }
    while (digits < width) {
        term_print(term, " ");
        digits++;
    }
    term_print_number(term, value);
}

static void complain(struct term *term, const char *what, enum vfs_result why)
{
    term_print(term, what);
    term_print(term, ": ");
    term_println(term, vfs_explain(why));
}

void cmdfs_pwd(struct cmd_context *context)
{
    char path[VFS_PATH_MAX];
    vfs_path_of(context->fs, context->fs->cwd, path, sizeof path);
    term_println(context->term, path);
}

void cmdfs_ls(struct cmd_context *context, const char *path)
{
    struct term *term = context->term;
    const char *where = path[0] == '\0' ? "." : path;

    const int16_t at = vfs_resolve(context->fs, where);
    if (at == VFS_NONE) {
        complain(term, where, VFS_NOT_FOUND);
        return;
    }

    const struct vfs_node *node = vfs_get(context->fs, at);
    /* A file names itself, which is what every other machine does when asked to
     * list one, and is more useful than an error. */
    if (node->kind == VFS_FILE) {
        term_print(term, "FILE ");
        print_padded(term, node->length, 6);
        term_print(term, "  ");
        term_println(term, node->name);
        return;
    }

    uint64_t files = 0;
    uint64_t directories = 0;
    for (int16_t child = vfs_first_child(context->fs, at); child != VFS_NONE;
         child = vfs_next_sibling(context->fs, child)) {
        const struct vfs_node *entry = vfs_get(context->fs, child);
        if (entry->kind == VFS_DIR) {
            term_print(term, "DIR  ");
            print_padded(term, vfs_count_children(context->fs, child), 6);
            directories++;
        } else {
            term_print(term, "FILE ");
            print_padded(term, entry->length, 6);
            files++;
        }
        term_print(term, "  ");
        term_print(term, entry->name);
        if (entry->kind == VFS_DIR) {
            term_print(term, "/");
        }
        term_newline(term);
    }

    if (files == 0 && directories == 0) {
        term_println(term, "(EMPTY)");
        return;
    }
    print_padded(term, directories, 1);
    term_print(term, " DIRECTORIES, ");
    print_padded(term, files, 1);
    term_println(term, " FILES");
}

void cmdfs_cd(struct cmd_context *context, const char *path)
{
    /* With nowhere named, the root. Every shell goes somewhere sensible rather
     * than complaining when CD is typed on its own. */
    const char *where = path[0] == '\0' ? "/" : path;
    const enum vfs_result done = vfs_chdir(context->fs, where);
    if (done != VFS_OK) {
        complain(context->term, where, done);
        return;
    }
    cmdfs_pwd(context);
}

void cmdfs_mkdir(struct cmd_context *context, const char *path)
{
    if (path[0] == '\0') {
        term_println(context->term, "MKDIR NEEDS A NAME");
        return;
    }
    const enum vfs_result done = vfs_mkdir(context->fs, path);
    if (done != VFS_OK) {
        complain(context->term, path, done);
    }
}

void cmdfs_touch(struct cmd_context *context, const char *path)
{
    if (path[0] == '\0') {
        term_println(context->term, "TOUCH NEEDS A NAME");
        return;
    }
    /* Already being there is not a failure. Touching a file that exists is a
     * thing people do on purpose. */
    const enum vfs_result done = vfs_create(context->fs, path);
    if (done != VFS_OK && done != VFS_EXISTS) {
        complain(context->term, path, done);
    }
}

void cmdfs_cat(struct cmd_context *context, const char *path)
{
    if (path[0] == '\0') {
        term_println(context->term, "CAT NEEDS A NAME");
        return;
    }
    char text[VFS_FILE_MAX + 1];
    uint64_t length = 0;
    const enum vfs_result done =
        vfs_read(context->fs, path, text, sizeof text, &length);
    if (done != VFS_OK) {
        complain(context->term, path, done);
        return;
    }
    if (length == 0) {
        term_println(context->term, "(EMPTY FILE)");
        return;
    }
    term_println(context->term, text);
}

void cmdfs_rm(struct cmd_context *context, const char *path)
{
    if (path[0] == '\0') {
        term_println(context->term, "RM NEEDS A NAME");
        return;
    }
    const enum vfs_result done = vfs_remove(context->fs, path);
    if (done != VFS_OK) {
        complain(context->term, path, done);
    }
}

/* WRITE puts one line in a file, and is also what `ECHO ... > FILE` becomes.
 * Replacing rather than appending, because that is what a single arrow means
 * everywhere else. */
void cmdfs_write(struct cmd_context *context, const char *path, const char *text)
{
    if (path[0] == '\0') {
        term_println(context->term, "WRITE NEEDS A NAME AND SOMETHING TO WRITE");
        return;
    }
    const enum vfs_result done = vfs_write(context->fs, path, text);
    if (done != VFS_OK) {
        complain(context->term, path, done);
    }
}

void cmdfs_df(struct cmd_context *context)
{
    struct term *term = context->term;
    const uint64_t used = vfs_used_nodes(context->fs);
    term_print(term, "ENTRIES ");
    term_print_number(term, used);
    term_print(term, " OF ");
    term_print_number(term, VFS_MAX_NODES);
    term_newline(term);
    term_print(term, "PER FILE ");
    term_print_number(term, VFS_FILE_MAX);
    term_println(term, " BYTES");
    /* Said every time, because it is the one thing about this filesystem that
     * will surprise somebody who has used another one. */
    term_println(term, "IN MEMORY ONLY. THERE IS NO DISK DRIVER YET,");
    term_println(term, "SO NONE OF THIS SURVIVES A RESTART.");
}
