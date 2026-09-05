/* Serving one system call. See kernel/include/syscall.h for the shape of the
 * boundary and why it is deliberately not Linux's yet.
 *
 * The rule for every handler below: the numbers in the frame were chosen by
 * the program and none of them means anything until it has been checked.
 */
#include "syscall.h"

#include <stddef.h>

#include "log.h"
#include "process.h"
#include "uaccess.h"

void proc_leave_user(struct process *proc);

static uint64_t served;
static uint64_t refused;

uint64_t syscall_served(void) { return served; }
uint64_t syscall_refused(void) { return refused; }

const char *syscall_name(uint64_t number)
{
    switch (number) {
    case SYS_EXIT:   return "exit";
    case SYS_WRITE:  return "write";
    case SYS_GETPID: return "getpid";
    default:         return "unknown";
    }
}

/* write(fd, text, bytes)
 *
 * The output goes to the process's sink, which is the shell's own. So a
 * program's output is redirected into a file and piped into another command by
 * exactly the machinery every built in command already uses, and none of that
 * had to be taught about programs. */
static int64_t do_write(struct process *proc, struct trapframe *frame)
{
    uint64_t fd = frame->rdi;
    uint64_t at = frame->rsi;
    uint64_t bytes = frame->rdx;

    if (fd != SYS_STDOUT) {
        return SYS_EBADFD;
    }
    if (bytes > SYS_WRITE_MAX) {
        /* Refused rather than shortened. A write the program did not ask to be
         * partial, served partially, is missing output somewhere else. */
        return SYS_ETOOBIG;
    }
    if (bytes == 0) {
        return 0;
    }

    /* Copied into the kernel before any of it is used, and refused entirely if
     * any page of the range is not the program's to read. A handler that
     * printed straight from the program's address would be printing whatever
     * address it was handed, including the kernel's own. */
    static char text[SYS_WRITE_MAX + 1];
    if (!uaccess_copy_in(&proc->space, text, at, bytes)) {
        return SYS_EFAULT;
    }
    text[bytes] = '\0';

    if (proc->out != NULL) {
        cmd_print(proc->out, text);
    }
    proc->bytes_written += bytes;
    return (int64_t)bytes;
}

/* exit(code). Does not return. */
static void do_exit(struct process *proc, struct trapframe *frame)
{
    /* The code is whatever the program said, kept as it was given. Nothing
     * here interprets it: the shell decides what a number means. */
    proc->exit_code = (int64_t)frame->rdi;
    proc->state = PROC_EXITED;
    proc->faulted = false;

    log_str("process: ");
    log_str(proc->name);
    log_str(" exited\n");
    log_named_dec("process:   code", (uint64_t)proc->exit_code);
    log_named_dec("process:   system calls", proc->syscalls);
    log_named_dec("process:   bytes written", proc->bytes_written);

    /* Leaves through the kernel stack saved when the program was entered, so
     * this returns from process_run rather than from here. */
    proc_leave_user(proc);
}

int64_t syscall_dispatch(struct process *proc, struct trapframe *frame)
{
    if (proc == NULL || frame == NULL) {
        refused++;
        return SYS_EBADCALL;
    }

    switch (frame->rax) {
    case SYS_EXIT:
        served++;
        do_exit(proc, frame);
        return SYS_OK;  /* not reached */

    case SYS_WRITE: {
        int64_t result = do_write(proc, frame);
        if (result < 0) {
            refused++;
        } else {
            served++;
        }
        return result;
    }

    case SYS_GETPID:
        served++;
        return (int64_t)proc->pid;

    default:
        /* An unknown number is refused and the program carries on, so it can
         * find out that the call is not there. Stopping it would make every
         * future call this kernel has not implemented yet a crash rather than
         * an answer. */
        refused++;
        return SYS_EBADCALL;
    }
}
