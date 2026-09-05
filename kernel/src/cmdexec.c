/* Running a program file, and listing what has run.
 * See kernel/include/cmdexec.h for how a program is told from a script.
 */
#include "cmdexec.h"

#include "cmdout.h"
#include "elfload.h"
#include "log.h"
#include "process.h"

/* One page at the top of the program's address space. A program that wants
 * more than four kilobytes of stack does not get it yet, and would fault
 * rather than quietly writing over whatever is below. */
static bool give_stack(struct process *proc)
{
    uint64_t flags = PTE_WRITE | (vmm_nx_available() ? PTE_NX : 0);
    if (!process_add_page(proc, USER_STACK_TOP - PAGE_SIZE, flags, NULL, 0)) {
        return false;
    }
    proc->user_stack = USER_STACK_TOP;
    return true;
}

void cmdexec_program(struct cmd_context *context, const char *name,
                     const uint8_t *file, uint64_t bytes)
{
    struct process *proc = process_create(name, context->out);
    if (proc == NULL) {
        cmd_println(context->out, "THERE IS NO ROOM TO START ANOTHER PROGRAM");
        return;
    }

    enum elf_result loaded = elfload(proc, file, bytes);
    if (loaded != ELF_OK) {
        cmd_print(context->out, name);
        cmd_print(context->out, ": ");
        cmd_println(context->out, elf_result_text(loaded));
        process_destroy(proc);
        return;
    }
    if (!give_stack(proc)) {
        cmd_println(context->out, "THERE IS NOT ENOUGH MEMORY TO GIVE IT A STACK");
        process_destroy(proc);
        return;
    }

    log_str("exec: running ");
    log_str(name);
    log_str("\n");

    bool clean = process_run(proc);
    int64_t code = proc->exit_code;
    bool faulted = proc->faulted;
    uint64_t vector = proc->fault_vector;
    uint64_t at = proc->fault_address;

    /* A program that wrote nothing and ended without a newline would otherwise
     * leave the next prompt on the same line as its output. */
    if (proc->bytes_written > 0) {
        cmd_newline(context->out);
    }

    if (faulted) {
        cmd_print(context->out, name);
        cmd_print(context->out, " STOPPED: ");
        cmd_print(context->out, trap_name(vector));
        cmd_print(context->out, " AT ");
        cmd_print_number(context->out, at);
        cmd_newline(context->out);
    } else if (code != 0) {
        cmd_print(context->out, name);
        cmd_print(context->out, " ENDED WITH ");
        cmd_print_number(context->out, (uint64_t)code);
        cmd_newline(context->out);
    }
    (void)clean;

    process_destroy(proc);
}

void cmdexec_ps(struct cmd_context *context)
{
    cmd_println(context->out, "PID  STATE    PROGRAM");

    uint64_t shown = 0;
    for (uint64_t i = 0; i < process_capacity(); i++) {
        const struct process *proc = process_at(i);
        if (proc == NULL || proc->state == PROC_FREE) {
            continue;
        }
        cmd_print_padded(context->out, proc->pid, 3);
        cmd_print(context->out, "  ");
        cmd_print(context->out, process_state_text(proc->state));
        cmd_print(context->out, "  ");
        cmd_println(context->out, proc->name);
        shown++;
    }

    /* Honest rather than tidy. There is no scheduler, so a program only exists
     * while the shell is inside it, and the shell cannot be running PS at the
     * same time. An empty list is the correct answer and would look like a
     * broken command without this line. */
    if (shown == 0) {
        cmd_println(context->out, "NOTHING IS RUNNING. A PROGRAM ONLY EXISTS WHILE IT RUNS,");
        cmd_println(context->out, "AND THE SHELL WAITS FOR IT, SO PS CANNOT SEE ONE YET.");
    }
}
