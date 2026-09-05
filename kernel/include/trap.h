/* Traps: what the machine does when something goes wrong.
 *
 * Up to M30 the kernel ran with no interrupt table at all. That was honest
 * while the only code on the machine was the kernel's own, because a kernel
 * fault is a bug either way and there was nothing useful to do about it. It
 * stops being possible the moment a program runs, because a program is
 * supposed to be able to be wrong. Dividing by zero, reading an address it does
 * not own, or executing rubbish must stop that program and leave the machine
 * running. Without a table the processor cannot find a handler, cannot report
 * that it cannot find one, and resets.
 *
 * So this is the difference between "a broken program" and "a reboot".
 *
 * External interrupts stay masked. Nothing here enables them, because nothing
 * yet needs them: the timer, the keyboard and the mouse are all still polled.
 * Exceptions are not interrupts and arrive whether or not they are masked.
 *
 * See M31 in docs/milestones.md.
 */
#ifndef ME_TRAP_H
#define ME_TRAP_H

#include <stdbool.h>
#include <stdint.h>

/* Everything the processor and the stubs put on the stack, in the order it is
 * in memory. The stub pushes registers in reverse, because the stack grows
 * down, so the last one pushed is the first field here. */
struct trapframe {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    /* Pushed by the stub. The error code is a zero the stub supplied for the
     * vectors where the processor does not push one of its own. */
    uint64_t vector;
    uint64_t error;
    /* Pushed by the processor. rsp and ss are only meaningful when the trap
     * came from user mode, which in long mode is always pushed anyway. */
    uint64_t rip, cs, rflags, rsp, ss;
};

/* The vectors worth naming. The rest are reported by number. */
#define TRAP_DIVIDE          0
#define TRAP_DEBUG           1
#define TRAP_BREAKPOINT      3
#define TRAP_INVALID_OPCODE  6
#define TRAP_DOUBLE_FAULT    8
#define TRAP_INVALID_TSS    10
#define TRAP_SEGMENT        11
#define TRAP_STACK          12
#define TRAP_PROTECTION     13
#define TRAP_PAGE_FAULT     14

/* Builds and loads the interrupt table. */
void trap_init(void);

/* Whether the table has been loaded. */
bool trap_ready(void);

/* How many traps have been taken, and what the last one was. Kept so the boot
 * test can prove a fault was really handled rather than merely not crashing. */
uint64_t trap_count(void);
uint64_t trap_last_vector(void);

/* The words for a vector, for a log line a person has to read at three in the
 * morning. */
const char *trap_name(uint64_t vector);

/* Where a trap from user mode goes.
 *
 * Installed by the process layer at M32. Until something installs one, a trap
 * from user mode is treated the same as one from the kernel, which is to say
 * the machine stops. Returns true when it dealt with the trap and execution
 * should continue, false to fall through to stopping.
 *
 * A function pointer rather than a direct call because the trap layer must not
 * depend on there being a process layer. Traps have to work at M31, before any
 * process exists, or M32 has no way to debug itself. */
typedef bool (*trap_user_handler)(struct trapframe *frame);
void trap_set_user_handler(trap_user_handler handler);

/* Deliberately causes a trap, to prove the table works. Returns whether the
 * machine came back from it. */
bool trap_selfcheck(void);

#endif /* ME_TRAP_H */
