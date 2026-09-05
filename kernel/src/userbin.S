/* The two broken programs, as bytes inside the kernel image.
 *
 * These are fixtures, not programs. One reads a null pointer and one reads the
 * kernel, and both exist to prove that doing so costs the program and nothing
 * else. Nobody would put either on a disk, so they stay here.
 *
 * The working program used to be here too and is not any more. M33 reads it
 * off the filesystem, having been given it by the bootloader as a file of its
 * own, and keeping a second copy inside the kernel would undercut exactly the
 * claim that milestone makes.
 *
 * `.incbin` rather than a C array of numbers, so what runs is the assembler's
 * output from user/hello.S and nobody has hand copied any bytes.
 *
 * See M32 in docs/milestones.md.
 */
    .section .rodata
    .balign 16

    .globl user_peek
    .globl user_peek_end
user_peek:
    .incbin "build/peek.bin"
user_peek_end:

    .balign 16
    .globl user_fault
    .globl user_fault_end
user_fault:
    .incbin "build/fault.bin"
user_fault_end:

    .section .note.GNU-stack,"",@progbits
