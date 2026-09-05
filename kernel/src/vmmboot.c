/* Address spaces on the real machine: adoption, creation, and the proof.
 *
 * See M30 in docs/milestones.md.
 */
#include "vmmboot.h"

#include <stddef.h>

#include "log.h"
#include "pmmboot.h"

static struct addrspace kernel_space;
static bool ready;

struct addrspace *vmm_kernel_space(void)
{
    return &kernel_space;
}

bool vmmboot_init(void)
{
    ready = false;
    if (!pmmboot_ready()) {
        log_line("vmm: no page allocator, staying on the bootloader's tables");
        return false;
    }

    /* Adopted rather than rebuilt. The kernel is running on these tables right
     * now, and replacing them at boot would mean building a complete map of
     * the kernel, the direct map and the framebuffer before the first mistake
     * became visible. Borrowing them costs nothing and every process shares
     * their upper half, which is the only part a process ever sees. */
    vmm_adopt(&kernel_space, pmm_kernel(), hhdm_offset(), vmm_current_root());

    if (vmm_enable_nx()) {
        log_line("vmm: no-execute available");
    } else {
        log_line("vmm: no-execute NOT available, pages cannot be made unrunnable");
    }

    ready = true;
    log_named_hex("vmm: kernel page tables at", kernel_space.root);
    return true;
}

enum vmm_result vmm_new_user_space(struct addrspace *space)
{
    if (!ready) {
        return VMM_NO_SPACE;
    }
    enum vmm_result result = vmm_create(space, pmm_kernel(), hhdm_offset());
    if (result != VMM_OK) {
        return result;
    }
    result = vmm_share_kernel(space, &kernel_space);
    if (result != VMM_OK) {
        vmm_destroy(space);
    }
    return result;
}

/* Somewhere in the lower half, well clear of where a program will be loaded.
 * Only ever mapped for the length of the self check. */
#define PROBE_AT 0x0000600000000000ull
#define PROBE_WORD 0x4D454F53504147ull  /* MEOSPAG */

void vmmboot_selfcheck(void)
{
    if (!ready) {
        log_line("vmm: selfcheck skipped, no address spaces");
        return;
    }

    struct addrspace space;
    enum vmm_result result = vmm_new_user_space(&space);
    if (result != VMM_OK) {
        log_line("vmm: SELFCHECK FAILED to build a space");
        log_line(vmm_result_text(result));
        return;
    }

    uint64_t phys = PMM_NONE;
    uint64_t *page = (uint64_t *)pmm_alloc_zeroed(&phys);
    if (page == NULL) {
        log_line("vmm: SELFCHECK FAILED, no page to map");
        vmm_destroy(&space);
        return;
    }
    page[0] = PROBE_WORD;

    result = vmm_map(&space, PROBE_AT, phys, PTE_WRITE);
    if (result != VMM_OK) {
        log_line("vmm: SELFCHECK FAILED to map the probe page");
        log_line(vmm_result_text(result));
        pmm_free(pmm_kernel(), phys);
        vmm_destroy(&space);
        return;
    }

    /* Before switching, prove the two things the kernel cannot survive losing.
     *
     * The stack first. Switching to a space where the current stack is not
     * mapped means the next push faults, and the fault handler needs a stack,
     * so the machine triple faults and reboots with nothing on the screen and
     * nothing in the log. Checked rather than assumed, because the answer
     * depends on where the bootloader put the stack. */
    uint64_t stack_now = 0;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(stack_now));

    bool stack_ok = vmm_translate(&space, stack_now & ~0xFFFull, NULL, NULL) == VMM_OK;
    bool code_ok = vmm_translate(&space, (uint64_t)(uintptr_t)&vmmboot_selfcheck & ~0xFFFull,
                                 NULL, NULL) == VMM_OK;
    bool probe_absent = vmm_translate(&kernel_space, PROBE_AT, NULL, NULL) == VMM_NOT_MAPPED;

    if (!stack_ok || !code_ok) {
        log_line("vmm: SELFCHECK FAILED, the new space cannot see the kernel");
        log_named_dec("vmm:   stack reachable", stack_ok ? 1u : 0u);
        log_named_dec("vmm:   code reachable", code_ok ? 1u : 0u);
        vmm_unmap(&space, PROBE_AT);
        pmm_free(pmm_kernel(), phys);
        vmm_destroy(&space);
        return;
    }

    /* The switch itself. Everything above says the tree looks right. This is
     * the only thing that says the processor agrees. */
    uint64_t was = vmm_current_root();
    vmm_activate(&space);
    volatile uint64_t read_back = *(volatile uint64_t *)PROBE_AT;
    struct addrspace back;
    vmm_adopt(&back, pmm_kernel(), hhdm_offset(), was);
    vmm_activate(&back);

    vmm_unmap(&space, PROBE_AT);
    pmm_free(pmm_kernel(), phys);

    uint64_t free_before_destroy = pmm_kernel()->free_pages;
    vmm_destroy(&space);
    uint64_t recovered = pmm_kernel()->free_pages - free_before_destroy;

    if (read_back == PROBE_WORD && probe_absent && recovered > 0 &&
        vmm_current_root() == was) {
        log_line("vmm: selfcheck passed, a second address space was built and run on");
        log_named_dec("vmm: tables returned", recovered);
    } else {
        log_line("vmm: SELFCHECK FAILED");
        log_named_hex("vmm:   read back", read_back);
        log_named_dec("vmm:   probe absent from the kernel space", probe_absent ? 1u : 0u);
        log_named_dec("vmm:   tables returned", recovered);
    }
}
