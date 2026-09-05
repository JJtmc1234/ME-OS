/* Reading and writing a program's memory, without trusting it.
 *
 * A system call arrives with numbers in registers. Some of those numbers are
 * addresses, and the program chose them. It may have chosen the address of the
 * kernel's own page tables, or of another program's memory, or of nothing at
 * all, and it may have done so on purpose.
 *
 * The kernel cannot simply follow one. It is running with the program's page
 * tables loaded but at privilege zero, so the user bit on a page stops meaning
 * anything: every mapping in the address space is readable and writable to it,
 * including the kernel half that the program itself cannot touch. A handler
 * that dereferenced a user pointer directly would be a way for any program to
 * read or write any part of the kernel by passing the right number.
 *
 * So every byte crossing the boundary goes through here, and every page of the
 * range is checked to be present and to carry the user bit before any of it is
 * touched.
 *
 * This is all page table arithmetic, so the whole of it is tested on the
 * development machine over an address space built there.
 *
 * See M32 in docs/milestones.md.
 */
#ifndef ME_UACCESS_H
#define ME_UACCESS_H

#include <stdbool.h>
#include <stdint.h>

#include "vmm.h"

/* Whether every page of a range is mapped, carries the user bit, and is
 * writable if asked. A zero length range is fine and reaches nothing.
 *
 * The length is checked for overflow first: a program that passes an address
 * near the top of memory and an enormous length would otherwise have the sum
 * wrap and describe a range that looks small and harmless. */
bool uaccess_range_ok(const struct addrspace *space, uint64_t user, uint64_t bytes,
                      bool need_write);

/* Copies out of a program's memory into the kernel's. Returns false and copies
 * nothing when any part of the range fails the check. Nothing partial: a
 * handler that acted on half a copied structure would be acting on a mixture
 * of what the program sent and whatever was in the buffer before. */
bool uaccess_copy_in(const struct addrspace *space, void *dest, uint64_t user,
                     uint64_t bytes);

/* Copies into a program's memory. Every page must also be writable, or a
 * program could ask the kernel to write to its own code. */
bool uaccess_copy_out(const struct addrspace *space, uint64_t user, const void *src,
                      uint64_t bytes);

/* Copies a zero terminated string out of a program's memory.
 *
 * Separate from a plain copy because the length is not known in advance, so
 * the check has to happen a page at a time as it goes rather than once at the
 * start. Returns false when the range fails, when there is no terminator
 * within `capacity`, or when capacity is zero. The result is always
 * terminated on success. */
bool uaccess_copy_string(const struct addrspace *space, char *dest, uint64_t capacity,
                         uint64_t user);

#endif /* ME_UACCESS_H */
