/* Making entries, and where in the tree they go.
 *
 * One function does the making, and MKDIR and TOUCH are the two kinds of thing
 * it makes. Joining a node into its parent's list of children lives here too,
 * because it is the other half of the same act and `vfsmove.c` needs it for the
 * same reason.
 *
 * See M20 and M24 in docs/milestones.md.
 */
#include "vfsnode.h"

static int16_t free_node(struct vfs *fs)
{
    for (int16_t i = 1; i < VFS_MAX_NODES; i++) {
        if (!fs->nodes[i].used) {
            return i;
        }
    }
    return VFS_NONE;
}


/* Takes an entry out of its parent's list without freeing it. */
void vfs_unlink_from_parent(struct vfs *fs, int16_t at)
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

void vfs_link_into(struct vfs *fs, int16_t parent, int16_t at)
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
enum vfs_result vfs_make(struct vfs *fs, const char *path, enum vfs_kind kind,
                            int16_t *out)
{
    if (fs == NULL || path == NULL) {
        return VFS_BAD_NAME;
    }
    const char *name = NULL;
    uint64_t length = 0;
    const int16_t parent = vfs_walk(fs, path, true, &name, &length);
    if (parent == VFS_NONE) {
        return VFS_NOT_FOUND;
    }
    if (fs->nodes[parent].kind != VFS_DIR) {
        return VFS_NOT_A_DIRECTORY;
    }
    if (!vfs_usable_name(name, length)) {
        return VFS_BAD_NAME;
    }
    if (vfs_child_named(fs, parent, name, length) != VFS_NONE) {
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
    for (uint64_t i = 0; i < VFS_DIRECT_BLOCKS; i++) {
        node->blocks[i] = VFS_NONE;
    }

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
    return vfs_make(fs, path, VFS_DIR, NULL);
}

enum vfs_result vfs_create(struct vfs *fs, const char *path)
{
    return vfs_make(fs, path, VFS_FILE, NULL);
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
