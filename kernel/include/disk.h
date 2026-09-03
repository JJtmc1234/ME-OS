/* A block device: sectors you can read and sectors you can write.
 *
 * Given as two function pointers rather than called directly, so everything
 * built on top of it can be tested on the development machine against a disk
 * made of memory. The alternative is a format nobody can check without booting
 * an emulator, and a format that is only ever exercised by hand is a format
 * that eats a filesystem the first time it is wrong.
 *
 * Sectors, not bytes. That is the unit the hardware actually moves, and
 * pretending otherwise would put a buffer and a read modify write cycle in the
 * one place that has to be simple enough to trust.
 *
 * See M23 in docs/milestones.md.
 */
#ifndef ME_DISK_H
#define ME_DISK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DISK_SECTOR 512

struct disk {
    /* Both return false on any failure, including a sector past the end. A
     * short read is a failure, not a partial success: half a node is worse
     * than no node, because it looks like data. */
    bool (*read)(void *context, uint64_t sector, void *out, uint64_t count);
    bool (*write)(void *context, uint64_t sector, const void *in, uint64_t count);
    void *context;
    /* How many sectors the device holds. Zero means it did not say, in which
     * case nothing here can check a request against the end of the disk and
     * the driver has to. */
    uint64_t sectors;
};

/* True when there is a device with both halves of the interface. A disk with
 * only a read is not a disk this can save to, and finding that out at the last
 * sector of a save is too late. */
bool disk_present(const struct disk *disk);

bool disk_read(const struct disk *disk, uint64_t sector, void *out, uint64_t count);
bool disk_write(const struct disk *disk, uint64_t sector, const void *in,
                uint64_t count);

#endif /* ME_DISK_H */
