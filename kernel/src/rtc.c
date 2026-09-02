#include "rtc.h"

#include <stddef.h>

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

#define CMOS_SECOND  0x00
#define CMOS_MINUTE  0x02
#define CMOS_HOUR     0x04
#define CMOS_DAY      0x07
#define CMOS_MONTH    0x08
#define CMOS_YEAR     0x09
#define CMOS_STATUS_A 0x0A
#define CMOS_STATUS_B 0x0B

#define STATUS_A_UPDATING 0x80
#define STATUS_B_24_HOUR  0x02
#define STATUS_B_BINARY   0x04
#define HOUR_AFTERNOON    0x80

uint8_t rtc_decode(uint8_t value, bool binary)
{
    if (binary) {
        return value;
    }
    /* Binary coded decimal: each nibble is one digit. Reading 0x59 as 89 is the
     * mistake that gives a clock which is right for the first ten minutes of
     * every hour and wrong for the other fifty. */
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
}

uint8_t rtc_to_24_hour(uint8_t hour, bool already_24_hour)
{
    if (already_24_hour) {
        return hour;
    }
    const bool afternoon = (hour & HOUR_AFTERNOON) != 0;
    uint8_t value = hour & (uint8_t)~HOUR_AFTERNOON;

    /* Twelve in the morning is midnight, which is zero, and twelve in the
     * afternoon is noon, which is twelve. Adding twelve to both would put
     * midnight at noon. */
    if (value == 12) {
        value = 0;
    }
    return afternoon ? (uint8_t)(value + 12) : value;
}

static uint64_t put_two(char *out, uint64_t at, uint64_t capacity, uint8_t value)
{
    if (at + 2 >= capacity) {
        return at;
    }
    out[at++] = (char)('0' + (value / 10) % 10);
    out[at++] = (char)('0' + (value % 10));
    return at;
}

uint64_t rtc_format_time(const struct rtc_time *time, char *out, uint64_t capacity)
{
    if (out == NULL || capacity == 0) {
        return 0;
    }
    out[0] = '\0';
    if (time == NULL) {
        return 0;
    }
    uint64_t at = put_two(out, 0, capacity, time->hour);
    if (at + 1 < capacity) out[at++] = ':';
    at = put_two(out, at, capacity, time->minute);
    if (at + 1 < capacity) out[at++] = ':';
    at = put_two(out, at, capacity, time->second);
    out[at] = '\0';
    return at;
}

uint64_t rtc_format_date(const struct rtc_time *time, char *out, uint64_t capacity)
{
    if (out == NULL || capacity == 0) {
        return 0;
    }
    out[0] = '\0';
    if (time == NULL) {
        return 0;
    }
    /* Year first, then month, then day. It sorts, and it is the one order that
     * is not read differently on different sides of an ocean. */
    uint64_t at = put_two(out, 0, capacity, (uint8_t)(time->year / 100));
    at = put_two(out, at, capacity, (uint8_t)(time->year % 100));
    if (at + 1 < capacity) out[at++] = '-';
    at = put_two(out, at, capacity, time->month);
    if (at + 1 < capacity) out[at++] = '-';
    at = put_two(out, at, capacity, time->day);
    out[at] = '\0';
    return at;
}

#if defined(__x86_64__) && !defined(ME_NO_CMOS)

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

static uint8_t cmos(uint8_t reg)
{
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

static bool updating(void)
{
    return (cmos(CMOS_STATUS_A) & STATUS_A_UPDATING) != 0;
}

static void read_registers(struct rtc_time *out, bool binary, bool hour_24)
{
    out->second = rtc_decode(cmos(CMOS_SECOND), binary);
    out->minute = rtc_decode(cmos(CMOS_MINUTE), binary);

    /* The afternoon bit lives in the raw byte, so it has to be taken off before
     * the value is decoded and put back afterwards. */
    const uint8_t raw_hour = cmos(CMOS_HOUR);
    const bool afternoon = !hour_24 && (raw_hour & HOUR_AFTERNOON) != 0;
    uint8_t hour = rtc_decode(raw_hour & (uint8_t)~HOUR_AFTERNOON, binary);
    out->hour = rtc_to_24_hour(afternoon ? (uint8_t)(hour | HOUR_AFTERNOON) : hour,
                               hour_24);

    out->day = rtc_decode(cmos(CMOS_DAY), binary);
    out->month = rtc_decode(cmos(CMOS_MONTH), binary);
    out->year = (uint16_t)(2000 + rtc_decode(cmos(CMOS_YEAR), binary));
}

bool rtc_read(struct rtc_time *out)
{
    if (out == NULL) {
        return false;
    }
    *out = (struct rtc_time){0};

    /* Bounded, so a chip that always claims to be updating cannot hang the
     * boot. Everything else in this kernel that spins on a port does the same. */
    int spins = 0;
    while (updating() && spins++ < 1000000) {
    }
    if (spins >= 1000000) {
        return false;
    }

    const uint8_t status = cmos(CMOS_STATUS_B);
    const bool binary = (status & STATUS_B_BINARY) != 0;
    const bool hour_24 = (status & STATUS_B_24_HOUR) != 0;

    /* Read twice and only trust a matching pair. The chip can tick between the
     * first register and the last, which would give 10:59:59 the seconds of
     * 11:00:00 and report an hour that never happened. */
    struct rtc_time first;
    for (int attempt = 0; attempt < 8; attempt++) {
        read_registers(&first, binary, hour_24);
        read_registers(out, binary, hour_24);
        if (first.second == out->second && first.minute == out->minute &&
            first.hour == out->hour && first.day == out->day &&
            first.month == out->month && first.year == out->year) {
            break;
        }
    }

    /* A clock that answers with something impossible is a clock that is not
     * there. Saying so beats putting the thirty second of no month on a bar. */
    if (out->month == 0 || out->month > 12 || out->day == 0 || out->day > 31 ||
        out->hour > 23 || out->minute > 59 || out->second > 59) {
        *out = (struct rtc_time){0};
        return false;
    }
    return true;
}

#else

bool rtc_read(struct rtc_time *out)
{
    if (out != NULL) {
        *out = (struct rtc_time){0};
    }
    return false;
}

#endif
