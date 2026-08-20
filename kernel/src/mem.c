#include "mem.h"

#include <stdint.h>

void *memset(void *dest, int value, size_t count)
{
    uint8_t *p = (uint8_t *)dest;
    for (size_t i = 0; i < count; i++) {
        p[i] = (uint8_t)value;
    }
    return dest;
}

void *memcpy(void *restrict dest, const void *restrict src, size_t count)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < count; i++) {
        d[i] = s[i];
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t count)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || count == 0) {
        return dest;
    }
    /* Copy backwards when the regions overlap with dest above src. */
    if (d < s) {
        for (size_t i = 0; i < count; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = count; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int memcmp(const void *a, const void *b, size_t count)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;

    for (size_t i = 0; i < count; i++) {
        if (x[i] != y[i]) {
            return x[i] < y[i] ? -1 : 1;
        }
    }
    return 0;
}
