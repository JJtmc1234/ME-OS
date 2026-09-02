/* What processor this is, asked of the processor itself.
 *
 * CPUID is the one thing on x86 that answers questions about the machine
 * without a driver, a bus walk or a table. It is what makes a system
 * information window say something true rather than something written down when
 * the code was compiled. See M19 in docs/milestones.md.
 */
#ifndef ME_CPU_H
#define ME_CPU_H

#include <stdbool.h>
#include <stdint.h>

/* Twelve characters and a terminator, which is exactly what leaf zero returns. */
#define CPU_VENDOR_CAPACITY 13
/* Forty eight characters and a terminator, the size of the brand string. */
#define CPU_BRAND_CAPACITY 49

/* "GenuineIntel", "AuthenticAMD", or whatever this machine says it is. False
 * when CPUID is not available, in which case `out` is left as an empty string
 * rather than filled with something invented. */
bool cpu_vendor(char *out, uint64_t capacity);

/* The marketing name, from the extended leaves. Not every processor has one, and
 * an emulator often does not, so false here is ordinary and not a fault. */
bool cpu_brand(char *out, uint64_t capacity);

/* Pure. Splits the four registers a CPUID leaf returns into characters, in the
 * order the manual gives for that leaf. Exposed so the unpacking can be checked
 * on the development machine, where the answer is known.
 *
 * `order` names which register supplies each group of four characters, using the
 * letters a, b, c and d. Leaf zero is "bdc"; the brand leaves are "abcd".
 */
uint64_t cpu_unpack(char *out, uint64_t capacity, const char *order,
                    uint32_t a, uint32_t b, uint32_t c, uint32_t d);

#endif /* ME_CPU_H */
