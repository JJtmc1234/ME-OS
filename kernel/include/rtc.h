/* The real time clock, read from the CMOS.
 *
 * The machine has known the date since it was switched on and nothing here has
 * ever asked it. Uptime is a true answer to a different question: a desktop
 * that cannot say what time it is is a desktop nobody would leave open.
 *
 * See M21 in docs/milestones.md.
 */
#ifndef ME_RTC_H
#define ME_RTC_H

#include <stdbool.h>
#include <stdint.h>

struct rtc_time {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

/* Reads the clock. False when the chip is not answering, in which case `out` is
 * zeroed rather than filled with a plausible date. A wrong clock is worse than a
 * missing one, because nothing downstream can tell it is wrong. */
bool rtc_read(struct rtc_time *out);

/* Pure. Turns one CMOS register into a number.
 *
 * The chip reports either binary or binary coded decimal, where each nibble is
 * one digit, so 0x59 means 59 rather than 89. Which of the two it uses is in a
 * status register, and getting it backwards gives a clock that is right for the
 * first ten minutes of every hour. Exposed so that can be checked. */
uint8_t rtc_decode(uint8_t value, bool binary);

/* Pure. Turns a twelve hour reading into a twenty four hour one. The chip sets
 * the top bit of the hour for the afternoon, and midnight is twelve, not zero. */
uint8_t rtc_to_24_hour(uint8_t hour, bool already_24_hour);

/* Writes the time as HH:MM:SS, and the date as YYYY-MM-DD, in a way anybody
 * reads the same way round. */
uint64_t rtc_format_time(const struct rtc_time *time, char *out, uint64_t capacity);
uint64_t rtc_format_date(const struct rtc_time *time, char *out, uint64_t capacity);

#endif /* ME_RTC_H */
