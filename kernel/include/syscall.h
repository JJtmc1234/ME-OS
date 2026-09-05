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

/* rax holds the number. The arguments are in rdi, rsi, rdx, r10, r8 and r9.
 *
 * The first three are the front of the ordinary calling convention, so a
 * program written in C needs no shuffling to make a call with up to three
 * arguments. The fourth is r10 rather than rcx, which is where the C
 * convention would put it, and that is deliberate: the `syscall` instruction
 * destroys rcx on entry, so a kernel that read the fourth argument from rcx
 * would have to change its whole ABI the day it moved off `int 0x80`. Linux
 * chose r10 for exactly this reason and this follows it, which costs a two
 * instruction shuffle in the user side wrapper and nothing else.
 *
 * Six because that is what the window calls need and the registers run out
 * there anyway. A call wanting more takes a pointer to a structure. */
#define SYS_ARG_MAX 6

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_GETPID 2

/* The window calls, added at M34.
 *
 * A program gets one window. Not because more is hard, but because one is
 * enough to answer whether a program can draw at all, and a limit that is
 * obviously too small is easier to raise later than a limit that is nearly
 * right is to reason about now. */
#define SYS_WIN_OPEN  10
#define SYS_WIN_FILL  11
#define SYS_WIN_TEXT  12
#define SYS_WIN_FLUSH 13
#define SYS_WIN_CLOSE 14
/* Reads one input event, or reports that none was waiting. Added at M35. */
#define SYS_WIN_EVENT 16

/* Waits, so a program can be looked at. Takes milliseconds.
 *
 * There is no scheduler, so the shell is stopped inside the program while it
 * runs and nothing else on the machine moves. That makes this honest rather
 * than useful: it is how a program stays on the screen long enough to be seen,
 * and it will become a real wait once there is something else to run.
 *
 * Milliseconds rather than the timer's own counts, which is what the first
 * version took and was wrong in a way that looked right. The counter runs at
 * 1.19 MHz, so a program asking to wait "36" got thirty microseconds and the
 * window it drew was gone before the screenshot. A unit a person can reason
 * about is worth the multiply. */
#define SYS_HOLD 15

/* Five seconds. A program that asked to wait forever would be a program that
 * stopped the machine, because with interrupts off nothing can interrupt a
 * wait. */
#define SYS_HOLD_MAX_MS 5000

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
/* Named keys, above every character so a program can tell them apart with one
 * comparison. */
#define SYS_KEY_ESCAPE    0x100u
#define SYS_KEY_ENTER     0x101u
#define SYS_KEY_BACKSPACE 0x102u
#define SYS_KEY_TAB       0x103u
#define SYS_KEY_UP        0x104u
#define SYS_KEY_DOWN      0x105u
#define SYS_KEY_LEFT      0x106u
#define SYS_KEY_RIGHT     0x107u
#define SYS_KEY_PAGEUP    0x108u
#define SYS_KEY_PAGEDOWN  0x109u

/* No window, or not this program's window. */
#define SYS_ENOWINDOW (-5)
/* A size or a coordinate that is not usable. */
#define SYS_EBADSIZE  (-6)
/* The desktop has no room for another window. */
#define SYS_ENOROOM   (-7)

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
