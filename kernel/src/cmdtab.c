/* Finishing a name you have started typing.
 *
 * The shell has directories, paths and files with names like README.TXT, and
 * until now the only way to reach one was to type all of it correctly. On a
 * machine whose keyboard is a virtual one in an emulator that is not a small
 * thing.
 *
 * The rules here are the ones every shell settled on, and each is a decision
 * rather than an accident:
 *
 *   one match      finish it, and add a slash if it is a directory, because
 *                  the next thing you type is almost certainly inside it
 *   several        finish as much as they all agree on, then show them, so a
 *                  second press is not needed to find out what they were
 *   none           leave the line exactly as it was and say nothing
 *
 * The last one matters most. A completion that changed the line when it had
 * nothing to offer would be worse than one that did nothing, because the line
 * would then be wrong in a way that looks like something you typed.
 *
 * See M28 in docs/milestones.md.
 */
#include "cmd.h"

#include "vfs.h"

static uint64_t length_of(const char *text)
{
    uint64_t n = 0;
    while (text != NULL && text[n] != '\0') {
        n++;
    }
    return n;
}

static char upper(char ch)
{
    return ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
}

/* Where the word under completion starts. Everything after the last blank, so
 * `CAT PROJ` completes PROJ and leaves `CAT ` alone. */
static uint64_t word_start(const char *line)
{
    uint64_t at = 0;
    uint64_t start = 0;
    while (line[at] != '\0') {
        if (line[at] == ' ') {
            start = at + 1;
        }
        at++;
    }
    return start;
}

/* Splits the word into the directory to look in and the part to match.
 *
 * `PROJECTS/ME` looks in PROJECTS for names starting with ME. A word with no
 * slash looks in the working directory, which is what `.` resolves to.
 */
static uint64_t match_start(const char *word)
{
    uint64_t at = 0;
    uint64_t start = 0;
    while (word[at] != '\0') {
        if (word[at] == '/') {
            start = at + 1;
        }
        at++;
    }
    return start;
}

/* Where to look and what to match, worked out once.
 *
 * Both halves of completion need the same three answers, and two copies of this
 * would be two chances for finishing a name and listing the candidates to
 * disagree about which directory they meant.
 */
struct completion_at {
    int16_t dir;
    const char *partial;
    uint64_t partial_length;
    /* How much of the line to keep exactly as typed: everything up to and
     * including the last slash of the word. */
    uint64_t keep;
};

static bool where_to_look(struct cmd_context *context, const char *line,
                          struct completion_at *at)
{
    const uint64_t word_at = word_start(line);
    const char *word = line + word_at;
    const uint64_t match_at = match_start(word);

    char where[VFS_PATH_MAX];
    if (match_at == 0) {
        where[0] = '.';
        where[1] = '\0';
    } else {
        /* The slash is kept when it is the only character, because `/` is the
         * root and an empty string is nowhere. */
        const uint64_t take = match_at == 1 ? 1 : match_at - 1;
        uint64_t i = 0;
        for (; i < take && i + 1 < sizeof where; i++) {
            where[i] = word[i];
        }
        where[i] = '\0';
    }

    const int16_t dir = vfs_resolve(context->fs, where);
    if (dir == VFS_NONE || vfs_get(context->fs, dir)->kind != VFS_DIR) {
        return false;
    }
    at->dir = dir;
    at->partial = word + match_at;
    at->partial_length = length_of(at->partial);
    at->keep = word_at + match_at;
    return true;
}

/* Whether this entry is one the partial name could grow into. */
static bool begins_with(const struct vfs_node *node, const struct completion_at *at)
{
    for (uint64_t i = 0; i < at->partial_length; i++) {
        if (upper(node->name[i]) != upper(at->partial[i])) {
            return false;
        }
    }
    return true;
}

uint64_t cmd_complete(struct cmd_context *context, const char *line,
                      char *out, uint64_t capacity)
{
    if (context == NULL || context->fs == NULL || line == NULL ||
        out == NULL || capacity == 0) {
        return 0;
    }
    out[0] = '\0';

    struct completion_at at;
    if (!where_to_look(context, line, &at)) {
        return 0;
    }

    /* The longest beginning every match agrees on, built up as they are found.
     * With one match that is the whole name, which is why finishing one name
     * and finishing as far as several agree are the same piece of code. */
    char shared[VFS_NAME_MAX];
    uint64_t shared_length = 0;
    uint64_t matches = 0;
    bool only_is_dir = false;

    for (int16_t child = vfs_first_child(context->fs, at.dir); child != VFS_NONE;
         child = vfs_next_sibling(context->fs, child)) {
        const struct vfs_node *node = vfs_get(context->fs, child);
        if (!begins_with(node, &at)) {
            continue;
        }

        matches++;
        if (matches == 1) {
            while (node->name[shared_length] != '\0' &&
                   shared_length + 1 < sizeof shared) {
                shared[shared_length] = node->name[shared_length];
                shared_length++;
            }
            shared[shared_length] = '\0';
            only_is_dir = node->kind == VFS_DIR;
            continue;
        }

        uint64_t agree = 0;
        while (agree < shared_length && node->name[agree] != '\0' &&
               upper(shared[agree]) == upper(node->name[agree])) {
            agree++;
        }
        shared_length = agree;
        shared[shared_length] = '\0';
        only_is_dir = false;
    }

    if (matches == 0) {
        return 0;
    }

    uint64_t written = 0;
    for (uint64_t i = 0; i < at.keep && written + 1 < capacity; i++) {
        out[written++] = line[i];
    }
    for (uint64_t i = 0; i < shared_length && written + 1 < capacity; i++) {
        out[written++] = shared[i];
    }
    /* A slash after a directory, because whatever comes next is almost
     * certainly inside it. Only when it is the one match: with several, the
     * shared beginning is not a whole name and a slash would be a lie. */
    if (matches == 1 && only_is_dir && written + 1 < capacity) {
        out[written++] = '/';
    }
    out[written] = '\0';
    return matches;
}

void cmdtab_show(struct cmd_context *context, const char *line)
{
    struct completion_at at;
    if (context == NULL || context->fs == NULL || line == NULL ||
        !where_to_look(context, line, &at)) {
        return;
    }
    for (int16_t child = vfs_first_child(context->fs, at.dir); child != VFS_NONE;
         child = vfs_next_sibling(context->fs, child)) {
        const struct vfs_node *node = vfs_get(context->fs, child);
        if (!begins_with(node, &at)) {
            continue;
        }
        cmd_print(context->out, node->name);
        if (node->kind == VFS_DIR) {
            cmd_print(context->out, "/");
        }
        cmd_newline(context->out);
    }
}
