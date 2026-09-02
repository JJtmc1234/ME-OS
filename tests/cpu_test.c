/* Host tests for the M19 processor identification.
 *
 * CPUID itself is not run here. A development machine is not necessarily the
 * machine the kernel boots on, and the part that can be wrong is not the
 * instruction, it is the unpacking: which register supplies which four
 * characters, and in what order the bytes come out of each one. Those are
 * written down in the manual, so they can be checked against known values.
 */
#include <stdio.h>
#include <string.h>

#include "cpu.h"

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
    char out[CPU_BRAND_CAPACITY];

    printf("leaf zero spells the vendor from EBX, EDX and ECX in that order\n");
    /* The values a real Intel processor returns for leaf zero. */
    check(cpu_unpack(out, sizeof out, "bdc",
                     0, 0x756E6547u, 0x6C65746Eu, 0x49656E69u) == 12,
          "twelve characters");
    check(strcmp(out, "GenuineIntel") == 0, "and they spell GenuineIntel");

    /* And what an AMD one returns, which uses the same three registers in the
     * same order and would come out as nonsense under any other. */
    check(cpu_unpack(out, sizeof out, "bdc",
                     0, 0x68747541u, 0x444D4163u, 0x69746E65u) == 12,
          "twelve characters again");
    check(strcmp(out, "AuthenticAMD") == 0, "spelling AuthenticAMD");

    printf("the register order matters, so a wrong one is visibly wrong\n");
    check(cpu_unpack(out, sizeof out, "bcd",
                     0, 0x756E6547u, 0x6C65746Eu, 0x49656E69u) == 12,
          "the same registers in the wrong order still fill the buffer");
    check(strcmp(out, "GenuineIntel") != 0, "and do not spell the vendor");

    printf("the brand leaves run straight through EAX, EBX, ECX and EDX\n");
    check(cpu_unpack(out, sizeof out, "abcd",
                     0x20202020u, 0x20202020u, 0x20202020u, 0x6C65746Eu) == 16,
          "sixteen characters from one leaf");
    check(strcmp(out, "            ntel") == 0, "in the order the manual gives");

    printf("a buffer too small is filled and terminated rather than overrun\n");
    char small[5];
    check(cpu_unpack(small, sizeof small, "bdc",
                     0, 0x756E6547u, 0x6C65746Eu, 0x49656E69u) == 4,
          "only what fits is written");
    check(strcmp(small, "Genu") == 0, "and it is the beginning of the answer");
    check(cpu_unpack(out, 1, "bdc", 0, 1, 2, 3) == 0, "a one byte buffer holds none");
    check(out[0] == '\0', "and is still terminated");

    printf("nonsense is refused rather than written somewhere\n");
    check(cpu_unpack(NULL, sizeof out, "bdc", 0, 1, 2, 3) == 0, "nowhere to write");
    check(cpu_unpack(out, 0, "bdc", 0, 1, 2, 3) == 0, "no capacity");
    check(cpu_unpack(out, sizeof out, NULL, 0, 1, 2, 3) == 0, "no order");
    /* A letter naming no register would silently drop four characters out of
     * the middle of the answer, which would read as a real vendor string. */
    check(cpu_unpack(out, sizeof out, "bzc", 0, 0x756E6547u, 0, 0) == 4,
          "an unknown register stops rather than skipping");
    check(strcmp(out, "Genu") == 0, "leaving only what was certain");

    printf("with no CPUID the answer is empty rather than invented\n");
    check(!cpu_vendor(out, sizeof out), "the vendor is not claimed");
    check(out[0] == '\0', "and nothing was left in the buffer");
    check(!cpu_brand(out, sizeof out), "nor is the brand");
    check(!cpu_vendor(NULL, 10), "no buffer is refused");
    check(!cpu_brand(out, 0), "and so is no capacity");

    if (failures > 0) {
        printf("\n%d processor check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nprocessor identification checks passed\n");
    return 0;
}
