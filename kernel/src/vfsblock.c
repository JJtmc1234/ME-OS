/* Where the bytes in a file actually live.
 *
 * A pool of fixed blocks and a bitmap saying which are spoken for. Files hold
 * a list of block numbers, so a file grows by taking another block rather than
 * by being copied somewhere with more room next to it.
 *
 * Fixed blocks, not a heap. Blocks are all the same size, so nothing here can
 * fragment: a free block always fits whatever wants one. That removes the whole
 * class of bug where a filesystem with plenty of room refuses a file because
 * none of the free space is in one piece.
 *
 * A bitmap, not a free list. A free list is a chain, and a chain on a disk is
 * one wrong number away from a loop that never ends or a block handed out
 * twice. A bitmap can be checked against what the files actually claim, which
 * is what `vfsdisk_check.c` does.
 *
 * Split from `vfs.c` because that file is about names and paths and this one is
 * about space. See M24 in docs/milestones.md.
 */
#include "vfsblock.h"

uint64_t vfs_blocks_for(uint64_t length)
{
    /* Rounded up, because a file of one byte still needs somewhere to put it.
     * A file of no bytes needs nowhere, which is why this is not simply a
     * division that always adds one. */
    return (length + VFS_BLOCK - 1) / VFS_BLOCK;
}

uint64_t vfs_used_blocks(const struct vfs *fs)
{
    uint64_t used = 0;
    for (uint64_t i = 0; fs != NULL && i < VFS_MAX_BLOCKS; i++) {
        if (fs->block_used[i]) {
            used++;
        }
    }
    return used;
}

int16_t vfsblock_take(struct vfs *fs)
{
    for (int16_t i = 0; i < VFS_MAX_BLOCKS; i++) {
        if (fs->block_used[i]) {
            continue;
        }
        fs->block_used[i] = true;
        /* Cleared on the way out rather than on the way in. A block handed out
         * still holding the last file's bytes would show them through the tail
         * of a file that was never written that far, and the length is the only
         * thing standing between that and being read. */
        for (uint64_t at = 0; at < VFS_BLOCK; at++) {
            fs->blocks[i][at] = '\0';
        }
        return i;
    }
    return VFS_NONE;
}

void vfsblock_give(struct vfs *fs, int16_t block)
{
    if (fs != NULL && block >= 0 && block < VFS_MAX_BLOCKS) {
        fs->block_used[block] = false;
    }
}

void vfsblock_release(struct vfs *fs, struct vfs_node *node)
{
    for (uint64_t i = 0; i < VFS_DIRECT_BLOCKS; i++) {
        vfsblock_give(fs, node->blocks[i]);
        node->blocks[i] = VFS_NONE;
    }
    node->length = 0;
}

bool vfsblock_reserve(struct vfs *fs, struct vfs_node *node, uint64_t wanted)
{
    if (wanted > VFS_DIRECT_BLOCKS) {
        return false;
    }
    const uint64_t held = vfsblock_held(node);
    if (held >= wanted) {
        return true;
    }

    /* Taken into a list first, and written into the node only once every one of
     * them came back. Handing them out straight into the node would leave a
     * file holding half of what it asked for when the pool runs out partway,
     * and a file that asked for three blocks and got two has a hole in the
     * middle of it. */
    int16_t taken[VFS_DIRECT_BLOCKS];
    uint64_t got = 0;
    while (got < wanted - held) {
        const int16_t block = vfsblock_take(fs);
        if (block == VFS_NONE) {
            for (uint64_t back = 0; back < got; back++) {
                vfsblock_give(fs, taken[back]);
            }
            return false;
        }
        taken[got++] = block;
    }

    /* Into the first free slots, so the blocks a node holds stay the first ones
     * in its table with nothing between them. `vfsblock_at` depends on it. */
    uint64_t next = 0;
    for (uint64_t i = 0; i < VFS_DIRECT_BLOCKS && next < got; i++) {
        if (node->blocks[i] == VFS_NONE) {
            node->blocks[i] = taken[next++];
        }
    }
    return true;
}

uint64_t vfsblock_held(const struct vfs_node *node)
{
    uint64_t held = 0;
    for (uint64_t i = 0; i < VFS_DIRECT_BLOCKS; i++) {
        if (node->blocks[i] != VFS_NONE) {
            held++;
        }
    }
    return held;
}

char *vfsblock_at(struct vfs *fs, struct vfs_node *node, uint64_t offset)
{
    const uint64_t which = offset / VFS_BLOCK;
    if (which >= VFS_DIRECT_BLOCKS) {
        return NULL;
    }
    const int16_t block = node->blocks[which];
    if (block < 0 || block >= VFS_MAX_BLOCKS) {
        return NULL;
    }
    return &fs->blocks[block][offset % VFS_BLOCK];
}

const char *vfsblock_read_at(const struct vfs *fs, const struct vfs_node *node,
                             uint64_t offset)
{
    /* The same walk, on a filesystem nobody is allowed to change. Written twice
     * rather than cast away, because casting const away in the one function
     * every read goes through is how a read ends up writing. */
    const uint64_t which = offset / VFS_BLOCK;
    if (which >= VFS_DIRECT_BLOCKS) {
        return NULL;
    }
    const int16_t block = node->blocks[which];
    if (block < 0 || block >= VFS_MAX_BLOCKS) {
        return NULL;
    }
    return &fs->blocks[block][offset % VFS_BLOCK];
}

bool vfsblock_rebuild(struct vfs *fs)
{
    if (fs == NULL) {
        return false;
    }
    for (uint64_t i = 0; i < VFS_MAX_BLOCKS; i++) {
        fs->block_used[i] = false;
    }

    for (int16_t at = 0; at < VFS_MAX_NODES; at++) {
        const struct vfs_node *node = &fs->nodes[at];
        const bool may_hold = node->used && node->kind == VFS_FILE;
        bool ended = false;

        for (uint64_t i = 0; i < VFS_DIRECT_BLOCKS; i++) {
            const int16_t block = node->blocks[i];
            if (block == VFS_NONE) {
                /* Everything after the first empty slot has to be empty too.
                 * A gap would make `vfsblock_at` read the wrong block for every
                 * byte past it, silently, because the arithmetic still lands
                 * somewhere real. */
                ended = true;
                continue;
            }
            if (ended || !may_hold || block < 0 || block >= VFS_MAX_BLOCKS) {
                return false;
            }
            /* Two owners for one block is the worst of these. Each file would
             * write over the other and neither would look damaged until it was
             * read. */
            if (fs->block_used[block]) {
                return false;
            }
            fs->block_used[block] = true;
        }
    }
    return true;
}
