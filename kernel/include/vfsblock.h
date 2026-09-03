/* The block pool a file's contents come out of.
 *
 * Kept apart from `vfs.h` because names and paths are one job and space is
 * another. Everything a file does with its contents goes through these, so
 * there is one place that knows a file is a list of blocks rather than a run of
 * bytes, and one place to check when a file comes back wrong.
 *
 * A node's blocks are always the first ones in its table, with VFS_NONE after
 * them. Everything here keeps that true, and `vfsblock_at` relies on it: block
 * `offset / VFS_BLOCK` is the one holding that byte only if there are no gaps.
 *
 * See M24 in docs/milestones.md.
 */
#ifndef ME_VFSBLOCK_H
#define ME_VFSBLOCK_H

#include "vfs.h"

/* One free block, cleared, or VFS_NONE when the pool is empty. */
int16_t vfsblock_take(struct vfs *fs);
void vfsblock_give(struct vfs *fs, int16_t block);

/* How many blocks this node is holding. */
uint64_t vfsblock_held(const struct vfs_node *node);

/* Makes the node hold `wanted` blocks, taking more if it needs them.
 *
 * All or nothing. A file that asked for three blocks and got two would have a
 * hole in the middle of it, so when the pool cannot cover the whole request
 * every block taken for it goes back and the node is left exactly as it was.
 */
bool vfsblock_reserve(struct vfs *fs, struct vfs_node *node, uint64_t wanted);

/* Gives every block back and leaves the file empty. */
void vfsblock_release(struct vfs *fs, struct vfs_node *node);

/* Works out which blocks are spoken for from what the files claim, and says
 * whether the claims make sense.
 *
 * This is how a filesystem read off a disk gets its bitmap. The bitmap is not
 * on the disk on purpose: it would be a second answer to a question the files
 * already answer, and when two answers disagree the machine hands the same
 * block to two files and each of them writes over the other.
 *
 * False when a block number is out of range, when two files claim the same
 * block, when a file has a gap in its block table, or when something that is
 * not a file in use claims a block at all.
 */
bool vfsblock_rebuild(struct vfs *fs);

/* Where byte `offset` of this file lives, or NULL when that is past the blocks
 * the node holds. The reading one exists separately rather than casting the
 * const away, because casting it away in the one function every read goes
 * through is how a read ends up writing. */
char *vfsblock_at(struct vfs *fs, struct vfs_node *node, uint64_t offset);
const char *vfsblock_read_at(const struct vfs *fs, const struct vfs_node *node,
                             uint64_t offset);

#endif /* ME_VFSBLOCK_H */
