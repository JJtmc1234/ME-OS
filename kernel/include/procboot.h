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

/* Runs two programs at privilege three: one that writes a line and exits, and
 * one that reads a null pointer. Logs what happened to both. */
void procboot_selfcheck(void);

#endif /* ME_PROCBOOT_H */
