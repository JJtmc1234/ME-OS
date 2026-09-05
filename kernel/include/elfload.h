/* Loading a checked ELF file into a process.
 *
 * Split from elf.c on purpose. Everything that can go wrong by trusting a
 * number somebody else wrote is over there and is tested against files built
 * to be wrong. What is here is arithmetic on values that have already been
 * checked, and the one thing that has to be got right is that a segment is not
 * a page: it has a byte address and a byte length, neither necessarily
 * aligned, and two segments may share the page at their ends.
 *
 * See M33 in docs/milestones.md.
 */
#ifndef ME_ELFLOAD_H
#define ME_ELFLOAD_H

#include <stdint.h>

#include "elf.h"
#include "process.h"

/* Maps the file's segments into the process and sets its entry point. The
 * process keeps whatever pages were mapped even on failure, so the caller
 * destroys it either way. */
enum elf_result elfload(struct process *proc, const uint8_t *file, uint64_t bytes);

#endif /* ME_ELFLOAD_H */
