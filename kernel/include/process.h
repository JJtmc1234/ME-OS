/* Processes, and running code the kernel does not trust.
 *
 * A process here is four things: a number, an address space of its own, a
 * privilege level below the kernel's, and a way to stop. That is the smallest
 * set that makes "a program" mean anything. There is no parent, no child, no
 * signal, no file descriptor table and no scheduler, because none of those is
 * needed to answer the question this milestone asks, which is whether code the
 * kernel did not write can run and be stopped without taking the machine with
 * it.
 *
 * The important word is privilege. Up to M31 everything ran at level zero,
 * where every instruction is allowed and every page is reachable. A program
 * runs at level three, where the privileged instructions fault and only pages
 * marked as the user's can be touched. That is not a rule the kernel enforces
 * by checking. It is what the processor does because the code segment selector
 * says three.
 *
 * See M32 in docs/milestones.md.
 */
#ifndef ME_PROCESS_H
#define ME_PROCESS_H

#include <stdbool.h>
#include <stdint.h>

#include "cmdout.h"
#include "trap.h"
#include "vmm.h"

#define PROC_NAME_MAX 32
/* How many pages one program may own. Sixteen is 64 kilobytes, which is far
 * more than anything this kernel can currently load and small enough that the
 * list can live in the structure rather than needing an allocator of its own. */
#define PROC_MAX_PAGES 16
#define PROC_MAX 8

/* Where a program's memory goes. Both well away from the kernel, which lives
 * in the top half, and away from zero so that a null pointer faults. */
#define USER_CODE_AT  0x0000000000400000ull
#define USER_STACK_TOP 0x0000000000800000ull

enum proc_state {
    PROC_FREE = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_EXITED,
};

struct process {
    /* The assembly in procenter.S reads these three by offset. The static
     * assertions in process.c are what stop a field being added above them
     * and silently moving them. */
    uint64_t kernel_rsp;    /* offset 0: where to put the kernel back */
    uint64_t entry;         /* offset 8: first instruction */
    uint64_t user_stack;    /* offset 16: first stack pointer */

    uint64_t pid;
    enum proc_state state;
    struct addrspace space;
    /* Where write goes. The shell's own sink, so a program's output is
     * redirected and piped by exactly the machinery every other command uses. */
    struct cmd_out *out;

    int64_t exit_code;
    bool faulted;
    uint64_t fault_vector;
    uint64_t fault_address;

    char name[PROC_NAME_MAX];
    /* Every page this program owns, so they can be given back. The address
     * space frees its page tables and deliberately not the pages they map,
     * because it never knew who else held one. This is who else. */
    uint64_t pages[PROC_MAX_PAGES];
    uint64_t page_count;

    uint64_t syscalls;
    uint64_t bytes_written;
};

/* Installs the trap handler that makes a fault in a program survivable, and
 * opens the system call gate. Called once at boot. */
void process_init(void);

/* A process with an empty address space of its own. NULL when there is no free
 * slot or no memory. */
struct process *process_create(const char *name, struct cmd_out *out);

/* Gives the program a page at `virt`, optionally filled from `contents`.
 *
 * The page is allocated, zeroed, filled, and mapped with the user bit set.
 * Anything not covered by `bytes` is left zero, which is what a program
 * expects of memory it has not written. */
bool process_add_page(struct process *proc, uint64_t virt, uint64_t flags,
                      const void *contents, uint64_t bytes);

/* Runs it until it exits or faults, and returns when the kernel has it back.
 *
 * This blocks. There is no scheduler yet, so the shell that started a program
 * waits for it, exactly as a shell waits for a command today. */
bool process_run(struct process *proc);

/* Frees every page and the address space. */
void process_destroy(struct process *proc);

/* The table, so a command can list what has run. */
const struct process *process_at(uint64_t index);
uint64_t process_capacity(void);
uint64_t process_count(void);

const char *process_state_text(enum proc_state state);

#endif /* ME_PROCESS_H */
