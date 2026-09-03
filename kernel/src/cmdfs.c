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

static void complain(struct cmd_out *out, const char *what, enum vfs_result why)
{
    cmd_print(out, what);
    cmd_print(out, ": ");
    cmd_println(out, vfs_explain(why));
}

void cmdfs_pwd(struct cmd_context *context)
{
    char path[VFS_PATH_MAX];
    vfs_path_of(context->fs, context->fs->cwd, path, sizeof path);
    cmd_println(context->out, path);
}

void cmdfs_ls(struct cmd_context *context, const char *path)
{
    struct cmd_out *out = context->out;
    const char *where = path[0] == '\0' ? "." : path;

    const int16_t at = vfs_resolve(context->fs, where);
    if (at == VFS_NONE) {
        complain(out, where, VFS_NOT_FOUND);
        return;
    }

    const struct vfs_node *node = vfs_get(context->fs, at);
    /* A file names itself, which is what every other machine does when asked to
     * list one, and is more useful than an error. */
    if (node->kind == VFS_FILE) {
        cmd_print(out, "FILE ");
        cmd_print_padded(out, node->length, 6);
        cmd_print(out, "  ");
        cmd_println(out, node->name);
        return;
    }

    uint64_t files = 0;
    uint64_t directories = 0;
    for (int16_t child = vfs_first_child(context->fs, at); child != VFS_NONE;
         child = vfs_next_sibling(context->fs, child)) {
        const struct vfs_node *entry = vfs_get(context->fs, child);
        if (entry->kind == VFS_DIR) {
            cmd_print(out, "DIR  ");
            cmd_print_padded(out, vfs_count_children(context->fs, child), 6);
            directories++;
        } else {
            cmd_print(out, "FILE ");
            cmd_print_padded(out, entry->length, 6);
            files++;
        }
        cmd_print(out, "  ");
        cmd_print(out, entry->name);
        if (entry->kind == VFS_DIR) {
            cmd_print(out, "/");
        }
        cmd_newline(out);
    }

    if (files == 0 && directories == 0) {
        cmd_println(out, "(EMPTY)");
        return;
    }
    cmd_print_padded(out, directories, 1);
    cmd_print(out, " DIRECTORIES, ");
    cmd_print_padded(out, files, 1);
    cmd_println(out, " FILES");
}

void cmdfs_cd(struct cmd_context *context, const char *path)
{
    /* With nowhere named, the root. Every shell goes somewhere sensible rather
     * than complaining when CD is typed on its own. */
    const char *where = path[0] == '\0' ? "/" : path;
    const enum vfs_result done = vfs_chdir(context->fs, where);
    if (done != VFS_OK) {
        complain(context->out, where, done);
        return;
    }
    cmdfs_pwd(context);
}

void cmdfs_mkdir(struct cmd_context *context, const char *path)
{
    if (path[0] == '\0') {
        cmd_println(context->out, "MKDIR NEEDS A NAME");
        return;
    }
    const enum vfs_result done = vfs_mkdir(context->fs, path);
    if (done != VFS_OK) {
        complain(context->out, path, done);
    }
}

void cmdfs_touch(struct cmd_context *context, const char *path)
{
    if (path[0] == '\0') {
        cmd_println(context->out, "TOUCH NEEDS A NAME");
        return;
    }
    /* Already being there is not a failure. Touching a file that exists is a
     * thing people do on purpose. */
    const enum vfs_result done = vfs_create(context->fs, path);
    if (done != VFS_OK && done != VFS_EXISTS) {
        complain(context->out, path, done);
    }
}

void cmdfs_cat(struct cmd_context *context, const char *path)
{
    /* With no name it prints what was piped into it, which is what makes it
     * useful at the end of a pipe as well as at the start of one. */
    static char text[VFS_FILE_MAX + 1];
    if (!cmd_input_text(context, path, text, sizeof text, "CAT")) {
        return;
    }
    if (text[0] == '\0') {
        cmd_println(context->out, "(EMPTY FILE)");
        return;
    }
    cmd_println(context->out, text);
}

void cmdfs_rm(struct cmd_context *context, const char *path)
{
    if (path[0] == '\0') {
        cmd_println(context->out, "RM NEEDS A NAME");
        return;
    }
    const enum vfs_result done = vfs_remove(context->fs, path);
    if (done != VFS_OK) {
        complain(context->out, path, done);
    }
}

/* WRITE puts one line in a file, and is also what `ECHO ... > FILE` becomes.
 * Replacing rather than appending, because that is what a single arrow means
 * everywhere else. */
void cmdfs_write(struct cmd_context *context, const char *path, const char *text)
{
    if (path[0] == '\0') {
        cmd_println(context->out, "WRITE NEEDS A NAME AND SOMETHING TO WRITE");
        return;
    }
    const enum vfs_result done = vfs_write(context->fs, path, text);
    if (done != VFS_OK) {
        complain(context->out, path, done);
    }
}

/* Two paths out of one argument list, for MV and CP. */
static bool two_paths(struct cmd_context *context, const char *rest,
                      const char *what, char *from, uint64_t from_capacity,
                      const char **to)
{
    cmd_split(rest, from, from_capacity, to);
    if (from[0] == '\0' || (*to)[0] == '\0') {
        cmd_print(context->out, what);
        cmd_println(context->out, " NEEDS SOMETHING TO MOVE AND SOMEWHERE TO PUT IT");
        return false;
    }
    return true;
}

