/* The first program ME OS runs that is not part of ME OS.
 *
 * It writes a line and stops. That is the entire program, and the smallness is
 * the point: everything interesting about it is that it runs at all. It is
 * built separately from the kernel, it does not link against the kernel, it
 * cannot call a kernel function, and it runs at privilege three where nearly
 * every instruction it could use to interfere with anything faults.
 *
 * The only thing it can do to the machine is `int $0x80`, which is the one
 * gate in the whole interrupt table a program is allowed through.
 *
 * Written in assembly rather than C because there is no C library to link
 * against yet. A libc is a real thing to want and a long way off. Twenty
 * instructions do not need one.
 *
 * See M32 in docs/milestones.md.
 */
    .code64
    .section .text
    .globl _start

/* Kept in step with kernel/include/syscall.h. There is no shared header,
 * because a program including a kernel header would be exactly the coupling
 * this milestone exists to prove does not exist. */
    .set SYS_EXIT,  0
    .set SYS_WRITE, 1
    .set STDOUT,    1

_start:
    /* write(STDOUT, message, length) */
    movq $SYS_WRITE, %rax
    movq $STDOUT, %rdi
    leaq message(%rip), %rsi
    movq $message_length, %rdx
    int  $0x80

    /* exit(0). The kernel does not come back from this one. */
    movq $SYS_EXIT, %rax
    xorq %rdi, %rdi
    int  $0x80

    /* Only reached if exit ever returned, which would be a kernel bug. Better
     * to stop here than to carry on into the message and execute it. */
.Lstop:
    jmp .Lstop

message:
    .ascii "HELLO FROM USERSPACE\n"
    .set message_length, . - message

    .section .note.GNU-stack,"",@progbits
