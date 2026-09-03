/* The filesystem written down, so it is still there after the power goes off.
 *
 * M20 built a real filesystem with nothing under it, and DF has been saying so
 * ever since. This is what it was waiting for.
 *
 * The format is written out byte by byte rather than by copying the structures
 * straight to the disk. A C structure has padding the compiler chooses, and a
 * disk written by one build would be read wrong by the next. Every number here
 * is little endian and every field is at a fixed offset that this file names.
 *
 * The header records the limits the filesystem was built with. A disk written
 * by a build with a different node count or file size is refused rather than
 * read as though the fields were where this build expects them, because that
 * reads one file's contents as another file's name.
 *
 * See M23 in docs/milestones.md.
 */
#ifndef ME_VFSDISK_H
#define ME_VFSDISK_H

#include "disk.h"
#include "vfs.h"

/* Eight bytes, so it fills the front of the header exactly. */
#define VFSDISK_MAGIC   "ME-OS FS"
#define VFSDISK_VERSION 2u

/* One sector per node, and one per block, laid out in that order.
 *
 *   sector 0                      the header
 *   sectors 1 to VFS_MAX_NODES    the nodes, node `i` at sector `1 + i`
 *   the rest                      the blocks, block `i` at BLOCKS_AT + i
 *
 * A node needed two sectors in version 1, because it carried its contents. It
 * carries twelve block numbers now and fits in well under one, and a block is
 * exactly a sector, so the arithmetic is a sum anybody can check by hand
 * against a hex dump when something has gone wrong. */
#define VFSDISK_NODE_SECTORS 1u
#define VFSDISK_BLOCKS_AT    (1u + VFS_MAX_NODES * VFSDISK_NODE_SECTORS)
#define VFSDISK_SECTORS      (VFSDISK_BLOCKS_AT + VFS_MAX_BLOCKS)

enum vfsdisk_result {
    VFSDISK_OK,
    VFSDISK_NO_DISK,
    VFSDISK_IO_FAILED,
    /* No magic. A blank disk or somebody else's, and neither is an error worth
     * alarming anybody about: it means there is nothing to load. */
    VFSDISK_NOT_FORMATTED,
    VFSDISK_WRONG_VERSION,
    /* Written by a build with different limits. */
    VFSDISK_WRONG_SHAPE,
    /* The magic is right and the contents are impossible. */
    VFSDISK_CORRUPT,
};

const char *vfsdisk_explain(enum vfsdisk_result result);

/* Writes the whole filesystem, nodes and blocks. Everything, every time, rather
 * than only what changed: tracking that is a milestone of its own and getting
 * it wrong loses a file quietly, which is the one failure a filesystem must not
 * have. */
enum vfsdisk_result vfsdisk_save(const struct vfs *fs, const struct disk *disk);

/* Reads it back.
 *
 * Every index on the disk is checked before it is believed, because every walk
 * in `vfs.c` trusts them. A parent that points at itself makes `vfs_path_of`
 * loop forever, and a corrupt disk is exactly where such a number comes from.
 *
 * Nothing is touched until the header has been read and accepted, so a disk
 * that will not answer at all leaves the filesystem it was given exactly as it
 * was. Emptying a working filesystem because a disk was missing would lose more
 * than the failure did. Once reading has started, any failure leaves it empty
 * rather than half read, because half a filesystem looks like a whole one.
 */
enum vfsdisk_result vfsdisk_load(struct vfs *fs, const struct disk *disk);

/* Whether this filesystem is one the rest of the kernel can safely walk.
 *
 * Separate from loading, and public, because it is the interesting half. It is
 * what decides whether a disk somebody has been carrying around is allowed to
 * become the filesystem, and it can be pointed at a filesystem built in memory
 * by a test without a disk anywhere near it.
 */
bool vfsdisk_sound(const struct vfs *fs);

#endif /* ME_VFSDISK_H */
