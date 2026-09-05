/* Host tests for the M31 descriptor encoding.
 *
 * A handler address in an interrupt gate is stored in three separate pieces,
 * and a task state segment base in four. Getting a piece wrong does not
 * produce a wrong answer that something notices. It produces a jump to an
 * address nobody chose, at the exact moment something has already gone wrong
 * enough to cause a fault, which is a triple fault and a silent reboot.
 *
 * So every split field here is written and then read back.
 */
#include <stdio.h>

#include "desc.h"

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

static uint8_t byte_of(uint64_t entry, int index)
{
    return (uint8_t)((entry >> (index * 8)) & 0xFF);
}

int main(void)
{
    printf("a segment descriptor puts the access byte where the processor looks\n");
    uint64_t code = desc_segment(DESC_KERNEL_CODE, GRAN_LONG_CODE);
    check(byte_of(code, 5) == DESC_KERNEL_CODE, "the access byte is byte five");
    check((byte_of(code, 6) & 0x20u) != 0, "the long mode bit is set on code");
    check((byte_of(code, 6) & 0x40u) == 0,
          "and the default operand size bit is not, because both is not allowed");

    uint64_t data = desc_segment(DESC_KERNEL_DATA, GRAN_DATA);
    check(byte_of(data, 5) == DESC_KERNEL_DATA, "a data segment carries its own access byte");
    check((byte_of(data, 6) & 0x20u) == 0, "and is not marked as long mode code");

    printf("privilege is what actually separates the four segments\n");
    check((DESC_KERNEL_CODE & 0x60u) == 0x00u, "kernel code is privilege zero");
    check((DESC_KERNEL_DATA & 0x60u) == 0x00u, "kernel data is privilege zero");
    check((DESC_USER_CODE & 0x60u) == 0x60u, "user code is privilege three");
    check((DESC_USER_DATA & 0x60u) == 0x60u, "user data is privilege three");
    check((DESC_KERNEL_CODE & 0x80u) != 0, "and every one of them is present");

    printf("a selector is an offset with the privilege in its low two bits\n");
    check((SEL_KERNEL_CODE & 3u) == 0, "the kernel asks at privilege zero");
    check((SEL_USER_CODE & 3u) == 3u, "a program asks at privilege three");
    check((SEL_USER_CODE & ~7u) == 0x20u, "and user code is the fifth descriptor");
    check((SEL_USER_DATA & ~7u) == 0x18u, "with user data just before it");
    check(SEL_TSS == 0x28u, "the task state segment comes after both");

    printf("a gate keeps the whole handler address across its three pieces\n");
    /* A kernel address, which is what makes this worth testing: the top half
     * is not zero, so a gate that only stored the low 32 bits would jump into
     * the bottom of the address space. */
    const uint64_t handler = 0xFFFFFFFF801234ABull;
    uint64_t low = 0;
    uint64_t high = 0;
    desc_gate(&low, &high, handler, SEL_KERNEL_CODE, GATE_INTERRUPT, 0);
    check(desc_gate_handler(low, high) == handler, "the address comes back whole");
    check((uint16_t)(low >> 16) == SEL_KERNEL_CODE, "the selector is where it belongs");
    check(byte_of(low, 5) == GATE_INTERRUPT, "and so is the type");
    check(((low >> 32) & 0x7u) == 0, "no separate stack was asked for");

    printf("addresses at the edges survive too\n");
    desc_gate(&low, &high, 0, SEL_KERNEL_CODE, GATE_INTERRUPT, 0);
    check(desc_gate_handler(low, high) == 0, "an address of zero comes back as zero");
    desc_gate(&low, &high, 0xFFFFFFFFFFFFFFFFull, SEL_KERNEL_CODE, GATE_INTERRUPT, 0);
    check(desc_gate_handler(low, high) == 0xFFFFFFFFFFFFFFFFull, "and every bit set comes back");
    desc_gate(&low, &high, 0x000000000000FFFFull, SEL_KERNEL_CODE, GATE_INTERRUPT, 0);
    check(desc_gate_handler(low, high) == 0xFFFFull, "as does one that ends on a piece boundary");
    desc_gate(&low, &high, 0x0000000000010000ull, SEL_KERNEL_CODE, GATE_INTERRUPT, 0);
    check(desc_gate_handler(low, high) == 0x10000ull, "and one that starts on the next piece");

    printf("a separate stack can be asked for, and only three bits of it\n");
    desc_gate(&low, &high, handler, SEL_KERNEL_CODE, GATE_INTERRUPT, 1);
    check(((low >> 32) & 0x7u) == 1, "stack one is recorded");
    check(desc_gate_handler(low, high) == handler, "and the address is still whole");
    desc_gate(&low, &high, handler, SEL_KERNEL_CODE, GATE_INTERRUPT, 0xFF);
    check(((low >> 32) & 0x7u) == 7, "an out of range index is masked, not spilled");
    check(byte_of(low, 5) == GATE_INTERRUPT, "so it cannot reach the type byte");

    printf("only the system call gate is reachable from a program\n");
    check((GATE_INTERRUPT & 0x60u) == 0x00u,
          "an ordinary gate cannot be entered from user mode");
    check((GATE_INTERRUPT_USER & 0x60u) == 0x60u, "the system call gate can");
    check((GATE_INTERRUPT_USER & 0x0Fu) == 0x0Eu,
          "and is still an interrupt gate, so it arrives with interrupts off");
    check((GATE_INTERRUPT & 0x0Fu) == 0x0Eu, "as is every other one");

    printf("a task state segment descriptor keeps a 64 bit base\n");
    const uint64_t tss_at = 0xFFFF8000DEADBEEFull;
    desc_tss(&low, &high, tss_at, 0x67);
    check(desc_tss_base(low, high) == tss_at, "the base comes back whole");
    check((low & 0xFFFFull) == 0x67, "the limit is where it belongs");
    check(byte_of(low, 5) == DESC_TSS, "it is marked as a task state segment");
    check((DESC_TSS & 0x10u) == 0, "which is a system descriptor, not a code or data one");
    check((DESC_TSS & 0x80u) != 0, "and it is present");

    printf("a base at the edges survives its four pieces\n");
    desc_tss(&low, &high, 0, 0x67);
    check(desc_tss_base(low, high) == 0, "zero comes back as zero");
    desc_tss(&low, &high, 0xFFFFFFFFFFFFFFFFull, 0xFFFFF);
    check(desc_tss_base(low, high) == 0xFFFFFFFFFFFFFFFFull, "every bit set comes back");
    check((low & 0xFFFFull) == 0xFFFFu, "the low limit bits are kept");
    check(((low >> 48) & 0x0Full) == 0x0Full, "and so are the high ones");

    printf(failures == 0 ? "\ndescriptor encoding checks passed\n"
                         : "\ndescriptor encoding checks FAILED\n");
    return failures == 0 ? 0 : 1;
}