void cmdfs_mv(struct cmd_context *context, const char *rest)
{
    char from[VFS_PATH_MAX];
    const char *to = "";
    if (!two_paths(context, rest, "MV", from, sizeof from, &to)) {
        return;
    }
    const enum vfs_result done = vfs_move(context->fs, from, to);
    if (done != VFS_OK) {
        complain(context->out, from, done);
    }
}

void cmdfs_cp(struct cmd_context *context, const char *rest)
{
    char from[VFS_PATH_MAX];
    const char *to = "";
    if (!two_paths(context, rest, "CP", from, sizeof from, &to)) {
        return;
    }
    const enum vfs_result done = vfs_copy(context->fs, from, to);
    if (done != VFS_OK) {
        complain(context->out, from, done);
    }
}

void cmdfs_wc(struct cmd_context *context, const char *path)
{
    static char text[VFS_FILE_MAX + 1];
    if (!cmd_input_text(context, path, text, sizeof text, "WC")) {
        return;
    }
    uint64_t length = 0;
    while (text[length] != '\0') {
        length++;
    }

    uint64_t lines = length == 0 ? 0 : 1;
    uint64_t words = 0;
    bool in_word = false;
    for (uint64_t i = 0; i < length; i++) {
        if (text[i] == '\n') {
            lines++;
        }
        const bool blank = text[i] == ' ' || text[i] == '\n';
        if (!blank && !in_word) {
            words++;
        }
        in_word = !blank;
    }

    cmd_print_padded(context->out, lines, 5);
    cmd_print_padded(context->out, words, 7);
    cmd_print_padded(context->out, length, 8);
    cmd_print(context->out, "  ");
    cmd_println(context->out, path);
}

/* Walks the tree from `at`, drawing the shape of it with indentation. Depth
 * bounded, because the tree cannot contain itself but a bug in MV could once
 * have made it, and a recursive walk with no bottom takes the machine with it. */
static void draw_tree(struct cmd_context *context, int16_t at, uint64_t depth)
{
    if (depth > 8) {
        cmd_println(context->out, "  ... DEEPER THAN THIS WILL SHOW");
        return;
    }
    for (int16_t child = vfs_first_child(context->fs, at); child != VFS_NONE;
         child = vfs_next_sibling(context->fs, child)) {
        const struct vfs_node *node = vfs_get(context->fs, child);
        for (uint64_t i = 0; i < depth; i++) {
            cmd_print(context->out, "  ");
        }
        cmd_print(context->out, node->kind == VFS_DIR ? "+ " : "  ");
        cmd_print(context->out, node->name);
        if (node->kind == VFS_DIR) {
            cmd_print(context->out, "/");
            cmd_newline(context->out);
            draw_tree(context, child, depth + 1);
        } else {
            cmd_newline(context->out);
        }
    }
}

void cmdfs_tree(struct cmd_context *context, const char *path)
{
    const char *where = path[0] == '\0' ? "." : path;
    const int16_t at = vfs_resolve(context->fs, where);
    if (at == VFS_NONE) {
        complain(context->out, where, VFS_NOT_FOUND);
        return;
    }
    char full[VFS_PATH_MAX];
    vfs_path_of(context->fs, at, full, sizeof full);
    cmd_println(context->out, full);
    draw_tree(context, at, 1);
}

void cmdfs_df(struct cmd_context *context)
{
    struct cmd_out *out = context->out;
    cmd_print(out, "ENTRIES ");
    cmd_print_number(out, vfs_used_nodes(context->fs));
    cmd_print(out, " OF ");
    cmd_print_number(out, VFS_MAX_NODES);
    cmd_newline(out);
    /* Blocks, because that is the limit a person actually meets. The node table
     * runs out at ninety six names. The pool runs out at whatever the files in
     * them add up to, and that is the one that stops a document being saved. */
    cmd_print(out, "BLOCKS  ");
    cmd_print_number(out, vfs_used_blocks(context->fs));
    cmd_print(out, " OF ");
    cmd_print_number(out, VFS_MAX_BLOCKS);
    cmd_print(out, ", ");
    cmd_print_number(out, VFS_BLOCK);
    cmd_println(out, " BYTES EACH");
    cmd_print(out, "PER FILE ");
    cmd_print_number(out, VFS_FILE_MAX);
    cmd_println(out, " BYTES AT MOST");

    /* The one thing about this filesystem that will surprise somebody, said
     * every time. Which of the two it is depends on the machine, and getting it
     * the wrong way round would either promise a disk that is not there or hide
     * one that is. */
    if (context->disk_model == NULL || context->disk_model[0] == '\0') {
        cmd_println(out, "NO DISK FOUND, SO THIS IS IN MEMORY ONLY");
        cmd_println(out, "AND NONE OF IT SURVIVES A RESTART.");
        return;
    }
    cmd_print(out, "DISK ");
    cmd_println(out, context->disk_model);
    cmd_print(out, "     ");
    cmd_print_number(out, context->disk_sectors);
    cmd_print(out, " SECTORS OF ");
    cmd_print_number(out, context->disk_sector_bytes);
    cmd_println(out, " BYTES");
    cmd_println(out, "SAVED AFTER EVERY CHANGE, SO IT SURVIVES");
    cmd_println(out, "A RESTART. THERE IS NOTHING TO TYPE.");
}
