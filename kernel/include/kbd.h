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
    /* Control was held when this key went down. Filled in by `kbd_poll`, which
     * is the only thing that tracks modifier state; the pure translators leave
     * it false because a scancode on its own does not say what was held.
     *
     * Control rather than Super, which is what a tiling desktop would normally
     * use. The keyboard would decode Super perfectly well, and neither QEMU nor
     * VirtualBox reliably delivers it: the host's own window manager takes it
     * first. A shortcut that works on one machine and silently does nothing on
     * the next is worse than a different shortcut. See M18. */
    bool ctrl;
};

/* Drains anything the firmware left in the controller's output buffer. */
void kbd_init(void);

/* Pure: the shift state after seeing one scancode byte. Shift is the only
 * modifier decoded, because it is the only one the symbols need. */
bool kbd_shift_after(uint8_t code, bool shift);

/* Pure: the control state after seeing one scancode byte. Both control keys
 * report scancode 0x1D, the right one behind the extended prefix, so the same
 * rule serves for either. */
bool kbd_ctrl_after(uint8_t code, bool ctrl);

/* Pure: the key a make code means, with shift held or not. False when this
 * milestone does not decode that key. */
bool kbd_translate(uint8_t code, bool shift, struct kbd_key *out);

/* Pure: the same for a code that followed the extended prefix. The arrow keys
 * are the only ones decoded, because they are the only ones anything uses.
 * They are named rather than printable, so they cannot end up in a typed sum. */
bool kbd_translate_extended(uint8_t code, struct kbd_key *out);

/* True when a key was pressed since the last call. Key releases, key repeats
 * of the extended set, and unmapped keys are discarded. */
bool kbd_poll(struct kbd_key *out);

#endif /* ME_KBD_H */
