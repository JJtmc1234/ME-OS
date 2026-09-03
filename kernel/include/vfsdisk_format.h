/* The shape of the bytes on the disk, and nothing about when they are written.
 *
 * Split from `vfsdisk.c` because the two answer different questions. This one
 * says where each field sits in a record. That one says what to do when a disk
 * will not answer. Together they were one file nobody could read in a sitting.
 *
 *   header    0..7   magic          24..27  node record sectors
 *             8..11  version
 *            12..15  node count     16..19  file size   20..23  name size
 *
 *   node      0..23  name           24  kind        25  used
 *            26..27  parent         28..29  first child  30..31  next sibling
 *            32..35  length         64..    contents
 *
 * Written a byte at a time rather than by copying the structures, because a C
 * structure has padding the compiler chooses and a disk written by one build
 * would be read crooked by the next.
 *
 * The gap between 36 and 64 is deliberate. It leaves room to add a field
 * without moving the contents, which would make every existing disk unreadable.
 *
 * See M23 in docs/milestones.md.
 */
#ifndef ME_VFSDISK_FORMAT_H
#define ME_VFSDISK_FORMAT_H

#include "vfsdisk.h"

#define VFSDISK_NODE_BYTES (VFSDISK_NODE_SECTORS * DISK_SECTOR)

void vfsdisk_write_header(uint8_t *sector);
/* Which of the header's promises this build can keep. A disk that is not ours
 * at all is not the same news as one of ours we cannot read, so the order the
 * checks run in is part of the answer. */
enum vfsdisk_result vfsdisk_judge_header(const uint8_t *sector);

void vfsdisk_write_node(uint8_t *record, const struct vfs_node *node);
void vfsdisk_read_node(const uint8_t *record, struct vfs_node *node);

#endif /* ME_VFSDISK_FORMAT_H */
