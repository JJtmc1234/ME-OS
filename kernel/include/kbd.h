/* PS/2 keyboard input for M2.
 *
 * Polled, not interrupt driven. There is no interrupt descriptor table yet,
 * so an IRQ would triple fault the machine. Polling is slower and perfectly
 * adequate for proving a key press reaches the kernel.
 *
 * Scancode set 1, which is what the i8042 controller produces by default.
 * Only keys the M2 milestone needs are decoded.
 */
#ifndef ME_KBD_H
#define ME_KBD_H

#include <stdbool.h>
#include <stdint.h>

struct kbd_key {
    /* Printable keys: an uppercase letter, a digit, or a space.
     * Named keys: ch is 0 and name holds ENTER, ESCAPE, BACKSPACE or TAB. */
    char ch;
    const char *name;
};

/* Drains anything the firmware left in the controller's output buffer. */
void kbd_init(void);

/* True when a key was pressed since the last call. Key releases, key repeats
 * of the extended set, and unmapped keys are discarded. */
bool kbd_poll(struct kbd_key *out);

#endif /* ME_KBD_H */
