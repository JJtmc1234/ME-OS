/* The filesystem: directories, files, and the paths that reach them.
 *
 * Everything here works on the copy in memory. That was all there was in M20,
 * and the commands said so every time DF was typed. M23 put a disk under it:
 * `vfsdisk.c` writes this out and reads it back, and the shell saves after
 * anything that changed something.
 *
 * Nothing about a disk is in this file, deliberately. A filesystem that had to
 * reach a controller to make a directory could not be tested without one, and
 * the rules about names, paths and what may be deleted are the same either way.
 *
 * See M20 and M23 in docs/milestones.md.
 */
#ifndef ME_VFS_H
#define ME_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_NODES 96
#define VFS_NAME_MAX  24
/* One fixed block per file, held inside the node.
 *
 * Deliberately not an allocator. A bump allocator would leak on every rewrite
 * and a real one is a milestone of its own, whereas a fixed block cannot
 * fragment, cannot leak and cannot be got wrong. The cost is that a small file
 * takes as much room as a large one, which matters when there is a disk and
 * does not matter yet. */
#define VFS_FILE_MAX  512
#define VFS_PATH_MAX  256

/* Nothing, rather than a node. Signed so it can be told from index zero, which
 * is the root and is a perfectly good node. */
#define VFS_NONE ((int16_t)-1)

enum vfs_kind {
    VFS_DIR,
    VFS_FILE,
};

enum vfs_result {
    VFS_OK,
    VFS_NOT_FOUND,
    VFS_EXISTS,
    VFS_NOT_A_DIRECTORY,
    VFS_IS_A_DIRECTORY,
    VFS_NOT_EMPTY,
    VFS_NO_SPACE,
    VFS_TOO_BIG,
    VFS_BAD_NAME,
};

/* A sentence a person can act on, not an error number. */
const char *vfs_explain(enum vfs_result result);

struct vfs_node {
    char name[VFS_NAME_MAX];
    enum vfs_kind kind;
    bool used;
    int16_t parent;
    int16_t first_child;
    int16_t next_sibling;
    uint32_t length;
    char data[VFS_FILE_MAX];
};

struct vfs {
    struct vfs_node nodes[VFS_MAX_NODES];
    int16_t cwd;
    /* How many times anything in here has actually changed. Every operation
     * that succeeds moves it and every one that is refused does not.
     *
     * It exists so the shell can save to the disk only when there is something
     * to save. The alternative is for each command to remember to say it
     * changed something, and the one that forgets is the one that loses a file
     * on the next restart. */
    uint32_t changes;
};

/* An empty filesystem holding nothing but the root directory. */
void vfs_init(struct vfs *fs);

/* The node a path names, or VFS_NONE. A path starting with `/` is resolved from
 * the root and any other from the working directory. `.` stays put, `..` goes to
 * the parent, and the root's parent is the root, so no path can climb out. */
int16_t vfs_resolve(const struct vfs *fs, const char *path);

const struct vfs_node *vfs_get(const struct vfs *fs, int16_t node);
int16_t vfs_first_child(const struct vfs *fs, int16_t dir);
int16_t vfs_next_sibling(const struct vfs *fs, int16_t node);
uint64_t vfs_count_children(const struct vfs *fs, int16_t dir);

/* Writes the full path of a node, from the root. */
uint64_t vfs_path_of(const struct vfs *fs, int16_t node, char *out, uint64_t capacity);

enum vfs_result vfs_mkdir(struct vfs *fs, const char *path);
enum vfs_result vfs_create(struct vfs *fs, const char *path);
enum vfs_result vfs_write(struct vfs *fs, const char *path, const char *text);
/* Appends rather than replacing, so a file can be built up a line at a time. */
enum vfs_result vfs_append(struct vfs *fs, const char *path, const char *text);
enum vfs_result vfs_read(const struct vfs *fs, const char *path,
                         char *out, uint64_t capacity, uint64_t *length);
/* Removes a file, or a directory with nothing in it. A directory with contents
 * is refused rather than emptied: losing a tree to one mistyped word is not a
 * thing this should be able to do. */
enum vfs_result vfs_remove(struct vfs *fs, const char *path);
enum vfs_result vfs_chdir(struct vfs *fs, const char *path);

/* Moves an entry, which is a rename when the destination is in the same
 * directory. Refuses to move a directory into itself, because that would take
 * the whole subtree out of the tree and leave it pointing at its own parent. */
enum vfs_result vfs_move(struct vfs *fs, const char *from, const char *to);

/* Copies a file. Directories are refused: copying one means copying everything
 * under it, which is a different operation with a different way to fail. */
enum vfs_result vfs_copy(struct vfs *fs, const char *from, const char *to);

/* How many nodes are in use and how many there are, so a person can see the
 * limit rather than meeting it. */
uint64_t vfs_used_nodes(const struct vfs *fs);

#endif /* ME_VFS_H */
