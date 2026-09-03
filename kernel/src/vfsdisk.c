/* Saving the filesystem and reading it back. The shape of the bytes is in
 * `vfsdisk_format.c`, so this file is only about the order things happen in and
 * what to do when the disk will not answer.
 *
 * See M23 in docs/milestones.md.
 */
#include "vfsdisk_format.h"

#include "vfsblock.h"

#define NODE_BYTES VFSDISK_NODE_BYTES

const char *vfsdisk_explain(enum vfsdisk_result result)
{
    switch (result) {
    case VFSDISK_OK:            return "OK";
    case VFSDISK_NO_DISK:       return "THERE IS NO DISK";
    case VFSDISK_IO_FAILED:     return "THE DISK WOULD NOT ANSWER";
    case VFSDISK_NOT_FORMATTED: return "NOTHING OF OURS IS ON THAT DISK";
    case VFSDISK_WRONG_VERSION: return "WRITTEN BY A DIFFERENT VERSION";
    case VFSDISK_WRONG_SHAPE:   return "WRITTEN BY A BUILD WITH OTHER LIMITS";
    case VFSDISK_CORRUPT:       return "THAT DISK SAYS SOMETHING IMPOSSIBLE";
    }
    return "SOMETHING WENT WRONG";
}

enum vfsdisk_result vfsdisk_save(const struct vfs *fs, const struct disk *disk)
{
    if (fs == NULL) {
        return VFSDISK_CORRUPT;
    }
    if (!disk_present(disk)) {
        return VFSDISK_NO_DISK;
    }

    uint8_t record[NODE_BYTES];
    vfsdisk_write_header(record);
    if (!disk_write(disk, 0, record, 1)) {
        return VFSDISK_IO_FAILED;
    }

    for (uint64_t i = 0; i < VFS_MAX_NODES; i++) {
        vfsdisk_write_node(record, &fs->nodes[i]);
        if (!disk_write(disk, 1 + i * VFSDISK_NODE_SECTORS, record,
                        VFSDISK_NODE_SECTORS)) {
            return VFSDISK_IO_FAILED;
        }
    }

    /* Then the blocks, one to a sector. A block nobody is using is written as
     * zeros rather than as whatever it last held, for the same reason a free
     * node is: what a deleted file held should not be sitting on the disk. */
    for (uint64_t i = 0; i < VFS_MAX_BLOCKS; i++) {
        if (fs->block_used[i]) {
            for (uint64_t at = 0; at < VFS_BLOCK; at++) {
                record[at] = (uint8_t)fs->blocks[i][at];
            }
        } else {
            for (uint64_t at = 0; at < VFS_BLOCK; at++) {
                record[at] = 0;
            }
        }
        if (!disk_write(disk, VFSDISK_BLOCKS_AT + i, record, 1)) {
            return VFSDISK_IO_FAILED;
        }
    }
    return VFSDISK_OK;
}

enum vfsdisk_result vfsdisk_load(struct vfs *fs, const struct disk *disk)
{
    if (fs == NULL) {
        return VFSDISK_CORRUPT;
    }
    if (!disk_present(disk)) {
        return VFSDISK_NO_DISK;
    }

    uint8_t record[NODE_BYTES];
    if (!disk_read(disk, 0, record, 1)) {
        return VFSDISK_IO_FAILED;
    }
    const enum vfsdisk_result header = vfsdisk_judge_header(record);
    if (header != VFSDISK_OK) {
        return header;
    }

    /* Read straight into the filesystem, then judge the whole thing before
     * anybody walks it. Reading into a copy first would want fifty kilobytes of
     * stack, and the alternative to a copy is to leave it empty when the
     * judgement goes against it, which is what happens below. */
    for (uint64_t i = 0; i < VFS_MAX_NODES; i++) {
        if (!disk_read(disk, 1 + i * VFSDISK_NODE_SECTORS, record,
                       VFSDISK_NODE_SECTORS)) {
            vfs_init(fs);
            return VFSDISK_IO_FAILED;
        }
        vfsdisk_read_node(record, &fs->nodes[i]);
    }

    for (uint64_t i = 0; i < VFS_MAX_BLOCKS; i++) {
        if (!disk_read(disk, VFSDISK_BLOCKS_AT + i, record, 1)) {
            vfs_init(fs);
            return VFSDISK_IO_FAILED;
        }
        for (uint64_t at = 0; at < VFS_BLOCK; at++) {
            fs->blocks[i][at] = (char)record[at];
        }
    }

    /* Which blocks are spoken for is worked out from the files rather than read
     * off the disk. A bitmap on the disk is a second answer to a question the
     * files already answer, and when two answers disagree the machine gets to
     * hand the same block to two files. `vfsdisk_sound` builds it, and refuses
     * the disk if any two files claim the same block. */
    fs->cwd = 0;

    if (!vfsblock_rebuild(fs) || !vfsdisk_sound(fs)) {
        vfs_init(fs);
        return VFSDISK_CORRUPT;
    }
    /* What is in memory is now exactly what is on the disk, so there is nothing
     * outstanding to save. */
    fs->changes = 0;
    return VFSDISK_OK;
}
