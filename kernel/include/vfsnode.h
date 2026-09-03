/* The parts of the filesystem its own source files share and nothing else uses.
 *
 * `vfs.h` is what the rest of the kernel is allowed to call. This is the layer
 * underneath it: walking a path down to a node, checking a name, taking a node
 * out of the table, and joining one into a directory. They were static
 * functions in one file until that file passed six hundred lines and stopped
 * being readable in a sitting.
 *
 * Not in `vfs.h`, deliberately. A shell command that could call `walk` directly
 * could hand out a node number, and every guard in the public functions is
 * written on the assumption that nothing outside has one.
 *
 * See M20 and M24 in docs/milestones.md.
 */
#ifndef ME_VFSNODE_H
#define ME_VFSNODE_H

#include "vfs.h"

uint64_t vfs_length_of(const char *text);

/* Compares a name against the first `b_length` characters of another, so a path
 * can be walked without being cut up into pieces first. */
bool vfs_names_match(const char *a, const char *b, uint64_t b_length);

/* The child of `dir` with this name, or VFS_NONE. */
int16_t vfs_child_named(const struct vfs *fs, int16_t dir, const char *name,
                        uint64_t length);

/* Follows a path to the node it names.
 *
 * With `parent_only`, stops at the directory that would hold the last piece and
 * hands that piece back through `name` and `length`, which is what making
 * something needs: where to put it, and what to call it.
 */
int16_t vfs_walk(const struct vfs *fs, const char *path, bool parent_only,
                 const char **name, uint64_t *length);

/* Whether this is a name a file may have. Empty ones, `.`, `..` and anything
 * too long to hold are not, because none of them could be named again
 * afterwards. */
bool vfs_usable_name(const char *name, uint64_t length);

void vfs_unlink_from_parent(struct vfs *fs, int16_t at);
void vfs_link_into(struct vfs *fs, int16_t parent, int16_t at);

/* Makes a directory or an empty file at `path`. `out` receives the new node
 * when it is not NULL. */
enum vfs_result vfs_make(struct vfs *fs, const char *path, enum vfs_kind kind,
                         int16_t *out);

#endif /* ME_VFSNODE_H */
