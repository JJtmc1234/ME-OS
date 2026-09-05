/* The segment table and the task state segment.
 * See kernel/include/gdt.h for why the bootloader's table is not enough.
 */
#include "gdt.h"

#include <stddef.h>

#include "desc.h"
#include "log.h"
#include "mem.h"

/* Seven slots: null, kernel code, kernel data, user data, user code, and two
 * for the task state segment, which is sixteen bytes because it carries a full
 * 64 bit address.
 *
 * User data comes before user code deliberately. Nothing at M31 depends on the
 * order, but the `sysret` instruction a later milestone will want derives both
 * selectors from one register and requires exactly this arrangement. Putting
 * them the other way round costs nothing today and would have to be undone. */
#define GDT_SLOTS 7

static uint64_t gdt[GDT_SLOTS];

/* The task state segment. In 64 bit mode almost all of it is unused: the
 * processor no longer switches tasks with it. What is left is a list of stacks
 * to switch to, and that is the whole reason it still exists. */
struct __attribute__((packed)) tss {
    uint32_t reserved0;
    uint64_t rsp[3];      /* one stack per privilege level, 0 to 2 */
    uint64_t reserved1;
    uint64_t ist[7];      /* stacks a gate can name explicitly */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

static struct tss task_state;

/* The kernel's trap stack, used until processes bring their own.
 *
 * Its own array rather than the boot stack, because the boot stack is where
 * the faulting code was running and a fault that happened because that stack
 * was exhausted would then fault again pushing the frame. */
#define TRAP_STACK_BYTES 16384
static uint8_t trap_stack[TRAP_STACK_BYTES] __attribute__((aligned(16)));

struct __attribute__((packed)) table_pointer {
    uint16_t limit;
    uint64_t base;
};

static bool ready;

/* In gdtload.S. Loads the table, then reloads every segment register,
 * including the code segment, which can only be changed by a far return. */
void gdt_load(const struct table_pointer *pointer, uint64_t code, uint64_t data);

bool gdt_ready(void) { return ready; }

void gdt_set_kernel_stack(uint64_t rsp)
{
    task_state.rsp[0] = rsp;
}

uint64_t gdt_current_code_selector(void)
{
    uint64_t selector = 0;
    __asm__ volatile ("mov %%cs, %0" : "=r"(selector));
    return selector;
}

void gdt_init(void)
{
    memset(gdt, 0, sizeof gdt);
    memset(&task_state, 0, sizeof task_state);

    gdt[0] = 0;  /* the null descriptor, which the processor requires */
    gdt[1] = desc_segment(DESC_KERNEL_CODE, GRAN_LONG_CODE);
    gdt[2] = desc_segment(DESC_KERNEL_DATA, GRAN_DATA);
    gdt[3] = desc_segment(DESC_USER_DATA, GRAN_DATA);
    gdt[4] = desc_segment(DESC_USER_CODE, GRAN_LONG_CODE);

    /* The stack grows downwards, so the top of the array is the starting
     * point, and it has to be sixteen byte aligned when the handler is
     * entered. */
    task_state.rsp[0] = (uint64_t)(uintptr_t)(trap_stack + TRAP_STACK_BYTES);
    /* Past the end of the structure, which is how a task state segment says it
     * has no input and output permission map. Without this the processor reads
     * whatever follows in memory as one. */
    task_state.iomap_base = (uint16_t)sizeof task_state;

    desc_tss(&gdt[5], &gdt[6], (uint64_t)(uintptr_t)&task_state,
             (uint32_t)(sizeof task_state - 1));

    struct table_pointer pointer = {
        .limit = (uint16_t)(sizeof gdt - 1),
        .base = (uint64_t)(uintptr_t)gdt,
    };
    gdt_load(&pointer, SEL_KERNEL_CODE, SEL_KERNEL_DATA);

    /* Loading the task register is separate, and has to happen after the table
     * is in place, because it names a descriptor inside it. */
    __asm__ volatile ("ltr %w0" :: "r"(SEL_TSS));

    ready = true;
    log_named_hex("gdt: table at", pointer.base);
    log_named_hex("gdt: code selector now", gdt_current_code_selector());
    log_named_hex("gdt: trap stack top", task_state.rsp[0]);
}
