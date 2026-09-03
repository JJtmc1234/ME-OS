#include "disk.h"

bool disk_present(const struct disk *disk)
{
    return disk != NULL && disk->read != NULL && disk->write != NULL;
}

/* The one place a request is checked against the end of the device.
 *
 * Sector plus count can wrap on a large enough count, and a wrapped sum is a
 * small number that passes every comparison after it, so the count is checked
 * on its own first. */
static bool within(const struct disk *disk, uint64_t sector, uint64_t count)
{
    if (count == 0) {
        return false;
    }
    if (disk->sectors == 0) {
        /* The device did not say how big it is, so nothing here can tell. The
         * driver has to, and saying so is better than inventing a limit. */
        return true;
    }
    return sector < disk->sectors && count <= disk->sectors - sector;
}

bool disk_read(const struct disk *disk, uint64_t sector, void *out, uint64_t count)
{
    if (!disk_present(disk) || out == NULL || !within(disk, sector, count)) {
        return false;
    }
    return disk->read(disk->context, sector, out, count);
}

bool disk_write(const struct disk *disk, uint64_t sector, const void *in,
                uint64_t count)
{
    if (!disk_present(disk) || in == NULL || !within(disk, sector, count)) {
        return false;
    }
    return disk->write(disk->context, sector, in, count);
}
