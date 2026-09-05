/* What is inside a file: writing it, adding to it, and reading it back.
 *
 * Where the blocks come from is in `vfsblock.c`. This file is about what a
 * write means, which is mostly about what it refuses: a file longer than a file
 * can be, a write into a directory, and a read into a buffer too small for it.
 *
 * See M20 and M24 in docs/milestones.md.
 */
#include "vfsnode.h"

#include "vfsblock.h"

/* Finds a file to write to, making it if it is not there, which is what every
 * shell does when it is told to write somewhere. */
static enum vfs_result file_for_writing(struct vfs *fs, const char *path,
                                        int16_t *out)
{
    int16_t at = vfs_resolve(fs, path);
    if (at == VFS_NONE) {
        const enum vfs_result made = vfs_make(fs, path, VFS_FILE, &at);
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
    /* Replacing, so the old contents go back to the pool before the new ones
     * are asked for. Otherwise rewriting a large file with a small one would
     * hold the large one's room until the file was deleted. */
    vfsblock_release(fs, &fs->nodes[at]);
    return vfs_append(fs, path, text);
}

enum vfs_result vfs_write_bytes(struct vfs *fs, const char *path,
                                const char *data, uint64_t length)
{
    if (fs == NULL) {
        return VFS_BAD_NAME;
    }
    int16_t at = VFS_NONE;
    const enum vfs_result found = file_for_writing(fs, path, &at);
    if (found != VFS_OK) {
        return found;
    }
    vfsblock_release(fs, &fs->nodes[at]);
    return vfs_append_bytes(fs, path, data, length);
}

enum vfs_result vfs_append(struct vfs *fs, const char *path, const char *text)
{
    /* The length is where the first zero byte is, which is right for text and
     * wrong for anything else. vfs_append_bytes is for anything else. */
    return vfs_append_bytes(fs, path, text, vfs_length_of(text));
}

enum vfs_result vfs_append_bytes(struct vfs *fs, const char *path,
                                 const char *data, uint64_t adding)
{
    if (fs == NULL || data == NULL) {
        return VFS_BAD_NAME;
    }
    int16_t at = VFS_NONE;
    const enum vfs_result found = file_for_writing(fs, path, &at);
    if (found != VFS_OK) {
        return found;
    }

    struct vfs_node *node = &fs->nodes[at];
    /* Refused rather than cut. A file that quietly holds less than it was given
     * is a file whose contents nobody can trust. */
    if (node->length + adding > VFS_FILE_MAX) {
        return VFS_TOO_BIG;
    }
    /* Every block it will need, before a byte is written. Taking them as it
     * went would leave the file longer and half written when the pool ran out
     * in the middle, and there is no way back from that. */
    if (!vfsblock_reserve(fs, node, vfs_blocks_for(node->length + adding))) {
        return VFS_NO_SPACE;
    }
    for (uint64_t i = 0; i < adding; i++) {
        char *byte = vfsblock_at(fs, node, node->length + i);
        if (byte == NULL) {
            return VFS_NO_SPACE;
        }
        *byte = data[i];
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
        const char *byte = vfsblock_read_at(fs, node, written);
        if (byte == NULL) {
            break;
        }
        out[written] = *byte;
        written++;
    }
    out[written] = '\0';
    if (length != NULL) {
        *length = written;
    }
    return VFS_OK;
}
