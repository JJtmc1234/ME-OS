/* The ATA disk controller, driven a word at a time by the processor.
 *
 * Programmed input and output, not direct memory access. DMA is faster and
 * needs a command list, physical region descriptors and memory the controller
 * is allowed to write to on its own, none of which this kernel has yet. PIO
 * needs two ports and a loop, and a filesystem that saves when you ask it to
 * does not care that the save took a millisecond longer.
 *
 * Twenty eight bit addressing, so it reaches 128 gigabytes. The disk this is
 * written for is a few hundred kilobytes.
 *
 * Every wait here is bounded. A controller that is not there leaves the bus
 * floating and every read comes back as 0xFF, which has the busy bit set, so an
 * unbounded wait for "not busy" on a machine with no disk would hang the kernel
 * before it drew anything. Asking is allowed to fail. Hanging is not.
 *
 * See M23 in docs/milestones.md.
 */
#ifndef ME_ATA_H
#define ME_ATA_H

#include "disk.h"

/* The two legacy channels. A disk is at one of these on any machine with an IDE
 * controller, which is what QEMU's `pc` machine and VirtualBox's PIIX both
 * present. A machine that only has AHCI has nothing here, and this says so
 * rather than pretending. */
#define ATA_PRIMARY_IO        0x1F0
#define ATA_PRIMARY_CONTROL   0x3F6
#define ATA_SECONDARY_IO      0x170
#define ATA_SECONDARY_CONTROL 0x376

struct ata_drive {
    uint16_t io;
    uint16_t control;
    /* 0xE0 for the master, 0xF0 for the slave, with the top address bits added
     * per command. */
    uint8_t select;
    bool present;
    uint64_t sectors;
    /* What the drive calls itself, so the machine can say which disk it found
     * rather than only that it found one. */
    char model[41];
};

/* Asks whether there is a drive there. False means nothing answered, or what
 * answered is not a disk this can read, such as a CD drive. */
bool ata_probe(struct ata_drive *drive, uint16_t io, uint16_t control, bool slave);

/* Fills in a `struct disk` that reads and writes through this drive. */
void ata_as_disk(struct ata_drive *drive, struct disk *out);

#endif /* ME_ATA_H */
