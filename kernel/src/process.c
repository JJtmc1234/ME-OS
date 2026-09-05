/* Processes: creating one, giving it memory, running it, taking it apart.
 * See kernel/include/process.h for what a process is here and what it is not.
 */
#include "process.h"

#include <stddef.h>

#include "desc.h"
#include "gdt.h"
#include "log.h"
#include "mem.h"
#include "pmmboot.h"
#include "syscall.h"
#include "vmmboot.h"
#include "winsys.h"

/* procenter.S reads these three fields by offset, because assembly has no way
 * to know what a C structure looks like. If a field is ever added above them
 * the offsets move silently and the program is entered at whatever the new
 * field happens to contain. These make that a build failure instead. */
_Static_assert(offsetof(struct process, kernel_rsp) == 0, "procenter.S expects this at 0");
_Static_assert(offsetof(struct process, entry) == 8, "procenter.S expects this at 8");
_Static_assert(offsetof(struct process, user_stack) == 16, "procenter.S expects this at 16");
_Static_assert(SEL_USER_CODE == 0x23, "procenter.S has this selector written out");
_Static_assert(SEL_USER_DATA == 0x1B, "procenter.S has this selector written out");

void proc_enter_user(struct process *proc);
void proc_leave_user(struct process *proc);

static struct process table[PROC_MAX];
static uint64_t next_pid = 1;
/* The one running right now, so a trap arriving from user mode knows whose it
 * is. There is no scheduler, so there can only ever be one. */
static struct process *current;

const struct process *process_at(uint64_t index)
{
    return (index < PROC_MAX) ? &table[index] : NULL;
}

uint64_t process_capacity(void) { return PROC_MAX; }

uint64_t process_count(void)
{
    uint64_t used = 0;
    for (uint64_t i = 0; i < PROC_MAX; i++) {
        if (table[i].state != PROC_FREE) {
            used++;
        }
    }
    return used;
}

const char *process_state_text(enum proc_state state)
{
    switch (state) {
    case PROC_FREE:    return "free";
    case PROC_READY:   return "ready";
    case PROC_RUNNING: return "running";
    case PROC_EXITED:  return "exited";
    }
    return "unknown";
}

/* Where a trap from user mode arrives. Installed on the trap layer at boot.
 *
 * Returning true means the program carries on. Anything else ends it, which is
 * the whole point of the milestone: a program may be wrong, and being wrong
 * must cost the program and nothing else. */
static bool on_user_trap(struct trapframe *frame)
{
    if (current == NULL) {
        return false;
    }
    if (frame->vector == SYSCALL_VECTOR) {
        current->syscalls++;
        frame->rax = (uint64_t)syscall_dispatch(current, frame);
        /* A system call that ends the program does not come back here: the
         * exit handler leaves through proc_leave_user. */
        return true;
    }

    current->faulted = true;
    current->fault_vector = frame->vector;
    if (frame->vector == TRAP_PAGE_FAULT) {
        __asm__ volatile ("mov %%cr2, %0" : "=r"(current->fault_address));
    } else {
        current->fault_address = frame->rip;
    }
    current->exit_code = -1;
    current->state = PROC_EXITED;

    log_str("process: ");
    log_str(current->name);
    log_str(" stopped by ");
    log_str(trap_name(frame->vector));
    log_str("\n");
    log_named_hex("process:   at instruction", frame->rip);
    log_named_hex("process:   reaching for", current->fault_address);

    /* Does not return. The kernel resumes where it entered user mode. */
    proc_leave_user(current);
    return false;
}

void process_stop(struct process *proc, int64_t code, const char *why)
{
    if (proc == NULL) {
        return;
    }
    proc->overran = true;
    proc->stopped_because = why;
    proc->exit_code = code;
    proc->state = PROC_EXITED;

    log_str("process: ");
    log_str(proc->name);
    log_str(" stopped: ");
    log_str(why);
    log_str("\n");
    log_named_dec("process:   system calls", proc->syscalls);

    /* Leaves through the kernel stack saved when the program was entered, so
     * this returns from process_run rather than from here. */
    proc_leave_user(proc);
}

void process_init(void)
{
    trap_set_user_handler(on_user_trap);
    log_line("process: user traps are handled, programs may fault safely");
}

