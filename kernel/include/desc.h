/* Descriptor encoding: the bit layouts the processor's tables are made of.
 *
 * Two tables matter. The global descriptor table says what a segment selector
 * means, and in 64 bit mode its only real job is to say which privilege level
 * code runs at. The interrupt descriptor table says where to go when something
 * happens, and at what privilege the handler runs.
 *
 * Both are packed structures where a field is split across non adjacent bytes
 * for reasons that made sense in 1982. A handler address in an IDT gate is
 * stored in three pieces. Getting one of those pieces wrong does not produce a
 * wrong answer, it produces a jump to an address nobody chose, which on a
 * fault is a triple fault and a silent reboot.
 *
 * So the encoding lives here, apart from the tables it fills in, and is
 * checked on the development machine where a wrong bit prints a failure.
 *
 * See M31 in docs/milestones.md.
 */
#ifndef ME_DESC_H
#define ME_DESC_H

#include <stdbool.h>
#include <stdint.h>

/* Where each descriptor sits in the table. A selector is the byte offset of
 * the descriptor with the requested privilege level in its low two bits, which
 * is why the user selectors below are the offset plus three. */
#define SEL_KERNEL_CODE 0x08u
#define SEL_KERNEL_DATA 0x10u
#define SEL_USER_DATA   0x1Bu  /* offset 0x18, privilege 3 */
#define SEL_USER_CODE   0x23u  /* offset 0x20, privilege 3 */
#define SEL_TSS         0x28u

/* Access byte values. Present, privilege, and what kind of segment. */
#define DESC_KERNEL_CODE 0x9Au
#define DESC_KERNEL_DATA 0x92u
#define DESC_USER_CODE   0xFAu
#define DESC_USER_DATA   0xF2u
/* A 64 bit task state segment, which is a system descriptor rather than a
 * code or data one, so the S bit is clear. */
#define DESC_TSS         0x89u

/* Granularity byte for a 64 bit code segment: the long mode bit set, and the
 * default operand size bit clear because the two may not both be set. */
#define GRAN_LONG_CODE 0x20u
#define GRAN_DATA      0x00u

/* An interrupt gate. Entering it clears the interrupt flag, so a handler is
 * not itself interrupted before it has saved anything. */
#define GATE_INTERRUPT 0x8Eu
/* The same, reachable from user mode. Only for the system call vector: any
 * other gate a program could enter deliberately is a way into the kernel that
 * nobody designed. */
#define GATE_INTERRUPT_USER 0xEEu

/* One global descriptor table entry. */
uint64_t desc_segment(uint8_t access, uint8_t granularity);

/* A task state segment descriptor, which is sixteen bytes because it holds a
 * full 64 bit base address. Written into two consecutive table slots. */
void desc_tss(uint64_t *low, uint64_t *high, uint64_t base, uint32_t limit);

/* One interrupt descriptor table entry, as its two 64 bit halves. */
void desc_gate(uint64_t *low, uint64_t *high, uint64_t handler,
               uint16_t selector, uint8_t type, uint8_t ist);

/* Reading back what an encoder produced, so a test can say the handler
 * address survived being split into three pieces. */
uint64_t desc_gate_handler(uint64_t low, uint64_t high);
uint64_t desc_tss_base(uint64_t low, uint64_t high);

#endif /* ME_DESC_H */
