/* Taking entries out of the tree and putting them somewhere else.
 *
 * Removing, moving and copying. All three change what the tree looks like
 * rather than what is inside a file, and all three have a way to go wrong that
 * leaves the tree pointing at something that is no longer there.
 *
 * See M20 and M24 in docs/milestones.md.
 */
#include "vfsnode.h"

#include "vfsblock.h"

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

    vfs_unlink_from_parent(fs, at);
    /* The room goes back with the name. A node marked free while still naming
     * blocks is how a filesystem fills up with files nobody can see. */
    vfsblock_release(fs, &fs->nodes[at]);
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
    const int16_t parent = vfs_walk(fs, to, true, &name, &length);
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
    /* A directory moved inside itself takes its whole subtree out of the tree
     * and leaves it pointing at its own parent, which nothing can then reach. */
    if (is_within(fs, parent, at)) {
        return VFS_BAD_NAME;
    }

    vfs_unlink_from_parent(fs, at);
    for (uint64_t i = 0; i < length; i++) {
        fs->nodes[at].name[i] = name[i];
    }
    fs->nodes[at].name[length] = '\0';
    vfs_link_into(fs, parent, at);
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
    const enum vfs_result created = vfs_make(fs, to, VFS_FILE, &made);
    if (created != VFS_OK) {
        return created;
    }
    /* Room for the copy after the destination exists, because making it may
     * have been what filled the node table up, and taking blocks for a file
     * that then cannot be named would lose them until the next restart. */
    const uint32_t length = fs->nodes[at].length;
    if (!vfsblock_reserve(fs, &fs->nodes[made], vfs_blocks_for(length))) {
        /* The name goes back with the room. Half a copy under the new name is
         * worse than no copy, because only one of the two looks like a failure. */
        (void)vfs_remove(fs, to);
        return VFS_NO_SPACE;
    }
    for (uint32_t i = 0; i < length; i++) {
        const char *from_at = vfsblock_read_at(fs, &fs->nodes[at], i);
        char *to_at = vfsblock_at(fs, &fs->nodes[made], i);
        if (from_at == NULL || to_at == NULL) {
            (void)vfs_remove(fs, to);
            return VFS_NO_SPACE;
        }
        *to_at = *from_at;
    }
    fs->nodes[made].length = length;
    return VFS_OK;
}
