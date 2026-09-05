/* The handful of things every program wants and no library provides.
 *
 * Not a C library and not the start of one. A C library is a real thing to
 * want, it is a long way off, and calling this the beginning of one would put
 * the wrong things in it. These are the four or five functions that every
 * program written for ME OS so far has had to write for itself, which is the
 * only reason they are shared.
 *
 * Header only, so a program includes it and links nothing.
 *
 * See user/README.md for what a program may do.
 */
#ifndef ME_USER_UTIL_H
#define ME_USER_UTIL_H

#include "sys.h"

static inline void u_memset(void *at, int value, long count)
{
    unsigned char *p = (unsigned char *)at;
    for (long i = 0; i < count; i++) {
        p[i] = (unsigned char)value;
    }
}

static inline void u_memcpy(void *to, const void *from, long count)
{
    unsigned char *d = (unsigned char *)to;
    const unsigned char *s = (const unsigned char *)from;
    for (long i = 0; i < count; i++) {
        d[i] = s[i];
    }
}

static inline long u_min(long a, long b) { return a < b ? a : b; }
static inline long u_max(long a, long b) { return a > b ? a : b; }
static inline long u_abs(long a) { return a < 0 ? -a : a; }

static inline long u_clamp(long value, long low, long high)
{
    return value < low ? low : (value > high ? high : value);
}

/* A number as text, written into `out`, which needs 21 bytes for the worst
 * case. Returns `out`, so it can be used inside a call.
 *
 * Written out by hand because there is no printf and one is not wanted: a
 * whole formatting language is a lot of program to carry in a machine whose
 * files stop at six kilobytes. */
static inline char *u_itoa(long value, char *out)
{
    char digits[21];
    long n = 0;
    long at = 0;
    unsigned long magnitude;

    if (value < 0) {
        out[at++] = '-';
        /* Negated as unsigned, so the most negative number does not overflow
         * on the way to being printed. */
        magnitude = (unsigned long)(-(value + 1)) + 1u;
    } else {
        magnitude = (unsigned long)value;
    }

    if (magnitude == 0) {
        digits[n++] = '0';
    }
    while (magnitude > 0) {
        digits[n++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    }
    while (n > 0) {
        out[at++] = digits[--n];
    }
    out[at] = '\0';
    return out;
}

/* Joins a label and a number, for a score line. `out` needs the label's length
 * plus 22. */
static inline char *u_label(char *out, const char *label, long value)
{
    long at = 0;
    while (label[at] != '\0') {
        out[at] = label[at];
        at++;
    }
    u_itoa(value, out + at);
    return out;
}

/* Numbers that are hard to predict, from a seed that is hard to predict.
 *
 * xorshift64, which is nine instructions and good enough to place an apple
 * where the player did not expect. Not good enough for anything that matters,
 * and nothing here does.
 *
 * The seed has to come from somewhere the player cannot control. `getpid` is
 * the same every run, so it is mixed with how long the program waited for its
 * first key press, which is a person's reaction time and is never the same
 * twice. A game that seeds from pid alone deals the same first hand forever. */
static unsigned long u_random_state = 0x2545F4914F6CDD1Dul;

static inline void u_seed(unsigned long with)
{
    /* Never zero: xorshift is stuck there and would return zero forever. */
    u_random_state = with != 0 ? with : 0x9E3779B97F4A7C15ul;
}

static inline unsigned long u_random(void)
{
    unsigned long x = u_random_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    u_random_state = x;
    return x;
}

/* A number from 0 to limit - 1. Zero when the limit is not positive, which is
 * a caller's mistake and not worth dividing by. */
static inline long u_random_under(long limit)
{
    if (limit <= 0) {
        return 0;
    }
    return (long)(u_random() % (unsigned long)limit);
}

#endif /* ME_USER_UTIL_H */
