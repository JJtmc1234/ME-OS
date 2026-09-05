/* The boot time proof that user mode works.
 *
 * Kept apart from process.c because it is a test, not a facility. Nothing in
 * the running system calls it, and when M33 makes the shell able to run a real
 * program from a file this will still be here, because a self check that only
 * runs when somebody types something is not a regression test.
 *
 * See M32 in docs/milestones.md.
 */
#ifndef ME_PROCBOOT_H
#define ME_PROCBOOT_H

#include <stdbool.h>
#include <stdint.h>

/* Runs two programs at privilege three: one that writes a line and exits, and
 * one that reads a null pointer. Logs what happened to both. */
void procboot_selfcheck(void);

/* Loads and runs an executable held in memory, and reports whether it said
 * what it was supposed to. This is M33's proof: the bytes come off the
 * filesystem, having arrived on the disc as a file of their own. */
bool procboot_run_file(const char *name, const uint8_t *file, uint64_t bytes);

#endif /* ME_PROCBOOT_H */
