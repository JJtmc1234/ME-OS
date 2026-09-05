/* Bringing address spaces up on the real machine.
 *
 * `vmm.c` builds page table trees and knows nothing about the processor. This
 * is the part that adopts the tables the bootloader left running, turns on the
 * no-execute bit, and owns the one address space the kernel itself lives in.
 *
 * See M30 in docs/milestones.md.
 */
#ifndef ME_VMMBOOT_H
#define ME_VMMBOOT_H

#include <stdbool.h>

#include "vmm.h"

/* Adopts the page tables the machine is already running on and turns on the
 * no-execute bit if the processor has it. Returns false when there is no page
 * allocator, in which case no new address space can be built and the kernel
 * carries on with the one it booted on. */
bool vmmboot_init(void);

/* The address space the kernel runs in. Its upper half is what every process
 * borrows. Never NULL, but has no root until init succeeds. */
struct addrspace *vmm_kernel_space(void);

/* Builds an address space for a program: a fresh tree with the kernel's upper
 * half shared into it. The lower half is empty and is the process's own. */
enum vmm_result vmm_new_user_space(struct addrspace *space);

/* Proves at boot that the tables this kernel builds are ones the processor
 * will actually run on.
 *
 * The host tests cover the whole walk and cannot cover that. Building a
 * correct looking tree and having the processor accept it are two different
 * claims, and the second one is only answered by loading it into CR3. So this
 * makes a space, checks the stack and the kernel are both reachable in it,
 * switches to it, reads back through a mapping that exists only there, and
 * switches back. */
void vmmboot_selfcheck(void);

#endif /* ME_VMMBOOT_H */
