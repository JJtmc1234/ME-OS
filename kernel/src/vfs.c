/* The node table, and the questions that only read it.
 *
 * The filesystem is spread over five files, each answering one thing. This one
 * owns the table itself: setting it up, and looking at a node without changing
 * anything. Paths are in `vfspath.c`, making and removing entries in
 * `vfstree.c`, contents in `vfsfile.c`, and moving things about in `vfsmove.c`.
 *
 * They were one file until it passed six hundred lines, at which point nobody
 * could read the whole of it in a sitting and the interesting parts were buried
 * among the obvious ones.
 *
 * See M20 and M24 in docs/milestones.md.
 */
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

void vfs_init(struct vfs *fs)
{
    if (fs == NULL) {
        return;
    }
    for (int16_t i = 0; i < VFS_MAX_NODES; i++) {
        fs->nodes[i].used = false;
        /* Emptied rather than left alone. A free node still naming blocks would
         * have the check in `vfsdisk_check.c` see two owners for one block the
         * moment somebody else took it. */
        for (uint64_t at = 0; at < VFS_DIRECT_BLOCKS; at++) {
            fs->nodes[i].blocks[at] = VFS_NONE;
        }
    }
    for (uint64_t i = 0; i < VFS_MAX_BLOCKS; i++) {
        fs->block_used[i] = false;
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

