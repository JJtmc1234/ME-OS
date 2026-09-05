/* Proving at boot that a program can run and that a broken one costs only
 * itself.
 *
 * Two programs, because the milestone is two claims and the second is the one
 * that matters. That correct code runs at privilege three is pleasant. That
 * incorrect code stops at privilege three, and the machine carries on, is what
 * makes privilege separation something other than decoration.
 *
 * See M32 in docs/milestones.md.
 */
#include "procboot.h"

#include <stddef.h>

#include "cmdout.h"
#include "log.h"
#include "process.h"
#include "elf.h"
#include "elfload.h"
#include "syscall.h"

/* Placed in the kernel image by userbin.S, assembled from the sources in the user directory. */
extern const uint8_t user_fault[];
extern const uint8_t user_fault_end[];
extern const uint8_t user_peek[];
extern const uint8_t user_peek_end[];

/* Where a program's output is caught, so the self check can say what it said
 * rather than only that it said something. */
static char captured[256];

/* Builds a process out of a blob: one page of code, one page of stack. */
static struct process *load_blob(const char *name, const uint8_t *from,
                                 const uint8_t *to, struct cmd_out *out)
{
    uint64_t bytes = (uint64_t)(to - from);
    if (bytes == 0 || bytes > PAGE_SIZE) {
        return NULL;
    }
    struct process *proc = process_create(name, out);
    if (proc == NULL) {
        return NULL;
    }

    /* Code, readable and executable and deliberately not writable. A program
     * that can rewrite its own instructions is one whose behaviour cannot be
     * reasoned about from its file. */
    if (!process_add_page(proc, USER_CODE_AT, 0, from, bytes)) {
        process_destroy(proc);
        return NULL;
    }
    /* Stack, writable and not executable where the processor allows it, so
     * data a program pushed cannot become instructions it runs. */
    uint64_t stack_flags = PTE_WRITE | (vmm_nx_available() ? PTE_NX : 0);
    if (!process_add_page(proc, USER_STACK_TOP - PAGE_SIZE, stack_flags, NULL, 0)) {
        process_destroy(proc);
        return NULL;
    }
    return proc;
}

bool procboot_run_file(const char *name, const uint8_t *file, uint64_t bytes)
{
    struct cmd_out out;
    cmd_out_to_buffer(&out, captured, sizeof captured);

    if (!elf_looks_like_elf(file, bytes)) {
        log_line("elf: SELFCHECK FAILED, the file on the disk is not an executable");
        return false;
    }

    struct process *proc = process_create(name, &out);
    if (proc == NULL) {
        log_line("elf: SELFCHECK FAILED, no room for a process");
        return false;
    }
    enum elf_result loaded = elfload(proc, file, bytes);
    if (loaded != ELF_OK) {
        log_str("elf: SELFCHECK FAILED to load, ");
        log_str(elf_result_text(loaded));
        log_str("\n");
        process_destroy(proc);
        return false;
    }
    uint64_t stack_flags = PTE_WRITE | (vmm_nx_available() ? PTE_NX : 0);
    if (!process_add_page(proc, USER_STACK_TOP - PAGE_SIZE, stack_flags, NULL, 0)) {
        log_line("elf: SELFCHECK FAILED, no page for a stack");
        process_destroy(proc);
        return false;
    }
    proc->user_stack = USER_STACK_TOP;

    bool clean = process_run(proc);
    int64_t code = proc->exit_code;
    uint64_t wrote = proc->bytes_written;
    process_destroy(proc);

    const char *said = cmd_out_text(&out);
    bool right_words = said[0] == 'H' && said[1] == 'E' && said[2] == 'L' &&
                       said[3] == 'L' && said[4] == 'O';

    if (clean && code == 0 && wrote == 21 && right_words) {
        log_str("elf: selfcheck passed, a program loaded from a file said: ");
        log_str(said);
        log_named_dec("elf:   bytes of file", bytes);
        return true;
    }
    log_line("elf: SELFCHECK FAILED after loading");
    log_named_dec("elf:   ran cleanly", clean ? 1u : 0u);
    log_named_dec("elf:   exit code", (uint64_t)code);
    log_named_dec("elf:   bytes written", wrote);
    return false;
}

void procboot_selfcheck(void)
{
    /* The second claim. This program reads address zero, which is mapped in no
     * address space, so the processor faults. Everything after this line is
     * the proof that the machine is still here. */
    struct cmd_out nowhere;
    cmd_out_to_buffer(&nowhere, captured, sizeof captured);
    struct process *bad = load_blob("fault", user_fault, user_fault_end, &nowhere);
    if (bad == NULL) {
        log_line("process: SELFCHECK FAILED, could not build the faulting program");
        return;
    }

    bool bad_clean = process_run(bad);
    bool faulted = bad->faulted;
    uint64_t vector = bad->fault_vector;
    uint64_t reached_for = bad->fault_address;
    process_destroy(bad);

    if (!bad_clean && faulted && vector == TRAP_PAGE_FAULT && reached_for == 0) {
        log_line("process: selfcheck passed, a faulting program was stopped and the "
                 "machine carried on");
        log_named_dec("process:   stopped by vector", vector);
    } else {
        log_line("process: SELFCHECK FAILED on the faulting program");
        log_named_dec("process:   reported a fault", faulted ? 1u : 0u);
        log_named_dec("process:   vector", vector);
        log_named_hex("process:   reached for", reached_for);
    }

    /* The third claim, and the one the whole milestone is for. The kernel is
     * mapped in this program's address space, because a system call has to
     * land somewhere. This program holds a correct address for it and is
     * refused anyway, because those pages have no user bit. */
    struct cmd_out peeked;
    cmd_out_to_buffer(&peeked, captured, sizeof captured);
    struct process *nosy = load_blob("peek", user_peek, user_peek_end, &peeked);
    if (nosy == NULL) {
        log_line("process: SELFCHECK FAILED, could not build the peeking program");
        return;
    }

    bool peek_clean = process_run(nosy);
    bool peek_faulted = nosy->faulted;
    uint64_t peek_at = nosy->fault_address;
    uint64_t peek_wrote = nosy->bytes_written;
    process_destroy(nosy);

    if (!peek_clean && peek_faulted && peek_wrote == 0 &&
        peek_at >= 0xFFFFFFFF80000000ull) {
        log_line("process: selfcheck passed, a program was refused the kernel's own "
                 "memory at an address it had correctly");
        log_named_hex("process:   refused at", peek_at);
    } else {
        log_line("process: SELFCHECK FAILED, a program reached the kernel");
        log_named_dec("process:   was refused", peek_faulted ? 1u : 0u);
        log_named_hex("process:   fault address", peek_at);
    }

    log_named_dec("process: system calls served", syscall_served());
    log_named_dec("process: system calls refused", syscall_refused());
}
