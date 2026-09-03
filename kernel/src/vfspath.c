/* Turning a path into a node.
 *
 * Everything here reads and nothing changes anything, which is why it is worth
 * having on its own. The rules about `.`, `..` and where a path starts from are
 * the ones most likely to be wrong in a way that only shows up somewhere else,
 * and this is the whole of them.
 *
 * See M20 in docs/milestones.md.
 */
#include "vfsnode.h"

uint64_t vfs_length_of(const char *text)
{
    uint64_t n = 0;
    while (text != NULL && text[n] != '\0') {
        n++;
    }
    return n;
}

bool vfs_names_match(const char *a, const char *b, uint64_t b_length)
{
    for (uint64_t i = 0; i < b_length; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return a[b_length] == '\0';
}

/* The child of `dir` with this name, comparing only the first `length`
 * characters of `name` so a path can be walked without being cut up first. */
int16_t vfs_child_named(const struct vfs *fs, int16_t dir,
                           const char *name, uint64_t length)
{
    for (int16_t at = vfs_first_child(fs, dir); at != VFS_NONE;
         at = vfs_next_sibling(fs, at)) {
        if (vfs_names_match(fs->nodes[at].name, name, length)) {
            return at;
        }
    }
    return VFS_NONE;
}

/* Walks a path, stopping before its last component when `parent_only` is set.
 * `last` and `last_length` then name the component that was not walked, which
 * is what every creating operation needs: the directory to create in, and the
 * name to create there. */
int16_t vfs_walk(const struct vfs *fs, const char *path, bool parent_only,
                    const char **last, uint64_t *last_length)
{
    if (fs == NULL || path == NULL) {
        return VFS_NONE;
    }
    int16_t at = path[0] == '/' ? 0 : fs->cwd;
    uint64_t i = path[0] == '/' ? 1 : 0;

    if (last != NULL) {
        *last = "";
        *last_length = 0;
    }

    while (path[i] != '\0') {
        while (path[i] == '/') {
            i++;
        }
        if (path[i] == '\0') {
            break;
        }
        uint64_t start = i;
        while (path[i] != '\0' && path[i] != '/') {
            i++;
        }
        const uint64_t length = i - start;

        /* Whether this was the last component. Trailing slashes do not make a
         * new component, so `A/B/` ends on B just as `A/B` does. */
        uint64_t peek = i;
        while (path[peek] == '/') {
            peek++;
        }
        const bool is_last = path[peek] == '\0';

        if (is_last && parent_only) {
            if (last != NULL) {
                *last = path + start;
                *last_length = length;
            }
            return at;
        }

        if (length == 1 && path[start] == '.') {
            continue;
        }
        if (length == 2 && path[start] == '.' && path[start + 1] == '.') {
            at = fs->nodes[at].parent;
            continue;
        }
        /* A component in the middle of a path has to be a directory, or the
         * path names something inside a file, which is not a thing. */
        if (fs->nodes[at].kind != VFS_DIR) {
            return VFS_NONE;
        }
        at = vfs_child_named(fs, at, path + start, length);
        if (at == VFS_NONE) {
            return VFS_NONE;
        }
    }
    return at;
}

int16_t vfs_resolve(const struct vfs *fs, const char *path)
{
    if (fs == NULL || path == NULL) {
        return VFS_NONE;
    }
    return vfs_walk(fs, path, false, NULL, NULL);
}

uint64_t vfs_path_of(const struct vfs *fs, int16_t node, char *out, uint64_t capacity)
{
    if (out == NULL || capacity == 0) {
        return 0;
    }
    out[0] = '\0';
    if (vfs_get(fs, node) == NULL) {
        return 0;
    }
    if (node == 0) {
        if (capacity > 1) {
            out[0] = '/';
            out[1] = '\0';
            return 1;
        }
        return 0;
    }

    /* Walked from the node up to the root, so the names come out backwards and
     * are written into the end of the buffer, then moved to the front. Building
     * it forwards would need the tree to hold child to parent links twice. */
    uint64_t at = capacity - 1;
    out[at] = '\0';
    int16_t here = node;
    while (here != 0) {
        const struct vfs_node *found = &fs->nodes[here];
        const uint64_t length = vfs_length_of(found->name);
        if (at < length + 1) {
            return 0;
        }
        at -= length;
        for (uint64_t i = 0; i < length; i++) {
            out[at + i] = found->name[i];
        }
        out[--at] = '/';
        here = found->parent;
    }

    uint64_t written = 0;
    while (out[at] != '\0') {
        out[written++] = out[at++];
    }
    out[written] = '\0';
    return written;
}

bool vfs_usable_name(const char *name, uint64_t length)
{
    if (length == 0 || length >= VFS_NAME_MAX) {
        return false;
    }
    if (length == 1 && name[0] == '.') {
        return false;
    }
    if (length == 2 && name[0] == '.' && name[1] == '.') {
        return false;
    }
    for (uint64_t i = 0; i < length; i++) {
        if (name[i] == '/') {
            return false;
        }
    }
    return true;
}
