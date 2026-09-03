/* The registers of an ATA channel, and the four waits worth naming.
 *
 * A header rather than a source file because both halves of the driver need
 * them and neither owns them. Finding a drive and moving sectors are different
 * jobs, kept in different files, but they talk to the same seven ports and a
 * second copy of "wait while busy" is a second chance to get the busy bit
 * wrong.
 *
 * See M23 in docs/milestones.md.
 */
#ifndef ME_ATAPORT_H
#define ME_ATAPORT_H

#include "ata.h"

#define REG_DATA     0
#define REG_ERROR    1
#define REG_COUNT    2
#define REG_LBA_LOW  3
#define REG_LBA_MID  4
#define REG_LBA_HIGH 5
#define REG_SELECT   6
/* Reading it gives the status. Writing it takes a command. */
#define REG_STATUS   7

#define STATUS_ERR   0x01
#define STATUS_DRQ   0x08
#define STATUS_FAULT 0x20
#define STATUS_READY 0x40
#define STATUS_BUSY  0x80

#define CMD_READ     0x20
#define CMD_WRITE    0x30
#define CMD_FLUSH    0xE7
#define CMD_IDENTIFY 0xEC

/* How many times a wait looks before giving up. Large enough that a real disk
 * finishes long before it, small enough that a machine with no disk gets
 * through boot rather than stopping in a loop nobody can see.
 *
 * This is the whole reason every wait here is bounded. A controller that is not
 * there leaves the bus floating, so every read comes back as 0xFF, which has
 * the busy bit set. An unbounded wait for "not busy" would hang the kernel
 * before it drew anything, on any machine without an IDE controller. */
#define ATA_PATIENCE 2000000u

static inline void ata_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint8_t ata_inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t ata_inw(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void ata_outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" :: "a"(value), "Nd"(port));
}

/* The controller needs about 400 nanoseconds after a drive select before the
 * status register means anything. Four reads of the alternate status port is
 * how everybody does it: it takes the right amount of time and, unlike the real
 * status register, reading it does not clear a pending interrupt. */
static inline void ata_settle(const struct ata_drive *drive)
{
    for (int i = 0; i < 4; i++) {
        (void)ata_inb(drive->control);
    }
}

static inline bool ata_wait_while_busy(const struct ata_drive *drive, uint8_t *last)
{
    for (uint32_t i = 0; i < ATA_PATIENCE; i++) {
        const uint8_t status = ata_inb(drive->io + REG_STATUS);
        if ((status & STATUS_BUSY) == 0) {
            if (last != NULL) {
                *last = status;
            }
            return true;
        }
    }
    return false;
}

/* Not busy, and with a sector's worth of data ready to move. An error or a
 * fault ends the wait rather than being looked past, because after either of
 * them the data register holds nothing worth reading. */
static inline bool ata_wait_for_data(const struct ata_drive *drive)
{
    for (uint32_t i = 0; i < ATA_PATIENCE; i++) {
        const uint8_t status = ata_inb(drive->io + REG_STATUS);
        if ((status & (STATUS_ERR | STATUS_FAULT)) != 0) {
            return false;
        }
        if ((status & STATUS_BUSY) == 0 && (status & STATUS_DRQ) != 0) {
            return true;
        }
    }
    return false;
}

#endif /* ME_ATAPORT_H */
