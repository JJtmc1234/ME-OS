/* The interrupt descriptor table, and what happens when something arrives.
 *
 * See kernel/include/trap.h for why this exists and what it deliberately does
 * not do.
 */
#include "trap.h"

#include <stddef.h>

#include "desc.h"
#include "log.h"

#define IDT_VECTORS 256

/* Two 64 bit halves per gate. An array of pairs rather than a struct so the
 * encoding stays entirely in desc.c, where it is tested. */
static uint64_t idt[IDT_VECTORS][2];

/* What lgdt and lidt actually take: a length and an address, packed with no
 * padding between them. */
struct __attribute__((packed)) table_pointer {
    uint16_t limit;
    uint64_t base;
};

/* Filled in by the assembly, one entry per vector. */
extern void *isr_stub_table[];

static bool ready;
static uint64_t traps_taken;
static uint64_t last_vector = IDT_VECTORS;
static trap_user_handler user_handler;

bool trap_ready(void) { return ready; }
uint64_t trap_count(void) { return traps_taken; }
uint64_t trap_last_vector(void) { return last_vector; }

void trap_set_user_handler(trap_user_handler handler)
{
    user_handler = handler;
}

const char *trap_name(uint64_t vector)
{
    switch (vector) {
    case TRAP_DIVIDE:         return "divide by zero";
    case TRAP_DEBUG:          return "debug";
    case 2:                   return "non maskable interrupt";
    case TRAP_BREAKPOINT:     return "breakpoint";
    case 4:                   return "overflow";
    case 5:                   return "bound range exceeded";
    case TRAP_INVALID_OPCODE: return "invalid opcode";
    case 7:                   return "no maths coprocessor";
    case TRAP_DOUBLE_FAULT:   return "double fault";
    case TRAP_INVALID_TSS:    return "invalid task state segment";
    case TRAP_SEGMENT:        return "segment not present";
    case TRAP_STACK:          return "stack fault";
    case TRAP_PROTECTION:     return "general protection fault";
    case TRAP_PAGE_FAULT:     return "page fault";
    case 16:                  return "maths fault";
    case 17:                  return "alignment check";
    case 18:                  return "machine check";
    case 19:                  return "SIMD floating point";
    default:                  return "unexpected";
    }
}

void trap_init(void)
{
    for (uint64_t i = 0; i < IDT_VECTORS; i++) {
        uint64_t handler = (uint64_t)(uintptr_t)isr_stub_table[i];
        /* Every gate is privilege zero, so nothing in user mode can enter the
         * kernel by asking for a vector. The one exception is the system call
         * gate, and it is opened at M32 when there is something to call. */
        desc_gate(&idt[i][0], &idt[i][1], handler, SEL_KERNEL_CODE,
                  GATE_INTERRUPT, 0);
    }

    struct table_pointer pointer = {
        .limit = (uint16_t)(sizeof idt - 1),
        .base = (uint64_t)(uintptr_t)idt,
    };
    __asm__ volatile ("lidt %0" :: "m"(pointer));

    ready = true;
    log_named_hex("trap: interrupt table at", pointer.base);
    log_named_dec("trap: vectors", IDT_VECTORS);
}

/* Where every stub ends up. Called from trapentry.S with the frame it built. */
void trap_dispatch(struct trapframe *frame);

void trap_dispatch(struct trapframe *frame)
{
    traps_taken++;
    last_vector = frame->vector;

    /* The low two bits of the saved code segment are the privilege the trap
     * came from. Three means a program, and a program being wrong is ordinary
     * and must not stop the machine. */
    bool from_user = (frame->cs & 3u) == 3u;
    if (from_user && user_handler != NULL && user_handler(frame)) {
        return;
    }

    /* A breakpoint is how the self check proves the table works, and it is the
     * one trap that means nothing is wrong. It has already been counted. */
    if (frame->vector == TRAP_BREAKPOINT && !from_user) {
        return;
    }

    log_line("");
    log_str("trap: FAILED, ");
    log_str(trap_name(frame->vector));
    log_str(from_user ? " in a program\n" : " in the kernel\n");
    log_named_dec("trap:   vector", frame->vector);
    log_named_hex("trap:   error code", frame->error);
    log_named_hex("trap:   instruction at", frame->rip);
    log_named_hex("trap:   code segment", frame->cs);
    log_named_hex("trap:   flags", frame->rflags);
    log_named_hex("trap:   stack at", frame->rsp);

    if (frame->vector == TRAP_PAGE_FAULT) {
        /* The address that could not be translated is in CR2, and only there.
         * It is not part of the frame the processor pushed. */
        uint64_t at = 0;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(at));
        log_named_hex("trap:   tried to reach", at);
        /* The error code says why, one bit at a time. */
        log_named_dec("trap:   present", (frame->error & 1u) ? 1u : 0u);
        log_named_dec("trap:   was a write", (frame->error & 2u) ? 1u : 0u);
        log_named_dec("trap:   from user mode", (frame->error & 4u) ? 1u : 0u);
        log_named_dec("trap:   was a fetch", (frame->error & 16u) ? 1u : 0u);
    }

    /* Nothing can usefully continue. Stopping here leaves the log and the
     * screen exactly as they were, which is worth more than a reset. */
    log_line("trap: halted");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

bool trap_selfcheck(void)
{
    if (!ready) {
        log_line("trap: selfcheck skipped, no interrupt table");
        return false;
    }
    uint64_t before = traps_taken;

    /* A breakpoint, because it is the one exception that is asked for rather
     * than suffered. If the table is wrong this does not return, and the
     * absence of the next line is the report. */
    __asm__ volatile ("int3");

    bool came_back = traps_taken == before + 1 && last_vector == TRAP_BREAKPOINT;
    if (came_back) {
        log_line("trap: selfcheck passed, a fault was taken and returned from");
    } else {
        log_line("trap: SELFCHECK FAILED");
        log_named_dec("trap:   traps before", before);
        log_named_dec("trap:   traps after", traps_taken);
    }
    return came_back;
}
