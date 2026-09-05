/* Where the processor arrives when something goes wrong.
 *
 * This has to be assembly. A handler is entered with the processor's own frame
 * already on the stack and has to leave with every register exactly as it
 * found it, and a compiler is free to use any register it likes for its own
 * purposes on the way. There is no way to write that in C.
 *
 * The processor pushes an error code for some vectors and not for others,
 * which would make the stack a different shape depending on what happened. So
 * the stubs that do not get one push a zero in its place, and every handler
 * below this point sees one layout.
 *
 * See M31 in docs/milestones.md.
 */
    .code64
    .section .text

/* One stub per vector. The only difference between them is the number, and
 * whether a zero has to be pushed where the error code would have been. */
    .altmacro
    .macro ISR_STUB num
isr_\num:
    .if !(\num == 8 || (\num >= 10 && \num <= 14) || \num == 17 || \num == 21 || \num == 29 || \num == 30)
    pushq $0
    .endif
    pushq $\num
    jmp trap_common
    .endm

    .set vector, 0
    .rept 256
    ISR_STUB %vector
    .set vector, vector + 1
    .endr

/* The common half: save everything, hand C a pointer to it, put it all back.
 *
 * The push order is the reverse of the field order in `struct trapframe`,
 * because the stack grows downwards. The last thing pushed is at the lowest
 * address and so is the first field. */
trap_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* The C calling convention requires the direction flag clear, and an
     * interrupt can arrive at a moment when something had set it. */
    cld

    /* The frame that was just built is the argument. */
    movq %rsp, %rdi
    call trap_dispatch

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    /* Step over the vector number and the error code, which the processor did
     * not put there and will not take back. */
    addq $16, %rsp
    iretq

/* The addresses of all 256 stubs, so the C side can fill in the table without
 * naming each one. */
    .section .rodata
    .globl isr_stub_table
    .balign 8
isr_stub_table:
    .macro ISR_ADDR num
    .quad isr_\num
    .endm
    .set vector, 0
    .rept 256
    ISR_ADDR %vector
    .set vector, vector + 1
    .endr

/* Nothing here needs an executable stack, and saying so keeps the linker from
 * warning about it. */
    .section .note.GNU-stack,"",@progbits
