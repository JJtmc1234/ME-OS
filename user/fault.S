/* A program that is wrong on purpose.
 *
 * It reads address zero, which is mapped in no address space and never will
 * be, so the processor raises a page fault. That is the whole program.
 *
 * It exists because the interesting half of running untrusted code is not that
 * a correct program works. It is that an incorrect one costs only itself. If
 * this takes the machine down, then privilege separation is decoration.
 *
 * See M32 in docs/milestones.md.
 */
    .code64
    .section .text
    .globl _start

_start:
    /* A null pointer, which is why nothing is ever mapped at zero. */
    movq (%rax), %rax

    /* Never reached. */
.Lstop:
    jmp .Lstop

    .section .note.GNU-stack,"",@progbits
