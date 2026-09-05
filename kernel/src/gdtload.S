/* Loading the segment table, which C cannot do on its own.
 *
 * `lgdt` is easy enough from inline assembly. Reloading the code segment is
 * not: there is no instruction that assigns to CS. The only way to change it
 * is to perform a jump that carries a selector, and the only such jump left in
 * 64 bit mode is a far return. So the selector and a return address are pushed
 * by hand and `lretq` is used as a jump to the next line.
 *
 * Until that happens the processor is still running on whichever code
 * descriptor the bootloader left it with. The new table would be loaded and
 * ignored, which looks identical from C and is why gdt.c reads the selector
 * back afterwards.
 *
 * See M31 in docs/milestones.md.
 */
    .code64
    .section .text
    .globl gdt_load

/* void gdt_load(const struct table_pointer *pointer, uint64_t code, uint64_t data)
 *
 * rdi: the table pointer, rsi: the code selector, rdx: the data selector. */
gdt_load:
    lgdt (%rdi)

    /* The far return takes a selector and an address off the stack. */
    pushq %rsi
    leaq  .Lreloaded(%rip), %rax
    pushq %rax
    lretq

.Lreloaded:
    /* The data segment registers. In 64 bit mode these are almost entirely
     * ignored, but they are still checked when they are loaded, and one left
     * pointing at a descriptor in the old table is a fault waiting for the
     * first thing that touches it. */
    movw %dx, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    /* fs and gs are set to null rather than to the data segment. Nothing uses
     * them, and a null selector cannot be the one that quietly worked because
     * an old descriptor happened to still be valid. */
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    ret

    .section .note.GNU-stack,"",@progbits