struct process *process_create(const char *name, struct cmd_out *out)
{
    struct process *proc = NULL;
    for (uint64_t i = 0; i < PROC_MAX; i++) {
        if (table[i].state == PROC_FREE) {
            proc = &table[i];
            break;
        }
    }
    if (proc == NULL) {
        return NULL;
    }

    memset(proc, 0, sizeof *proc);
    if (vmm_new_user_space(&proc->space) != VMM_OK) {
        return NULL;
    }

    proc->pid = next_pid++;
    proc->state = PROC_READY;
    proc->out = out;
    proc->entry = USER_CODE_AT;
    proc->user_stack = USER_STACK_TOP;

    uint64_t i = 0;
    while (name != NULL && name[i] != '\0' && i < PROC_NAME_MAX - 1) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';
    return proc;
}

bool process_add_page(struct process *proc, uint64_t virt, uint64_t flags,
                      const void *contents, uint64_t bytes)
{
    return process_add_page_at(proc, virt, flags, contents, bytes, 0);
}

bool process_add_page_at(struct process *proc, uint64_t virt, uint64_t flags,
                         const void *contents, uint64_t bytes, uint64_t into)
{
    if (proc == NULL || into > PAGE_SIZE || bytes > PAGE_SIZE - into) {
        return false;
    }

    /* Already there, which happens when two segments of an executable share
     * the page at their ends. Filling the existing one is right: allocating a
     * second would leave whichever mapped last holding a page missing the
     * other's bytes. */
    uint64_t existing = 0;
    if (vmm_translate(&proc->space, virt, &existing, NULL) == VMM_OK) {
        if (contents != NULL && bytes > 0) {
            uint8_t *page = (uint8_t *)phys_to_virt(existing & ~0xFFFull);
            memcpy(page + into, contents, (size_t)bytes);
        }
        return true;
    }

    if (proc->page_count >= PROC_MAX_PAGES) {
        return false;
    }
    uint64_t phys = PMM_NONE;
    uint8_t *page = (uint8_t *)pmm_alloc_zeroed(&phys);
    if (page == NULL) {
        return false;
    }
    /* Zeroed above, so the part of the page the program did not supply reads
     * as zero rather than as whatever the last owner left there. */
    if (contents != NULL && bytes > 0) {
        memcpy(page + into, contents, (size_t)bytes);
    }

    if (vmm_map(&proc->space, virt, phys, flags | PTE_USER) != VMM_OK) {
        pmm_free(pmm_kernel(), phys);
        return false;
    }
    proc->pages[proc->page_count++] = phys;
    return true;
}

bool process_run(struct process *proc)
{
    if (proc == NULL || proc->state != PROC_READY) {
        return false;
    }

    /* The stack a trap from this program arrives on is the one gdt_init set,
     * and it is deliberately not changed here. With no scheduler there is only
     * ever one program running, so one trap stack is enough. The moment a
     * second process can be running while the first is still alive, each will
     * need its own, because a trap taken while another process's frame is
     * still on that stack would land on top of it. */

    struct addrspace kernel_was;
    vmm_adopt(&kernel_was, pmm_kernel(), hhdm_offset(), vmm_current_root());

    proc->state = PROC_RUNNING;
    current = proc;

    /* The program's own address space, so its addresses mean its pages. The
     * kernel half is shared into it, which is why the code doing the switch
     * still exists on the other side of this line. */
    vmm_activate(&proc->space);
    proc_enter_user(proc);

    /* Back, by way of proc_leave_user, from either an exit or a fault. */
    vmm_activate(&kernel_was);
    current = NULL;

    /* A window belonging to a program that is no longer running is a window
     * nobody can close. Released here rather than in the exit call, so that a
     * program which faulted loses its window on exactly the same path as one
     * that ended properly. */
    winsys_release(proc);
    if (proc->state == PROC_RUNNING) {
        proc->state = PROC_EXITED;
    }
    return !proc->faulted;
}

void process_destroy(struct process *proc)
{
    if (proc == NULL || proc->state == PROC_FREE) {
        return;
    }
    /* The pages first, then the tables that mapped them. The address space
     * frees tables and never pages, because it never knew whether anybody else
     * held one. This is the layer that knows. */
    for (uint64_t i = 0; i < proc->page_count; i++) {
        pmm_free(pmm_kernel(), proc->pages[i]);
    }
    proc->page_count = 0;
    vmm_destroy(&proc->space);
    proc->state = PROC_FREE;
}
