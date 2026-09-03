#include "vfs.h"

const char *vfs_explain(enum vfs_result result)
{
    switch (result) {
    case VFS_OK:              return "OK";
    case VFS_NOT_FOUND:       return "NO SUCH FILE OR DIRECTORY";
    case VFS_EXISTS:          return "ALREADY EXISTS";
    case VFS_NOT_A_DIRECTORY: return "NOT A DIRECTORY";
    case VFS_IS_A_DIRECTORY:  return "IS A DIRECTORY";
    case VFS_NOT_EMPTY:       return "DIRECTORY IS NOT EMPTY";
    case VFS_NO_SPACE:        return "THE FILESYSTEM IS FULL";
    case VFS_TOO_BIG:         return "TOO BIG FOR ONE FILE";
    case VFS_BAD_NAME:        return "THAT IS NOT A USABLE NAME";
    }
    return "SOMETHING WENT WRONG";
}

static uint64_t length_of(const char *text)
{
    uint64_t n = 0;
    while (text != NULL && text[n] != '\0') {
        n++;
    }
    return n;
}

static bool names_match(const char *a, const char *b, uint64_t b_length)
{
    for (uint64_t i = 0; i < b_length; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return a[b_length] == '\0';
}

void vfs_init(struct vfs *fs)
{
    if (fs == NULL) {
        return;
    }
    for (int16_t i = 0; i < VFS_MAX_NODES; i++) {
        fs->nodes[i].used = false;
    }
    struct vfs_node *root = &fs->nodes[0];
    root->used = true;
    root->kind = VFS_DIR;
    root->name[0] = '\0';
    root->parent = 0;   /* the root is its own parent, so `..` cannot escape */
    root->first_child = VFS_NONE;
    root->next_sibling = VFS_NONE;
    root->length = 0;
    fs->cwd = 0;
    /* Nothing has changed, because nothing has happened yet. Reset here rather
     * than left alone so that reusing a filesystem does not start it looking
     * like it has unsaved work in it. */
    fs->changes = 0;
}

const struct vfs_node *vfs_get(const struct vfs *fs, int16_t node)
{
    if (fs == NULL || node < 0 || node >= VFS_MAX_NODES || !fs->nodes[node].used) {
        return NULL;
    }
    return &fs->nodes[node];
}

int16_t vfs_first_child(const struct vfs *fs, int16_t dir)
{
    const struct vfs_node *node = vfs_get(fs, dir);
    return node == NULL ? VFS_NONE : node->first_child;
}

int16_t vfs_next_sibling(const struct vfs *fs, int16_t node)
{
    const struct vfs_node *found = vfs_get(fs, node);
    return found == NULL ? VFS_NONE : found->next_sibling;
}

uint64_t vfs_count_children(const struct vfs *fs, int16_t dir)
{
    uint64_t count = 0;
    for (int16_t at = vfs_first_child(fs, dir); at != VFS_NONE;
         at = vfs_next_sibling(fs, at)) {
        count++;
    }
    return count;
}

uint64_t vfs_used_nodes(const struct vfs *fs)
{
    uint64_t used = 0;
    for (int16_t i = 0; fs != NULL && i < VFS_MAX_NODES; i++) {
        if (fs->nodes[i].used) {
            used++;
        }
    }
    return used;
}

/* The child of `dir` with this name, comparing only the first `length`
 * characters of `name` so a path can be walked without being cut up first. */
static int16_t child_named(const struct vfs *fs, int16_t dir,
                           const char *name, uint64_t length)
{
    for (int16_t at = vfs_first_child(fs, dir); at != VFS_NONE;
         at = vfs_next_sibling(fs, at)) {
        if (names_match(fs->nodes[at].name, name, length)) {
            return at;
        }
    }
    return VFS_NONE;
}

/* Walks a path, stopping before its last component when `parent_only` is set.
 * `last` and `last_length` then name the component that was not walked, which
 * is what every creating operation needs: the directory to create in, and the
 * name to create there. */
static int16_t walk(const struct vfs *fs, const char *path, bool parent_only,
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
        at = child_named(fs, at, path + start, length);
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
    return walk(fs, path, false, NULL, NULL);
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
        const uint64_t length = length_of(found->name);
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

static bool usable_name(const char *name, uint64_t length)
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

static int16_t free_node(struct vfs *fs)
{
    for (int16_t i = 1; i < VFS_MAX_NODES; i++) {
        if (!fs->nodes[i].used) {
            return i;
        }
    }
    return VFS_NONE;
}

/* Whether `maybe_parent` is `node` or anywhere above it. */
static bool is_within(const struct vfs *fs, int16_t node, int16_t maybe_parent)
{
    for (int16_t here = node; ; here = fs->nodes[here].parent) {
        if (here == maybe_parent) {
            return true;
        }
        if (here == 0) {
            return false;
        }
    }
}

/* Takes an entry out of its parent's list without freeing it. */
static void unlink_from_parent(struct vfs *fs, int16_t at)
{
    const int16_t parent = fs->nodes[at].parent;
    if (fs->nodes[parent].first_child == at) {
        fs->nodes[parent].first_child = fs->nodes[at].next_sibling;
        return;
    }
    int16_t before = fs->nodes[parent].first_child;
    while (before != VFS_NONE && fs->nodes[before].next_sibling != at) {
        before = fs->nodes[before].next_sibling;
    }
    if (before != VFS_NONE) {
        fs->nodes[before].next_sibling = fs->nodes[at].next_sibling;
    }
}

static void link_into(struct vfs *fs, int16_t parent, int16_t at)
{
    fs->nodes[at].parent = parent;
    fs->nodes[at].next_sibling = VFS_NONE;
    if (fs->nodes[parent].first_child == VFS_NONE) {
        fs->nodes[parent].first_child = at;
        return;
    }
    int16_t last = fs->nodes[parent].first_child;
    while (fs->nodes[last].next_sibling != VFS_NONE) {
        last = fs->nodes[last].next_sibling;
    }
    fs->nodes[last].next_sibling = at;
}

/* Makes one entry in a directory. The two creating commands differ only in the
 * kind they ask for, so they share everything else. */
static enum vfs_result make(struct vfs *fs, const char *path, enum vfs_kind kind,
                            int16_t *out)
{
    if (fs == NULL || path == NULL) {
        return VFS_BAD_NAME;
    }
    const char *name = NULL;
    uint64_t length = 0;
    const int16_t parent = walk(fs, path, true, &name, &length);
    if (parent == VFS_NONE) {
        return VFS_NOT_FOUND;
    }
    if (fs->nodes[parent].kind != VFS_DIR) {
        return VFS_NOT_A_DIRECTORY;
    }
    if (!usable_name(name, length)) {
        return VFS_BAD_NAME;
    }
    if (child_named(fs, parent, name, length) != VFS_NONE) {
        return VFS_EXISTS;
    }

    const int16_t made = free_node(fs);
    if (made == VFS_NONE) {
        return VFS_NO_SPACE;
    }
    struct vfs_node *node = &fs->nodes[made];
    node->used = true;
    node->kind = kind;
    for (uint64_t i = 0; i < length; i++) {
        node->name[i] = name[i];
    }
    node->name[length] = '\0';
    node->parent = parent;
    node->first_child = VFS_NONE;
    node->length = 0;

    /* Added at the end, so a listing comes out in the order things were made
     * rather than backwards. */
    node->next_sibling = VFS_NONE;
    if (fs->nodes[parent].first_child == VFS_NONE) {
        fs->nodes[parent].first_child = made;
    } else {
        int16_t at = fs->nodes[parent].first_child;
        while (fs->nodes[at].next_sibling != VFS_NONE) {
            at = fs->nodes[at].next_sibling;
        }
        fs->nodes[at].next_sibling = made;
    }
    if (out != NULL) {
        *out = made;
    }
    fs->changes++;
    return VFS_OK;
}

enum vfs_result vfs_mkdir(struct vfs *fs, const char *path)
{
    return make(fs, path, VFS_DIR, NULL);
}

enum vfs_result vfs_create(struct vfs *fs, const char *path)
{
    return make(fs, path, VFS_FILE, NULL);
}

/* Finds a file to write to, making it if it is not there, which is what every
 * shell does when it is told to write somewhere. */
static enum vfs_result file_for_writing(struct vfs *fs, const char *path,
                                        int16_t *out)
{
    int16_t at = vfs_resolve(fs, path);
    if (at == VFS_NONE) {
        const enum vfs_result made = make(fs, path, VFS_FILE, &at);
        if (made != VFS_OK) {
            return made;
        }
    }
    if (fs->nodes[at].kind != VFS_FILE) {
        return VFS_IS_A_DIRECTORY;
    }
    *out = at;
    return VFS_OK;
}

enum vfs_result vfs_write(struct vfs *fs, const char *path, const char *text)
{
    if (fs == NULL) {
        return VFS_BAD_NAME;
    }
    int16_t at = VFS_NONE;
    const enum vfs_result found = file_for_writing(fs, path, &at);
    if (found != VFS_OK) {
        return found;
    }
    fs->nodes[at].length = 0;
    return vfs_append(fs, path, text);
}

enum vfs_result vfs_append(struct vfs *fs, const char *path, const char *text)
{
    if (fs == NULL) {
        return VFS_BAD_NAME;
    }
    int16_t at = VFS_NONE;
    const enum vfs_result found = file_for_writing(fs, path, &at);
    if (found != VFS_OK) {
        return found;
    }

    struct vfs_node *node = &fs->nodes[at];
    const uint64_t adding = length_of(text);
    /* Refused rather than cut. A file that quietly holds less than it was given
     * is a file whose contents nobody can trust. */
    if (node->length + adding > VFS_FILE_MAX) {
        return VFS_TOO_BIG;
    }
    for (uint64_t i = 0; i < adding; i++) {
        node->data[node->length + i] = text[i];
    }
    node->length += (uint32_t)adding;
    fs->changes++;
    return VFS_OK;
}

enum vfs_result vfs_read(const struct vfs *fs, const char *path,
                         char *out, uint64_t capacity, uint64_t *length)
{
    if (length != NULL) {
        *length = 0;
    }
    if (out == NULL || capacity == 0) {
        return VFS_BAD_NAME;
    }
    out[0] = '\0';

    const int16_t at = vfs_resolve(fs, path);
    if (at == VFS_NONE) {
        return VFS_NOT_FOUND;
    }
    if (fs->nodes[at].kind != VFS_FILE) {
        return VFS_IS_A_DIRECTORY;
    }

    const struct vfs_node *node = &fs->nodes[at];
    uint64_t written = 0;
    while (written < node->length && written + 1 < capacity) {
        out[written] = node->data[written];
        written++;
    }
    out[written] = '\0';
    if (length != NULL) {
        *length = written;
    }
    return VFS_OK;
}

enum vfs_result vfs_remove(struct vfs *fs, const char *path)
{
    if (fs == NULL) {
        return VFS_BAD_NAME;
    }
    const int16_t at = vfs_resolve(fs, path);
    if (at == VFS_NONE) {
        return VFS_NOT_FOUND;
    }
    if (at == 0) {
        return VFS_BAD_NAME;   /* the root is not something to delete */
    }
    if (fs->nodes[at].kind == VFS_DIR && fs->nodes[at].first_child != VFS_NONE) {
        return VFS_NOT_EMPTY;
    }
    /* The working directory would become a node nothing points at, and every
     * relative path after that would resolve into freed space. */
    for (int16_t here = fs->cwd; ; here = fs->nodes[here].parent) {
        if (here == at) {
            return VFS_NOT_EMPTY;
        }
        if (here == 0) {
            break;
        }
    }

    unlink_from_parent(fs, at);
    fs->nodes[at].used = false;
    fs->changes++;
    return VFS_OK;
}

enum vfs_result vfs_move(struct vfs *fs, const char *from, const char *to)
{
    if (fs == NULL) {
        return VFS_BAD_NAME;
    }
    const int16_t at = vfs_resolve(fs, from);
    if (at == VFS_NONE) {
        return VFS_NOT_FOUND;
    }
    if (at == 0) {
        return VFS_BAD_NAME;
    }

    const char *name = NULL;
    uint64_t length = 0;
    const int16_t parent = walk(fs, to, true, &name, &length);
    if (parent == VFS_NONE) {
        return VFS_NOT_FOUND;
    }
    if (fs->nodes[parent].kind != VFS_DIR) {
        return VFS_NOT_A_DIRECTORY;
    }
    if (!usable_name(name, length)) {
        return VFS_BAD_NAME;
    }
    if (child_named(fs, parent, name, length) != VFS_NONE) {
        return VFS_EXISTS;
    }
    /* A directory moved inside itself takes its whole subtree out of the tree
     * and leaves it pointing at its own parent, which nothing can then reach. */
    if (is_within(fs, parent, at)) {
        return VFS_BAD_NAME;
    }

    unlink_from_parent(fs, at);
    for (uint64_t i = 0; i < length; i++) {
        fs->nodes[at].name[i] = name[i];
    }
    fs->nodes[at].name[length] = '\0';
    link_into(fs, parent, at);
    fs->changes++;
    return VFS_OK;
}

enum vfs_result vfs_copy(struct vfs *fs, const char *from, const char *to)
{
    if (fs == NULL) {
        return VFS_BAD_NAME;
    }
    const int16_t at = vfs_resolve(fs, from);
    if (at == VFS_NONE) {
        return VFS_NOT_FOUND;
    }
    /* Copying a directory means copying everything under it, which is a
     * different operation with a different way to run out of room part way. */
    if (fs->nodes[at].kind != VFS_FILE) {
        return VFS_IS_A_DIRECTORY;
    }
    if (vfs_resolve(fs, to) != VFS_NONE) {
        return VFS_EXISTS;
    }

    int16_t made = VFS_NONE;
    const enum vfs_result created = make(fs, to, VFS_FILE, &made);
    if (created != VFS_OK) {
        return created;
    }
    /* Read from the source after the destination exists, because making it may
     * have been what filled the filesystem up. */
    const struct vfs_node *source = &fs->nodes[at];
    for (uint32_t i = 0; i < source->length; i++) {
        fs->nodes[made].data[i] = source->data[i];
    }
    fs->nodes[made].length = source->length;
    return VFS_OK;
}

enum vfs_result vfs_chdir(struct vfs *fs, const char *path)
{
    if (fs == NULL) {
        return VFS_BAD_NAME;
    }
    const int16_t at = vfs_resolve(fs, path);
    if (at == VFS_NONE) {
        return VFS_NOT_FOUND;
    }
    if (fs->nodes[at].kind != VFS_DIR) {
        return VFS_NOT_A_DIRECTORY;
    }
    fs->cwd = at;
    return VFS_OK;
}
