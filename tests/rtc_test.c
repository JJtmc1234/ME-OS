/* Host tests for the M21 real time clock decoding.
 *
 * The chip is not read here. A development machine's CMOS is not the one the
 * kernel boots on, and the part that can be wrong is not the port read, it is
 * the decoding: whether a register is binary or binary coded decimal, and what
 * the top bit of the hour means. Both are written down in the chip's datasheet,
 * so both can be checked against known values.
 *
 * Getting the first wrong gives a clock that is right for the first ten minutes
 * of every hour. Getting the second wrong puts midnight at noon.
 */
#include <stdio.h>
#include <string.h>

#include "rtc.h"

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

int main(void)
{
    printf("binary coded decimal is two digits, not one number\n");
    check(rtc_decode(0x59, false) == 59, "0x59 is fifty nine");
    check(rtc_decode(0x00, false) == 0, "0x00 is nothing");
    check(rtc_decode(0x09, false) == 9, "0x09 is nine");
    check(rtc_decode(0x23, false) == 23, "and 0x23 is twenty three");
    /* Read as plain binary, 0x59 is 89, which is a minute that does not exist
     * and an hour nobody has. */
    check(rtc_decode(0x59, true) == 0x59, "as binary the same byte is eighty nine");
    check(rtc_decode(30, true) == 30, "and a plain thirty stays thirty");

    printf("twelve hour readings become twenty four hour ones\n");
    check(rtc_to_24_hour(9, false) == 9, "nine in the morning");
    check(rtc_to_24_hour(11, false) == 11, "eleven in the morning");
    check(rtc_to_24_hour(1 | 0x80, false) == 13, "one in the afternoon is thirteen");
    check(rtc_to_24_hour(11 | 0x80, false) == 23, "eleven at night is twenty three");

    /* The two that are not simply plus twelve. Midnight is twelve in the
     * morning and is hour zero; noon is twelve in the afternoon and is twelve. */
    check(rtc_to_24_hour(12, false) == 0, "twelve in the morning is midnight");
    check(rtc_to_24_hour(12 | 0x80, false) == 12, "and twelve in the afternoon is noon");

    printf("a clock already in twenty four hours is left alone\n");
    check(rtc_to_24_hour(0, true) == 0, "midnight");
    check(rtc_to_24_hour(23, true) == 23, "and the last hour of the day");

    printf("the time and the date are written the way they are read\n");
    const struct rtc_time when = {
        .year = 2026, .month = 9, .day = 1,
        .hour = 7, .minute = 5, .second = 3,
    };
    char text[32];
    check(rtc_format_time(&when, text, sizeof text) == 8, "eight characters");
    check(strcmp(text, "07:05:03") == 0, "with a leading zero on each part");
    check(rtc_format_date(&when, text, sizeof text) == 10, "ten characters");
    check(strcmp(text, "2026-09-01") == 0, "year first, so it sorts");

    const struct rtc_time late = {
        .year = 1999, .month = 12, .day = 31,
        .hour = 23, .minute = 59, .second = 59,
    };
    rtc_format_time(&late, text, sizeof text);
    check(strcmp(text, "23:59:59") == 0, "the last second of a day");
    rtc_format_date(&late, text, sizeof text);
    check(strcmp(text, "1999-12-31") == 0, "and the last day of a century");

    printf("nonsense is refused rather than written somewhere\n");
    char tiny[4];
    rtc_format_time(&when, tiny, sizeof tiny);
    check(strlen(tiny) < sizeof tiny, "a short buffer is not overrun");
    check(rtc_format_time(NULL, text, sizeof text) == 0, "no time gives nothing");
    check(text[0] == '\0', "and clears what it was given");
    check(rtc_format_date(&when, NULL, 10) == 0, "nowhere to write gives nothing");

    printf("with no chip to ask, the answer is nothing rather than a date\n");
    struct rtc_time out = { .year = 1234 };
    check(!rtc_read(&out), "the read fails");
    check(out.year == 0, "and leaves nothing plausible behind");
    check(!rtc_read(NULL), "no output is refused");

    if (failures > 0) {
        printf("\n%d clock check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nreal time clock checks passed\n");
    return 0;
}
