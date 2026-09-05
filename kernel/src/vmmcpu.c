/* The four things about address spaces that need a privileged instruction.
 *
 * Kept apart from vmm.c so that the walk, which is where the mistakes are, can
 * run in the host test suite. Nothing in this file has any logic worth testing
 * and none of it can run anywhere but on the real processor.
 *
 * See M30 in docs/milestones.md.
 */
#include "vmm.h"

#include <stddef.h>

#include "cpu.h"

/* Extended feature enable register, where the no-execute bit is switched on. */
#define MSR_EFER      0xC0000080u
#define EFER_NXE      (1ull << 11)

static bool nx_ready;

static uint64_t read_msr(uint32_t msr)
{
    uint32_t low = 0;
    uint32_t high = 0;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value)
{
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" :: "a"(low), "d"(high), "c"(msr));
}

uint64_t vmm_current_root(void)
{
    uint64_t cr3 = 0;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    /* The low twelve bits of CR3 are cache control flags, not part of the
     * address, and a table address with them still attached is not a table
     * address. */
    return cr3 & PTE_ADDR_MASK;
}

void vmm_activate(const struct addrspace *space)
{
    if (space == NULL || space->root == 0) {
        return;
    }
    /* Loading CR3 replaces every translation the processor had cached, so no
     * separate invalidation is needed here. */
    __asm__ volatile ("mov %0, %%cr3" :: "r"(space->root) : "memory");
}

void vmm_flush(uint64_t virt)
{
    /* One address, rather than reloading CR3 and throwing away every cached
     * translation on the machine to change one page. */
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

bool vmm_enable_nx(void)
{
    /* Asking first. Setting bit 63 of a page table entry without this enabled
     * is a reserved bit violation, which faults on every access to the page
     * rather than being ignored. Every x86-64 processor made has it, and the
     * cost of checking is one CPUID. */
    if (!cpu_has_nx()) {
        nx_ready = false;
        return false;
    }
    uint64_t efer = read_msr(MSR_EFER);
    if ((efer & EFER_NXE) == 0) {
        write_msr(MSR_EFER, efer | EFER_NXE);
    }
    nx_ready = (read_msr(MSR_EFER) & EFER_NXE) != 0;
    return nx_ready;
}

bool vmm_nx_available(void)
{
    return nx_ready;
}
