/* Host unit test for keyboard translation.
 *
 * Scancode to character is a lookup and a shift flag, both pure, so all of it
 * can be checked without a keyboard. The symbols the calculator needs come
 * from shifted keys, which is the part most likely to be wrong.
 */
#include <stdio.h>
#include <string.h>

#include "kbd.h"

static int failures;

static void check(int condition, const char *what)
{
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

static int gives(uint8_t code, bool shift, char expected)
{
    struct kbd_key key;
    memset(&key, 0, sizeof key);
    if (!kbd_translate(code, shift, &key)) {
        return 0;
    }
    return key.ch == expected && key.name == NULL;
}

static int names(uint8_t code, const char *expected)
{
    struct kbd_key key;
    memset(&key, 0, sizeof key);
    if (!kbd_translate(code, false, &key)) {
        return 0;
    }
    return key.ch == '\0' && key.name != NULL && strcmp(key.name, expected) == 0;
}

static int refuses(uint8_t code, bool shift)
{
    struct kbd_key key;
    memset(&key, 0, sizeof key);
    return !kbd_translate(code, shift, &key);
}

static void test_shift_state(void)
{
    printf("shift is tracked going down and coming back up\n");
    check(kbd_shift_after(0x2A, false), "left shift pressed");
    check(!kbd_shift_after(0xAA, true), "left shift released");
    check(kbd_shift_after(0x36, false), "right shift pressed");
    check(!kbd_shift_after(0xB6, true), "right shift released");

    printf("other keys leave shift alone\n");
    check(kbd_shift_after(0x1E, true), "a letter while shift is held");
    check(!kbd_shift_after(0x1E, false), "a letter while it is not");
    check(kbd_shift_after(0x9E, true), "releasing a letter does not release shift");
    check(!kbd_shift_after(0x1C, false), "enter");
}

static void test_unshifted_keys(void)
{
    printf("letters and digits\n");
    check(gives(0x1E, false, 'A'), "A");
    check(gives(0x32, false, 'M'), "M");
    check(gives(0x0B, false, '0'), "zero, which is not where you would guess");
    check(gives(0x02, false, '1'), "one");
    check(gives(0x0A, false, '9'), "nine");
    check(gives(0x39, false, ' '), "space");

    printf("letters are unaffected by shift, since they are already uppercase\n");
    check(gives(0x1E, true, 'A'), "shifted A is still A");

    printf("named keys\n");
    check(names(0x1C, "ENTER"), "enter");
    check(names(0x01, "ESCAPE"), "escape");
    check(names(0x0E, "BACKSPACE"), "backspace");
    check(names(0x0F, "TAB"), "tab");
}

static void test_arithmetic_keys(void)
{
    printf("the symbols arithmetic needs\n");
    check(gives(0x0C, false, '-'), "minus, from the main row");
    check(gives(0x0D, false, '='), "equals, which evaluates");
    check(gives(0x35, false, '/'), "slash, for division");

    printf("shifted symbols\n");
    check(gives(0x07, true, '^'), "shift and 6 gives a power");
    check(gives(0x09, true, '*'), "shift and 8 gives a multiply");
    check(gives(0x0D, true, '+'), "shift and equals gives a plus");
    /* The comparison keys. Without these, a wrong 0x33 would leave IF a < b
     * with no working less than and nothing in either suite would say so. */
    check(gives(0x33, true, '<'), "shift and comma gives a less than");
    check(gives(0x34, true, '>'), "shift and full stop gives a greater than");
    check(refuses(0x33, false), "comma unshifted is not a key this layout knows");
    check(refuses(0x34, false), "nor is full stop unshifted");

    printf("and the same keys unshifted are still their own characters\n");
    check(gives(0x07, false, '6'), "6 without shift");
    check(gives(0x09, false, '8'), "8 without shift");

    printf("the keypad\n");
    check(gives(0x4E, false, '+'), "keypad plus");
    check(gives(0x4A, false, '-'), "keypad minus");
    check(gives(0x37, false, '*'), "keypad star");
    check(gives(0x4E, true, '+'), "keypad plus is the same with shift held");
}

static int extended_names(uint8_t code, const char *expected)
{
    struct kbd_key key;
    memset(&key, 0, sizeof key);
    if (!kbd_translate_extended(code, &key)) {
        return 0;
    }
    return key.ch == '\0' && key.name != NULL && strcmp(key.name, expected) == 0;
}

static void test_arrow_keys(void)
{
    printf("the arrows, which arrive after the extended prefix\n");
    check(extended_names(0x48, "UP"), "up");
    check(extended_names(0x50, "DOWN"), "down");
    check(extended_names(0x4B, "LEFT"), "left");
    check(extended_names(0x4D, "RIGHT"), "right");

    printf("arrows are named, never printable, so they cannot land in a sum\n");
    struct kbd_key key;
    memset(&key, 0, sizeof key);
    kbd_translate_extended(0x48, &key);
    check(key.ch == '\0', "up has no character");

    printf("and the rest of the extended set is still dropped\n");
    check(!extended_names(0x1C, "ENTER"), "the extended enter on the keypad");
    check(!extended_names(0x47, "HOME"), "home");
    check(!extended_names(0x53, "DELETE"), "delete");
    check(!kbd_translate_extended(0xC8, &key), "an arrow being released");
    check(!kbd_translate_extended(0x48, NULL), "nowhere to put the answer");

    printf("the same codes without the prefix are their ordinary keys\n");
    /* refuses, not gives with a nul. gives returns 0 for every behaviour
     * kbd_translate could have, so the old form passed whatever happened and
     * would have gone on passing if 0x4B were wired up as a printable key. */
    check(refuses(0x4B, false), "0x4B unprefixed is not a key this layout knows");
    check(refuses(0x48, false), "nor is 0x48");
    check(refuses(0x50, false), "nor is 0x50");
}

static void test_refusals(void)
{
    printf("kbd_translate refuses what it should\n");
    check(refuses(0x9E, false), "a release code");
    check(refuses(0xAA, false), "a shift release");
    check(refuses(0x59, false), "a key this milestone does not decode");
    check(refuses(0x00, false), "a zero byte");

    struct kbd_key key;
    memset(&key, 0, sizeof key);
    check(!kbd_translate(0x1E, false, NULL), "nowhere to put the answer");
    check(key.ch == '\0', "and nothing was written");
}

int main(void)
{
    test_shift_state();
    test_unshifted_keys();
    test_arithmetic_keys();
    test_arrow_keys();
    test_refusals();

    if (failures > 0) {
        printf("\n%d keyboard check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nkeyboard checks passed\n");
    return 0;
}
