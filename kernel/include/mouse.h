/* PS/2 mouse input for M4.
 *
 * Polled, like the keyboard, because there is still no interrupt table. The
 * controller's status register says which device a waiting byte came from, so
 * the mouse and the keyboard can share one port without stealing each other's
 * bytes.
 *
 * Decoding is separated from the port I/O so it can be tested on an ordinary
 * machine: mouse_decode takes three bytes and gives back a movement.
 */
#ifndef ME_MOUSE_H
#define ME_MOUSE_H

#include <stdbool.h>
#include <stdint.h>

struct mouse_delta {
    /* Screen space: x grows right, y grows down. The device reports y the
     * other way up, and mouse_decode has already flipped it. */
    int32_t dx;
    int32_t dy;
    bool left;
    bool right;
    bool middle;
};

/* True when the three bytes are a well formed packet. Rejects a packet whose
 * sync bit is missing, or whose counters overflowed and are meaningless. */
bool mouse_decode(const uint8_t bytes[3], struct mouse_delta *out);

/* Enables the auxiliary device and asks it to report movement. Returns false
 * if the controller never acknowledges, in which case there is no mouse and
 * the rest of the system carries on without one. */
bool mouse_init(void);

/* True when a complete packet arrived since the last call. */
bool mouse_poll(struct mouse_delta *out);

#endif /* ME_MOUSE_H */
