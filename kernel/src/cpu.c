#include "cpu.h"

#include <stddef.h>

uint64_t cpu_unpack(char *out, uint64_t capacity, const char *order,
                    uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    if (out == NULL || capacity == 0 || order == NULL) {
        return 0;
    }
    uint64_t written = 0;
    for (uint64_t group = 0; order[group] != '\0'; group++) {
        uint32_t value;
        switch (order[group]) {
        case 'a': value = a; break;
        case 'b': value = b; break;
        case 'c': value = c; break;
        case 'd': value = d; break;
        /* A letter that names no register would silently drop four characters
         * out of the middle of the answer, so it stops instead. */
        default: out[written] = '\0'; return written;
        }
        /* Little endian within each register, which is how the manual lays the
         * characters out and not something to guess at. */
        for (int byte = 0; byte < 4; byte++) {
            if (written + 1 >= capacity) {
                out[written] = '\0';
                return written;
            }
            out[written++] = (char)((value >> (byte * 8)) & 0xFF);
        }
    }
    out[written] = '\0';
    return written;
}

#if defined(__x86_64__) && !defined(ME_NO_CPUID)

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile ("cpuid"
                      : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                      : "a"(leaf), "c"(0));
}

bool cpu_vendor(char *out, uint64_t capacity)
{
    if (out == NULL || capacity == 0) {
        return false;
    }
    out[0] = '\0';

    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    if (a == 0 && b == 0 && c == 0 && d == 0) {
        return false;
    }
    return cpu_unpack(out, capacity, "bdc", a, b, c, d) > 0;
}

bool cpu_brand(char *out, uint64_t capacity)
{
    if (out == NULL || capacity == 0) {
        return false;
    }
    out[0] = '\0';

    uint32_t a, b, c, d;
    /* The highest extended leaf this processor supports. Asking for a leaf past
     * it returns whatever the highest one holds, so a machine with no brand
     * string would otherwise report the same text four times. */
    cpuid(0x80000000u, &a, &b, &c, &d);
    if (a < 0x80000004u) {
        return false;
    }

    uint64_t written = 0;
    for (uint32_t leaf = 0x80000002u; leaf <= 0x80000004u; leaf++) {
        cpuid(leaf, &a, &b, &c, &d);
        if (written >= capacity) {
            break;
        }
        written += cpu_unpack(out + written, capacity - written, "abcd", a, b, c, d);
    }

    /* Intel pads the brand with leading spaces. Trimming them here means the
     * window does not have to know that, and every other processor is
     * unaffected because it has none to trim. */
    uint64_t start = 0;
    while (out[start] == ' ') {
        start++;
    }
    if (start > 0) {
        uint64_t i = 0;
        while (out[start + i] != '\0') {
            out[i] = out[start + i];
            i++;
        }
        out[i] = '\0';
    }
    return out[0] != '\0';
}

bool cpu_has_nx(void)
{
    uint32_t a, b, c, d;

    /* The highest extended leaf, first. Asking for a leaf the processor does
     * not implement returns the highest one it does, whose bit 20 means
     * something else entirely. */
    cpuid(0x80000000u, &a, &b, &c, &d);
    if (a < 0x80000001u) {
        return false;
    }
    cpuid(0x80000001u, &a, &b, &c, &d);
    return (d & (1u << 20)) != 0;
}

#else

/* The host tests build this file to check `cpu_unpack`, and a host is not
 * necessarily the machine the kernel runs on. Answering "unknown" is honest;
 * running CPUID on something that may not have it is not. */
bool cpu_vendor(char *out, uint64_t capacity)
{
    if (out != NULL && capacity > 0) {
        out[0] = '\0';
    }
    return false;
}

bool cpu_brand(char *out, uint64_t capacity)
{
    if (out != NULL && capacity > 0) {
        out[0] = '\0';
    }
    return false;
}

bool cpu_has_nx(void)
{
    return false;
}

#endif
