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

/* Two paths out of one argument list, for MV and CP. */
static bool two_paths(struct cmd_context *context, const char *rest,
                      const char *what, char *from, uint64_t from_capacity,
                      const char **to)
{
    cmd_split(rest, from, from_capacity, to);
    if (from[0] == '\0' || (*to)[0] == '\0') {
        term_print(context->term, what);
        term_println(context->term, " NEEDS SOMETHING TO MOVE AND SOMEWHERE TO PUT IT");
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
        complain(context->term, from, done);
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
        complain(context->term, from, done);
    }
}

void cmdfs_wc(struct cmd_context *context, const char *path)
{
    if (path[0] == '\0') {
        term_println(context->term, "WC NEEDS A NAME");
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

    print_padded(context->term, lines, 5);
    print_padded(context->term, words, 7);
    print_padded(context->term, length, 8);
    term_print(context->term, "  ");
    term_println(context->term, path);
}

/* Walks the tree from `at`, drawing the shape of it with indentation. Depth
 * bounded, because the tree cannot contain itself but a bug in MV could once
 * have made it, and a recursive walk with no bottom takes the machine with it. */
static void draw_tree(struct cmd_context *context, int16_t at, uint64_t depth)
{
    if (depth > 8) {
        term_println(context->term, "  ... DEEPER THAN THIS WILL SHOW");
        return;
    }
    for (int16_t child = vfs_first_child(context->fs, at); child != VFS_NONE;
         child = vfs_next_sibling(context->fs, child)) {
        const struct vfs_node *node = vfs_get(context->fs, child);
        for (uint64_t i = 0; i < depth; i++) {
            term_print(context->term, "  ");
        }
        term_print(context->term, node->kind == VFS_DIR ? "+ " : "  ");
        term_print(context->term, node->name);
        if (node->kind == VFS_DIR) {
            term_print(context->term, "/");
            term_newline(context->term);
            draw_tree(context, child, depth + 1);
        } else {
            term_newline(context->term);
        }
    }
}

void cmdfs_tree(struct cmd_context *context, const char *path)
{
    const char *where = path[0] == '\0' ? "." : path;
    const int16_t at = vfs_resolve(context->fs, where);
    if (at == VFS_NONE) {
        complain(context->term, where, VFS_NOT_FOUND);
        return;
    }
    char full[VFS_PATH_MAX];
    vfs_path_of(context->fs, at, full, sizeof full);
    term_println(context->term, full);
    draw_tree(context, at, 1);
}

void cmdfs_df(struct cmd_context *context)
{
    struct term *term = context->term;
    term_print(term, "ENTRIES ");
    term_print_number(term, vfs_used_nodes(context->fs));
    term_print(term, " OF ");
    term_print_number(term, VFS_MAX_NODES);
    term_newline(term);
    /* Blocks, because that is the limit a person actually meets. The node table
     * runs out at ninety six names. The pool runs out at whatever the files in
     * them add up to, and that is the one that stops a document being saved. */
    term_print(term, "BLOCKS  ");
    term_print_number(term, vfs_used_blocks(context->fs));
    term_print(term, " OF ");
    term_print_number(term, VFS_MAX_BLOCKS);
    term_print(term, ", ");
    term_print_number(term, VFS_BLOCK);
    term_println(term, " BYTES EACH");
    term_print(term, "PER FILE ");
    term_print_number(term, VFS_FILE_MAX);
    term_println(term, " BYTES AT MOST");

    /* The one thing about this filesystem that will surprise somebody, said
     * every time. Which of the two it is depends on the machine, and getting it
     * the wrong way round would either promise a disk that is not there or hide
     * one that is. */
    if (context->disk_model == NULL || context->disk_model[0] == '\0') {
        term_println(term, "NO DISK FOUND, SO THIS IS IN MEMORY ONLY");
        term_println(term, "AND NONE OF IT SURVIVES A RESTART.");
        return;
    }
    term_print(term, "DISK ");
    term_println(term, context->disk_model);
    term_print(term, "     ");
    term_print_number(term, context->disk_sectors);
    term_print(term, " SECTORS OF ");
    term_print_number(term, context->disk_sector_bytes);
    term_println(term, " BYTES");
    term_println(term, "SAVED AFTER EVERY CHANGE, SO IT SURVIVES");
    term_println(term, "A RESTART. THERE IS NOTHING TO TYPE.");
}
