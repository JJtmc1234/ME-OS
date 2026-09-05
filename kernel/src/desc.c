/* Descriptor encoding. See kernel/include/desc.h for why this is its own file.
 *
 * The field splits below are the processor's, not a choice. Each one is
 * written out a piece at a time with the shift that piece needs, rather than
 * as one expression, because the whole point of the file is that a reader can
 * check it against the manual line by line.
 */
#include "desc.h"

uint64_t desc_segment(uint8_t access, uint8_t granularity)
{
    /* In 64 bit mode a code or data segment's base and limit are ignored
     * entirely. They are written as the traditional flat values anyway, so
     * that a descriptor dumped from memory looks like every other one and
     * nobody has to wonder whether the zeroes mean something. */
    uint64_t entry = 0;
    entry |= 0xFFFFull;                              /* limit 15:0  */
    entry |= 0ull << 16;                             /* base 15:0   */
    entry |= 0ull << 32;                             /* base 23:16  */
    entry |= (uint64_t)access << 40;
    entry |= (uint64_t)((granularity & 0xF0u) | 0x0Fu) << 48;  /* flags, limit 19:16 */
    entry |= 0ull << 56;                             /* base 31:24  */
    return entry;
}

void desc_tss(uint64_t *low, uint64_t *high, uint64_t base, uint32_t limit)
{
    uint64_t first = 0;
    first |= (uint64_t)(limit & 0xFFFFu);
    first |= (base & 0xFFFFull) << 16;
    first |= ((base >> 16) & 0xFFull) << 32;
    first |= (uint64_t)DESC_TSS << 40;
    first |= (uint64_t)((limit >> 16) & 0x0Fu) << 48;
    first |= ((base >> 24) & 0xFFull) << 56;

    /* The top half of the base address, and nothing else. A system descriptor
     * is sixteen bytes precisely so that the base can be 64 bits wide. */
    *low = first;
    *high = (base >> 32) & 0xFFFFFFFFull;
}

void desc_gate(uint64_t *low, uint64_t *high, uint64_t handler,
               uint16_t selector, uint8_t type, uint8_t ist)
{
    uint64_t first = 0;
    first |= handler & 0xFFFFull;                    /* offset 15:0  */
    first |= (uint64_t)selector << 16;
    first |= (uint64_t)(ist & 0x7u) << 32;           /* stack table index */
    first |= (uint64_t)type << 40;
    first |= ((handler >> 16) & 0xFFFFull) << 48;    /* offset 31:16 */

    *low = first;
    *high = (handler >> 32) & 0xFFFFFFFFull;         /* offset 63:32 */
}

uint64_t desc_gate_handler(uint64_t low, uint64_t high)
{
    return (low & 0xFFFFull)
         | ((low >> 48) & 0xFFFFull) << 16
         | (high & 0xFFFFFFFFull) << 32;
}

uint64_t desc_tss_base(uint64_t low, uint64_t high)
{
    return ((low >> 16) & 0xFFFFull)
         | ((low >> 32) & 0xFFull) << 16
         | ((low >> 56) & 0xFFull) << 24
         | (high & 0xFFFFFFFFull) << 32;
}
