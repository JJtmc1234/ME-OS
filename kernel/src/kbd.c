#include "kbd.h"

#include <stddef.h>

#define PS2_DATA   0x60
#define PS2_STATUS 0x64

#define STATUS_OUTPUT_FULL 0x01  /* a byte is waiting in the data port */
#define STATUS_FROM_MOUSE  0x20  /* that byte came from the auxiliary device */

#define SCANCODE_RELEASE 0x80    /* set in the make code of a key being released */
#define SCANCODE_EXTENDED 0xE0   /* prefix, the real code is the next byte */

#define SCANCODE_LEFT_SHIFT  0x2A
#define SCANCODE_RIGHT_SHIFT 0x36

static uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Scancode set 1 make codes for the keys M2 decodes. Everything else is 0,
 * which means "not a key this milestone knows about". */
static const char printable[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M',
    [0x39] = ' ',
    /* Arithmetic, from the main row and the keypad. No shift handling, so the
     * unshifted key is what counts: '=' is a key, '+' comes from the keypad. */
    [0x0C] = '-', [0x0D] = '=', [0x35] = '/',
    [0x4A] = '-', [0x4E] = '+', [0x37] = '*',
};

/* Only the shifted keys the calculator needs. Everything else is unaffected,
 * since the letters this milestone decodes are already uppercase. */
static const char shifted[128] = {
    [0x07] = '^',   /* shift and 6 */
    [0x09] = '*',   /* shift and 8 */
    [0x0D] = '+',   /* shift and equals */
};

static const char *named(uint8_t code)
{
    switch (code) {
    case 0x01: return "ESCAPE";
    case 0x0E: return "BACKSPACE";
    case 0x0F: return "TAB";
    case 0x1C: return "ENTER";
    default:   return NULL;
    }
}

bool kbd_shift_after(uint8_t code, bool shift)
{
    const uint8_t key = (uint8_t)(code & 0x7F);
    if (key != SCANCODE_LEFT_SHIFT && key != SCANCODE_RIGHT_SHIFT) {
        return shift;
    }
    return (code & SCANCODE_RELEASE) == 0;
}

bool kbd_translate(uint8_t code, bool shift, struct kbd_key *out)
{
    if (out == NULL || (code & SCANCODE_RELEASE) != 0) {
        return false;
    }

    const uint8_t key = (uint8_t)(code & 0x7F);

    if (shift && shifted[key] != '\0') {
        out->ch = shifted[key];
        out->name = NULL;
        return true;
    }

    const char ch = printable[key];
    if (ch != '\0') {
        out->ch = ch;
        out->name = NULL;
        return true;
    }

    const char *name = named(key);
    if (name != NULL) {
        out->ch = '\0';
        out->name = name;
        return true;
    }
    return false;
}

static bool byte_waiting(void)
{
    uint8_t status = inb(PS2_STATUS);
    return (status & STATUS_OUTPUT_FULL) != 0
        && (status & STATUS_FROM_MOUSE) == 0;
}

void kbd_init(void)
{
    /* The firmware may have left keystrokes behind. Bounded, so a controller
     * that always reports data cannot hang the boot. */
    for (int i = 0; i < 64 && byte_waiting(); i++) {
        (void)inb(PS2_DATA);
    }
}

bool kbd_poll(struct kbd_key *out)
{
    static bool extended = false;
    static bool shift = false;

    if (out == NULL || !byte_waiting()) {
        return false;
    }

    uint8_t code = inb(PS2_DATA);

    if (code == SCANCODE_EXTENDED) {
        extended = true;
        return false;
    }
    /* Arrow keys and friends arrive with the extended prefix. Nothing so far
     * has anything to do with them, so the code after the prefix is dropped. */
    if (extended) {
        extended = false;
        return false;
    }

    /* Shift has to be seen going down and coming back up, so this happens
     * before releases are discarded. */
    shift = kbd_shift_after(code, shift);

    return kbd_translate(code, shift, out);
}
