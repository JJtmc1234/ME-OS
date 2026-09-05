/* The system call boundary.
 *
 * This is the only door from a program into the kernel. Everything a program
 * can make the machine do, it does by arriving here with a number in a
 * register, so this is the surface that has to be right.
 *
 * Six calls, and not one more than the first program needs. Every call added
 * before something wants it is a door nobody has thought about.
 *
 * Deliberately ME OS's own numbers, not Linux's. A Linux compatible boundary
 * is a real goal and it is a translation layer that sits above this, so that
 * Linux specific decisions live in one file rather than being scattered
 * through the kernel. Pretending to be Linux now, badly, in a handful of
 * places, would be the version of that which cannot be undone.
 *
 * `int $0x80` rather than the `syscall` instruction, on purpose. `syscall`
 * is faster and is what Linux uses, and it does not switch stacks: it leaves
 * the program's own stack pointer in place and expects the kernel to find a
 * safe one itself, from a register the program could also have written. A
 * software interrupt switches to the stack in the task state segment before
 * running a single instruction of kernel code. The faster path is worth having
 * later, and not before the slow one is known to be correct.
 *
 * See M32 in docs/milestones.md.
 */
#ifndef ME_SYSCALL_H
#define ME_SYSCALL_H

#include <stdint.h>

#include "trap.h"

/* The vector a program raises to get here. Its gate is the only one in the
 * whole table reachable from user mode. */
#define SYSCALL_VECTOR 0x80

/* rax holds the number. rdi, rsi and rdx hold the arguments, which is the
 * front of the ordinary calling convention, so a program written in C needs no
 * shuffling to make one. */
#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_GETPID 2

/* Deliberately no call for the time.
 *
 * There is one, `timer_poll`, and it reports how long has passed since the
 * last time anybody asked, which means asking consumes the answer. The desktop
 * asks it once per frame to move things at a rate rather than at whatever
 * speed the loop happens to run. A program that could also ask would take
 * ticks the desktop never sees, and the visible symptom would be the rectangle
 * slowing down while a program is running. A call for the time needs a counter
 * that accumulates instead, which is a timer interrupt, which is a later
 * milestone. */

/* What a call answers with, in rax. Negative for failure, so a caller can test
 * one sign bit rather than knowing each call's range. */
#define SYS_OK          0
#define SYS_EBADCALL  (-1)
#define SYS_EFAULT    (-2)
#define SYS_EBADFD    (-3)
#define SYS_ETOOBIG   (-4)

/* The only file a program can write to at M32. There is no descriptor table
 * yet, so this is a number that means the terminal rather than an index into
 * anything. */
#define SYS_STDOUT 1

/* How much one write may move. A program asking for more is refused rather
 * than served in pieces, because a partial write it did not ask for is a bug
 * that shows up as missing output somewhere else entirely. */
#define SYS_WRITE_MAX 4096

struct process;

/* Runs one call for a process and returns what goes in rax. Never trusts a
 * number in the frame. */
int64_t syscall_dispatch(struct process *proc, struct trapframe *frame);

/* How many calls have been served, and how many were refused. Kept so the boot
 * test can prove a refusal is a refusal rather than a silent success. */
uint64_t syscall_served(void);
uint64_t syscall_refused(void);

const char *syscall_name(uint64_t number);

#endif /* ME_SYSCALL_H */
