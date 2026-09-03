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
#define SCANCODE_CTRL        0x1D

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
    /* Punctuation, added in M20 when there were filenames to type. Until then
     * these keys were only known with shift held, so README.TXT could not be
     * typed at the shell that had just listed it. */
    [0x33] = ',', [0x34] = '.',
    /* The backslash key, added in M25 when the shell learned about pipes. Its
     * shifted form is the bar, and a shell that understands `A | B` with no way
     * to type the bar understands nothing. */
    [0x2B] = '\\',
};

/* Only the shifted keys the calculator needs. Everything else is unaffected,
 * since the letters this milestone decodes are already uppercase. */
static const char shifted[128] = {
    [0x07] = '^',   /* shift and 6 */
    [0x09] = '*',   /* shift and 8 */
    [0x0D] = '+',   /* shift and equals */
    [0x33] = '<',   /* shift and comma */
    [0x34] = '>',   /* shift and full stop */
    [0x0C] = '_',   /* shift and minus, for names with words in them */
    [0x2B] = '|',   /* shift and backslash, the pipe */
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

bool kbd_ctrl_after(uint8_t code, bool ctrl)
{
    const uint8_t key = code & (uint8_t)~SCANCODE_RELEASE;
    if (key != SCANCODE_CTRL) {
        return ctrl;
    }
    return (code & SCANCODE_RELEASE) == 0;
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
    /* False here because a scancode on its own does not say what was held.
     * kbd_poll is the only thing that tracks modifiers and it fills this in. */
    out->ctrl = false;

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

bool kbd_translate_extended(uint8_t code, struct kbd_key *out)
{
    if (out == NULL || (code & SCANCODE_RELEASE) != 0) {
        return false;
    }

    const char *name;
    switch (code) {
    case 0x48: name = "UP";    break;
    case 0x50: name = "DOWN";  break;
    case 0x4B: name = "LEFT";  break;
    case 0x4D: name = "RIGHT"; break;
    default:   return false;
    }

    out->ch = '\0';
    out->name = name;
    out->ctrl = false;
    return true;
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
    static bool ctrl = false;

    if (out == NULL || !byte_waiting()) {
        return false;
    }

    uint8_t code = inb(PS2_DATA);

    if (code == SCANCODE_EXTENDED) {
        extended = true;
        return false;
    }
    /* The byte after the prefix is the real key. The arrows are decoded and
     * everything else in the extended set is dropped. */
    if (extended) {
        extended = false;
        /* Before the release check inside the translator, because the right
         * control key lives out here and has to be seen going up as well as
         * coming down. */
        ctrl = kbd_ctrl_after(code, ctrl);
        if (!kbd_translate_extended(code, out)) {
            return false;
        }
        out->ctrl = ctrl;
        return true;
    }

    /* Shift has to be seen going down and coming back up, so this happens
     * before releases are discarded. */
    shift = kbd_shift_after(code, shift);
    ctrl = kbd_ctrl_after(code, ctrl);

    if (!kbd_translate(code, shift, out)) {
        return false;
    }
    out->ctrl = ctrl;
    return true;
}
