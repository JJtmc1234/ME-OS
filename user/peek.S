/* A program that tries to read the kernel.
 *
 * The kernel is mapped in this program's address space. It has to be: a system
 * call has to land somewhere, and the handler has to exist at the moment the
 * processor arrives. So the pages really are there, and this program really
 * does hold a correct address for them.
 *
 * What it does not have is permission. Those pages are mapped without the user
 * bit, and at privilege three that is the whole difference. The processor
 * refuses the read and raises a page fault, and the error code says the page
 * was present, which is how the fault can be told apart from reading an
 * address where nothing is mapped at all.
 *
 * This is the security claim of M32 stated as a program. If this one prints
 * anything, the isolation is not there.
 *
 * See M32 in docs/milestones.md.
 */
    .code64
    .section .text
    .globl _start

/* The kernel's own base address, from linker.ld. Not a secret, and knowing it
 * is meant to buy nothing. */
    .set KERNEL_AT, 0xFFFFFFFF80000000

_start:
    movq $KERNEL_AT, %rax
    movq (%rax), %rax

    /* Never reached. If it were, the read succeeded and the program would go
     * on to exit cleanly, which is what the check treats as a failure. */
    movq $0, %rax
    xorq %rdi, %rdi
    int  $0x80

.Lstop:
    jmp .Lstop

    .section .note.GNU-stack,"",@progbits
