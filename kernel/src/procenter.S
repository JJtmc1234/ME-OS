/* Going down into user mode, and coming back up.
 *
 * These two are one pair. `proc_enter_user` saves the kernel's state on its own
 * stack, remembers where that stack was, and drops to privilege three.
 * `proc_leave_user` puts the stack pointer back and returns, which returns from
 * `proc_enter_user` even though it was never reached by returning through it.
 *
 * That is the whole trick, and it is the same one a coroutine uses. The kernel
 * side of a program is a function call that has not finished yet.
 *
 * There is no instruction that lowers privilege. `iretq` is used instead,
 * because returning from an interrupt restores whatever privilege the saved
 * code segment names, and nothing checks that the processor was ever there. So
 * a frame is built by hand that describes a place the machine has never been,
 * and returned to.
 *
 * See M32 in docs/milestones.md.
 */
    .code64
    .section .text

/* Offsets into struct process. The static assertions in process.c fail the
 * build if a field is ever added above these and moves them. */
    .set PROC_KERNEL_RSP, 0
    .set PROC_ENTRY,      8
    .set PROC_USER_STACK, 16

/* Selectors, with the low two bits saying privilege three. Kept in step with
 * kernel/include/desc.h by the same static assertions. */
    .set SEL_USER_CODE, 0x23
    .set SEL_USER_DATA, 0x1B

/* Bit 1 is reserved and always set. The interrupt flag is deliberately clear:
 * see the milestone notes. A program runs with interrupts off because nothing
 * has enabled them yet, which is also why nothing can preempt one. */
    .set USER_FLAGS, 0x2

    .globl proc_enter_user
/* void proc_enter_user(struct process *proc), in rdi. Returns when the process
 * has gone, by way of proc_leave_user. */
proc_enter_user:
    /* Everything the C calling convention says a function must preserve. The
     * return address is already on the stack above these. */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, PROC_KERNEL_RSP(%rdi)

    /* The frame iretq expects, pushed in the order it pops: stack segment,
     * stack pointer, flags, code segment, instruction pointer. */
    pushq $SEL_USER_DATA
    pushq PROC_USER_STACK(%rdi)
    pushq $USER_FLAGS
    pushq $SEL_USER_CODE
    pushq PROC_ENTRY(%rdi)

    /* Every register cleared before the drop.
     *
     * Whatever is left in one is visible to the program, and at this moment
     * they hold kernel addresses: the pointer in rdi is where this process
     * structure lives in the kernel's half of memory. A program cannot read
     * that address, but being told it is a gift to somebody trying to work out
     * where anything is. */
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %r10d, %r10d
    xorl %r11d, %r11d
    xorl %r12d, %r12d
    xorl %r13d, %r13d
    xorl %r14d, %r14d
    xorl %r15d, %r15d

    iretq

    .globl proc_leave_user
/* void proc_leave_user(struct process *proc), in rdi. Never returns to its
 * caller. Returns from proc_enter_user instead.
 *
 * Called from inside the trap handler, which is running on the trap stack the
 * task state segment named. That stack is simply abandoned: nothing on it
 * needs unwinding, and the next trap will start at its top again. */
proc_leave_user:
    movq PROC_KERNEL_RSP(%rdi), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

    .section .note.GNU-stack,"",@progbits
