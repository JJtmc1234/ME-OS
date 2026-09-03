/* Whether a filesystem is one the rest of the kernel can safely walk.
 *
 * Split from `vfsdisk.c` because it answers a different question. That file is
 * about where the bytes go. This one is about which arrangements of them are
 * possible, and it is the half that stops a scratched disk from taking the
 * machine down.
 *
 * Every walk in `vfs.c` follows `parent`, `first_child` and `next_sibling`
 * without checking them, which is right, because nothing inside the kernel can
 * make them wrong. A disk can. A parent that points at its own child makes
 * `vfs_path_of` loop forever, and a `first_child` of 5000 reads past the end of
 * the node table. Neither is a thing that shows up as a wrong answer. Both
 * hang or corrupt the machine.
 *
 * So the rule is that nothing from a disk is believed until the whole shape has
 * been checked, and the check is written to be read: one property per block,
 * each one saying what goes wrong without it.
 */
#include "vfsdisk.h"

#include "vfsblock.h"

static bool in_range(int16_t at)
{
    return at >= 0 && at < VFS_MAX_NODES;
}

/* A link is either nothing, or a node that is in range and in use. A used node
 * pointing at a free one is how a listing walks into uninitialised memory. */
static bool link_ok(const struct vfs *fs, int16_t at)
{
    return at == VFS_NONE || (in_range(at) && fs->nodes[at].used);
}

static bool name_ok(const struct vfs_node *node)
{
    for (uint64_t i = 0; i < VFS_NAME_MAX; i++) {
        if (node->name[i] == '\0') {
            /* Only the root is allowed no name. Anything else with an empty
             * one can never be named on a command line, so it could not be
             * opened, moved or deleted. */
            return i > 0;
        }
        /* The characters `vfs.c` treats specially. A name holding one of them
         * cannot be resolved back to the node it belongs to. */
        if (node->name[i] == '/' || node->name[i] < ' ') {
            return false;
        }
    }
    return false;   /* ran off the end, so it is not terminated */
}

static bool fields_ok(const struct vfs *fs, int16_t at)
{
    const struct vfs_node *node = &fs->nodes[at];
    if (!link_ok(fs, node->first_child) || !link_ok(fs, node->next_sibling)) {
        return false;
    }
    /* The parent is not optional. Every used node has one, and the root's is
     * itself, which is what stops `..` climbing out of the filesystem. */
    if (!in_range(node->parent) || !fs->nodes[node->parent].used ||
        fs->nodes[node->parent].kind != VFS_DIR) {
        return false;
    }
    if (node->length > VFS_FILE_MAX) {
        return false;
    }
    if (node->kind == VFS_DIR && node->length != 0) {
        return false;
    }
    /* Exactly the blocks the length calls for. Too few and the end of the file
     * has nowhere to be, and reading it walks off the block table. Too many and
     * the pool leaks room to a file that is not using it, which nothing else
     * would ever notice. */
    if (vfsblock_held(node) != vfs_blocks_for(node->length)) {
        return false;
    }
    /* And every one of them accounted for. `vfsblock_rebuild` marks what the
     * files claim, so a block a file holds that is not marked is a block this
     * node was not counted for, which means the two disagree. */
    for (uint64_t i = 0; i < VFS_DIRECT_BLOCKS; i++) {
        const int16_t block = node->blocks[i];
        if (block == VFS_NONE) {
            continue;
        }
        if (block < 0 || block >= VFS_MAX_BLOCKS || !fs->block_used[block]) {
            return false;
        }
    }
    /* A file with children would be listed as a directory by anything walking
     * the tree and refused as a directory by everything else. */
    if (node->kind == VFS_FILE && node->first_child != VFS_NONE) {
        return false;
    }
    return at == 0 ? node->name[0] == '\0' : name_ok(node);
}

/* Walking up from every node has to reach the root. Bounded by the size of the
 * table, because a cycle is exactly what this is looking for and an unbounded
 * walk through one never comes back. */
static bool climbs_to_the_root(const struct vfs *fs, int16_t at)
{
    for (uint64_t steps = 0; steps <= VFS_MAX_NODES; steps++) {
        if (at == 0) {
            return true;
        }
        at = fs->nodes[at].parent;
    }
    return false;
}

/* Every used node except the root is listed exactly once by its own parent.
 *
 * Once, because twice means one node in two places and deleting it from one
 * leaves the other pointing at a free slot. By its own parent, because a node
 * whose parent does not list it cannot be found by LS and cannot be deleted,
 * and it holds a slot forever.
 */
static bool children_agree_with_parents(const struct vfs *fs)
{
    bool listed[VFS_MAX_NODES] = { false };

    for (int16_t dir = 0; dir < VFS_MAX_NODES; dir++) {
        if (!fs->nodes[dir].used || fs->nodes[dir].kind != VFS_DIR) {
            continue;
        }
        int16_t child = fs->nodes[dir].first_child;
        for (uint64_t steps = 0; child != VFS_NONE; steps++) {
            if (steps > VFS_MAX_NODES) {
                return false;   /* the sibling list loops */
            }
            if (fs->nodes[child].parent != dir || listed[child]) {
                return false;
            }
            listed[child] = true;
            child = fs->nodes[child].next_sibling;
        }
    }

    for (int16_t at = 1; at < VFS_MAX_NODES; at++) {
        if (fs->nodes[at].used && !listed[at]) {
            return false;
        }
    }
    return listed[0] == false;   /* nothing lists the root, not even the root */
}

bool vfsdisk_sound(const struct vfs *fs)
{
    if (fs == NULL) {
        return false;
    }

    const struct vfs_node *root = &fs->nodes[0];
    if (!root->used || root->kind != VFS_DIR || root->parent != 0 ||
        root->next_sibling != VFS_NONE) {
        return false;
    }

    for (int16_t at = 0; at < VFS_MAX_NODES; at++) {
        if (!fs->nodes[at].used) {
            continue;
        }
        if (!fields_ok(fs, at) || !climbs_to_the_root(fs, at)) {
            return false;
        }
    }

    if (!children_agree_with_parents(fs)) {
        return false;
    }
    /* Where the last session was standing is not on the disk, so it is the root
     * or it is wrong. Checked rather than assumed, because a working directory
     * pointing at a free node makes every relative path resolve into it. */
    return in_range(fs->cwd) && fs->nodes[fs->cwd].used &&
           fs->nodes[fs->cwd].kind == VFS_DIR;
}
