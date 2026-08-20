#include "mouse.h"

#include <stddef.h>

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define STATUS_OUTPUT_FULL 0x01
#define STATUS_INPUT_FULL  0x02
#define STATUS_FROM_MOUSE  0x20

#define CMD_ENABLE_AUX      0xA8
#define CMD_WRITE_TO_MOUSE  0xD4
#define MOUSE_ENABLE_REPORT 0xF4
#define MOUSE_SET_DEFAULTS  0xF6
#define MOUSE_ACK           0xFA

#define PACKET_SYNC     0x08  /* always set in a valid first byte */
#define PACKET_X_SIGN   0x10
#define PACKET_Y_SIGN   0x20
#define PACKET_X_OVER   0x40
#define PACKET_Y_OVER   0x80

#define SPIN_LIMIT 100000

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

bool mouse_decode(const uint8_t bytes[3], struct mouse_delta *out)
{
    if (out == NULL || bytes == NULL) {
        return false;
    }

    const uint8_t flags = bytes[0];
    if ((flags & PACKET_SYNC) == 0) {
        return false;  /* not the start of a packet */
    }
    if ((flags & (PACKET_X_OVER | PACKET_Y_OVER)) != 0) {
        return false;  /* the counters wrapped, the movement is meaningless */
    }

    int32_t dx = (int32_t)bytes[1];
    int32_t dy = (int32_t)bytes[2];
    if ((flags & PACKET_X_SIGN) != 0) {
        dx -= 256;
    }
    if ((flags & PACKET_Y_SIGN) != 0) {
        dy -= 256;
    }

    out->dx = dx;
    out->dy = -dy;  /* the device counts up, the screen counts down */
    out->left = (flags & 0x01) != 0;
    out->right = (flags & 0x02) != 0;
    out->middle = (flags & 0x04) != 0;
    return true;
}

static bool wait_writable(void)
{
    for (int spins = 0; spins < SPIN_LIMIT; spins++) {
        if ((inb(PS2_STATUS) & STATUS_INPUT_FULL) == 0) {
            return true;
        }
    }
    return false;
}

static bool wait_readable(void)
{
    for (int spins = 0; spins < SPIN_LIMIT; spins++) {
        if ((inb(PS2_STATUS) & STATUS_OUTPUT_FULL) != 0) {
            return true;
        }
    }
    return false;
}

static bool send_to_mouse(uint8_t command)
{
    if (!wait_writable()) {
        return false;
    }
    outb(PS2_CMD, CMD_WRITE_TO_MOUSE);
    if (!wait_writable()) {
        return false;
    }
    outb(PS2_DATA, command);
    if (!wait_readable()) {
        return false;
    }
    return inb(PS2_DATA) == MOUSE_ACK;
}

bool mouse_init(void)
{
    if (!wait_writable()) {
        return false;
    }
    outb(PS2_CMD, CMD_ENABLE_AUX);

    /* Defaults first: 100 reports a second, one count per unit of movement. */
    if (!send_to_mouse(MOUSE_SET_DEFAULTS)) {
        return false;
    }
    return send_to_mouse(MOUSE_ENABLE_REPORT);
}

bool mouse_poll(struct mouse_delta *out)
{
    static uint8_t packet[3];
    static int have = 0;

    if (out == NULL) {
        return false;
    }

    while (true) {
        uint8_t status = inb(PS2_STATUS);
        if ((status & STATUS_OUTPUT_FULL) == 0 || (status & STATUS_FROM_MOUSE) == 0) {
            return false;  /* nothing waiting, or it belongs to the keyboard */
        }

        uint8_t byte = inb(PS2_DATA);
        if (have == 0 && (byte & PACKET_SYNC) == 0) {
            continue;  /* out of step, wait for a byte that can start a packet */
        }

        packet[have++] = byte;
        if (have < 3) {
            continue;
        }
        have = 0;
        if (mouse_decode(packet, out)) {
            return true;
        }
    }
}
