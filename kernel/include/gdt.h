/* The segment table, and the one stack the processor switches to by itself.
 *
 * In 64 bit mode segmentation is almost gone. Base and limit are ignored, and
 * the only thing a code segment still decides is the privilege level the
 * processor runs at. That is exactly the thing this milestone needs, because
 * "user mode" is not a mode the kernel puts the machine into by asking. It is
 * what the processor is in because the code segment selector it is running
 * with says privilege three.
 *
 * The bootloader left a perfectly good table behind, and it is replaced anyway
 * for one reason: it has no user segments in it and no task state segment. A
 * program cannot be entered without the first, and cannot fault without the
 * second, because the processor needs to be told which stack to switch to when
 * it takes a trap from user mode. Without that it would try to push the trap
 * frame onto the program's own stack, which is memory the program controls.
 *
 * See M31 in docs/milestones.md.
 */
#ifndef ME_GDT_H
#define ME_GDT_H

#include <stdbool.h>
#include <stdint.h>

/* Builds the table, loads it, and reloads every segment register so the new
 * entries are the ones in use. */
void gdt_init(void);

bool gdt_ready(void);

/* Sets the stack the processor switches to when a trap arrives from user mode.
 *
 * Called with a different stack for each process at M32. Until then it is the
 * kernel's own, which is correct: with nothing in user mode, no trap can
 * arrive from there. */
void gdt_set_kernel_stack(uint64_t rsp);

/* What the processor says its current code segment is. Used to prove the
 * reload worked, since a table that was built but not loaded looks identical
 * from C. */
uint64_t gdt_current_code_selector(void);

#endif /* ME_GDT_H */
