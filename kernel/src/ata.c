#include "ataport.h"

/* Sets up one command against one address. Shared by reading and writing
 * because getting the six registers in a different order in two places is how
 * one of them ends up reading the wrong sector. */
static bool begin(const struct ata_drive *drive, uint64_t sector, uint8_t count,
                  uint8_t command)
{
    if (!drive->present || sector > 0x0FFFFFFFu) {
        return false;
    }
    if (!ata_wait_while_busy(drive, NULL)) {
        return false;
    }
    ata_outb(drive->io + REG_SELECT,
         (uint8_t)(drive->select | ((sector >> 24) & 0x0Fu)));
    ata_settle(drive);
    ata_outb(drive->io + REG_COUNT, count);
    ata_outb(drive->io + REG_LBA_LOW, (uint8_t)sector);
    ata_outb(drive->io + REG_LBA_MID, (uint8_t)(sector >> 8));
    ata_outb(drive->io + REG_LBA_HIGH, (uint8_t)(sector >> 16));
    ata_outb(drive->io + REG_STATUS, command);
    return true;
}

static bool ata_read_sectors(void *context, uint64_t sector, void *out,
                             uint64_t count)
{
    struct ata_drive *drive = context;
    if (drive == NULL || count == 0 || count > 255) {
        return false;
    }
    if (!begin(drive, sector, (uint8_t)count, CMD_READ)) {
        return false;
    }

    uint16_t *words = out;
    for (uint64_t s = 0; s < count; s++) {
        /* Waited for once per sector. The controller raises the data request
         * again for each one, and reading ahead of it gets the last sector
         * twice. */
        if (!ata_wait_for_data(drive)) {
            return false;
        }
        for (uint64_t i = 0; i < DISK_SECTOR / 2; i++) {
            words[s * (DISK_SECTOR / 2) + i] = ata_inw(drive->io + REG_DATA);
        }
    }
    return true;
}

static bool ata_write_sectors(void *context, uint64_t sector, const void *in,
                              uint64_t count)
{
    struct ata_drive *drive = context;
    if (drive == NULL || count == 0 || count > 255) {
        return false;
    }
    if (!begin(drive, sector, (uint8_t)count, CMD_WRITE)) {
        return false;
    }

    const uint16_t *words = in;
    for (uint64_t s = 0; s < count; s++) {
        if (!ata_wait_for_data(drive)) {
            return false;
        }
        for (uint64_t i = 0; i < DISK_SECTOR / 2; i++) {
            ata_outw(drive->io + REG_DATA, words[s * (DISK_SECTOR / 2) + i]);
        }
    }

    /* The drive is allowed to keep a write in its own cache and say it is done.
     * Without this the data is on the disk only if the machine stays on, which
     * is the opposite of the point. */
    ata_outb(drive->io + REG_STATUS, CMD_FLUSH);
    uint8_t status = 0;
    return ata_wait_while_busy(drive, &status) && (status & STATUS_ERR) == 0;
}

void ata_as_disk(struct ata_drive *drive, struct disk *out)
{
    if (out == NULL) {
        return;
    }
    if (drive == NULL || !drive->present) {
        *out = (struct disk){0};
        return;
    }
    out->read = ata_read_sectors;
    out->write = ata_write_sectors;
    out->context = drive;
    out->sectors = drive->sectors;
}
