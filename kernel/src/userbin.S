/* The two test programs, as bytes inside the kernel image.
 *
 * Only for M32, and deliberately so. This milestone answers whether code can
 * run at privilege three and come back, and embedding the program keeps the
 * filesystem, the loader and the executable format out of the way of that one
 * question. M33 loads a real ELF file off the disk, and the point of that
 * milestone is precisely that the program is no longer in here.
 *
 * `.incbin` rather than a C array of numbers, so what runs is the assembler's
 * output from user/hello.S and nobody has hand copied any bytes.
 *
 * See M32 in docs/milestones.md.
 */
    .section .rodata
    .balign 16

    .globl user_hello
    .globl user_hello_end
user_hello:
    .incbin "build/hello.bin"
user_hello_end:

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
