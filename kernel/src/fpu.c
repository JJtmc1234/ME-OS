#include "fpu.h"

#include <stdint.h>

/* Feature bits in CPUID leaf 1, EDX. */
#define CPUID_EDX_SSE  (1u << 25)
#define CPUID_EDX_SSE2 (1u << 26)
#define CPUID_EDX_FXSR (1u << 24)

/* CR0 */
#define CR0_MP (1u << 1)   /* monitor coprocessor */
#define CR0_EM (1u << 2)   /* emulation: set means no floating point hardware */

/* CR4 */
#define CR4_OSFXSR     (1u << 9)   /* the OS supports fxsave and fxrstor */
#define CR4_OSXMMEXCPT (1u << 10)  /* the OS handles unmasked SSE exceptions */

static bool ready;

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf), "c"(0));
}

static uint64_t read_cr0(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(value));
    return value;
}

static void write_cr0(uint64_t value)
{
    __asm__ volatile ("mov %0, %%cr0" :: "r"(value));
}

static uint64_t read_cr4(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(value));
    return value;
}

static void write_cr4(uint64_t value)
{
    __asm__ volatile ("mov %0, %%cr4" :: "r"(value));
}

bool fpu_init(void)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);

    /* All three are architectural on x86-64. Checking anyway costs four
     * instructions and turns a fault into a message. */
    if ((edx & CPUID_EDX_SSE) == 0 || (edx & CPUID_EDX_SSE2) == 0 ||
        (edx & CPUID_EDX_FXSR) == 0) {
        return false;
    }

    /* Clear EM so floating point instructions run rather than fault, and set
     * MP so the processor pairs the two correctly. */
    uint64_t cr0 = read_cr0();
    cr0 &= ~(uint64_t)CR0_EM;
    cr0 |= CR0_MP;
    write_cr0(cr0);

    /* OSFXSR tells the processor that SSE state may be used at all.
     * OSXMMEXCPT routes SSE exceptions to vector 19 rather than the invalid
     * opcode vector, which will matter once there is an interrupt table to
     * receive them. There is not one yet, so the kernel's job is simply to
     * produce no unmasked exceptions: no division by zero and no square roots
     * of negative numbers reach this code. */
    uint64_t cr4 = read_cr4();
    cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
    write_cr4(cr4);

    ready = true;
    return true;
}

bool fpu_ready(void)
{
    return ready;
}
