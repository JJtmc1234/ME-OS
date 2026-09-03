/* Finding out whether there is a disk there, and what it is.
 *
 * Split from the transfer half because it is a different job with a different
 * way to go wrong. Moving sectors assumes a drive. This decides whether there
 * is one, and most of it is about the ways a machine says no.
 *
 * There are three of those, and telling them apart matters. A status of 0xFF is
 * a floating bus, meaning no controller at all. A status of 0 after a select
 * means the controller is there and that socket is empty. A drive that puts a
 * signature in the address registers is answering a different protocol, which
 * is what a CD drive does, and reading sectors off it this way returns rubbish
 * rather than failing.
 */
#include "ataport.h"

/* Words 60 and 61 of the identify block hold the twenty eight bit sector count,
 * low word first. */
#define IDENTIFY_SECTORS 60
/* Words 27 to 46 hold the model, as pairs of characters with the two in each
 * word the wrong way round. */
#define IDENTIFY_MODEL   27
#define MODEL_WORDS      20

static void read_model(struct ata_drive *drive, const uint16_t *identify)
{
    for (uint64_t i = 0; i < MODEL_WORDS; i++) {
        const uint16_t word = identify[IDENTIFY_MODEL + i];
        drive->model[i * 2] = (char)(word >> 8);
        drive->model[i * 2 + 1] = (char)(word & 0xFF);
    }
    drive->model[MODEL_WORDS * 2] = '\0';

    /* Drives pad the model with spaces to fill the field. Trailing ones would
     * be printed, so the name of the disk would be forty characters wide
     * whatever it actually says. */
    for (int64_t i = MODEL_WORDS * 2 - 1; i >= 0; i--) {
        if (drive->model[i] != ' ') {
            break;
        }
        drive->model[i] = '\0';
    }
}

bool ata_probe(struct ata_drive *drive, uint16_t io, uint16_t control, bool slave)
{
    if (drive == NULL) {
        return false;
    }
    *drive = (struct ata_drive){0};
    drive->io = io;
    drive->control = control;
    drive->select = slave ? 0xF0u : 0xE0u;

    /* Before selecting anything. A floating bus reads as all ones on every
     * port, and there is no point sending commands into one. */
    if (ata_inb(io + REG_STATUS) == 0xFF) {
        return false;
    }

    /* No interrupts from this channel. Nothing in this kernel handles one: there
     * is no interrupt table and the processor is still running with them
     * masked, so every wait here polls. Saying so to the controller rather than
     * relying on the mask means the driver keeps working the day something else
     * turns interrupts on. */
    ata_outb(control, 0x02);

    ata_outb(io + REG_SELECT, drive->select);
    ata_settle(drive);
    /* Zeroed so the signature check below means something. A drive that is not
     * an ordinary disk writes its own values into these. */
    ata_outb(io + REG_COUNT, 0);
    ata_outb(io + REG_LBA_LOW, 0);
    ata_outb(io + REG_LBA_MID, 0);
    ata_outb(io + REG_LBA_HIGH, 0);
    ata_outb(io + REG_STATUS, CMD_IDENTIFY);

    if (ata_inb(io + REG_STATUS) == 0) {
        return false;   /* the controller is there and the socket is empty */
    }
    uint8_t status = 0;
    if (!ata_wait_while_busy(drive, &status)) {
        return false;
    }
    /* A CD drive answers identify by putting 0x14 0xEB here and failing the
     * command. Reading sectors off one with this driver would return rubbish
     * rather than an error, which is worse than not finding it. */
    if (ata_inb(io + REG_LBA_MID) != 0 || ata_inb(io + REG_LBA_HIGH) != 0) {
        return false;
    }
    if (!ata_wait_for_data(drive)) {
        return false;
    }

    uint16_t identify[256];
    for (uint64_t i = 0; i < 256; i++) {
        identify[i] = ata_inw(io + REG_DATA);
    }

    drive->sectors = (uint64_t)identify[IDENTIFY_SECTORS] |
                     ((uint64_t)identify[IDENTIFY_SECTORS + 1] << 16);
    if (drive->sectors == 0) {
        /* A disk with no sectors is not a disk. Believing it would make every
         * bounds check in `disk.c` pass, since a size of zero means "did not
         * say" there. */
        return false;
    }
    read_model(drive, identify);
    drive->present = true;
    return true;
}
