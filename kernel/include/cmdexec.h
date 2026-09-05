/* Running a program file from the shell.
 *
 * RUN has read files since M27, but everything it could do with one was
 * interpret its lines as commands. This is the other thing a file can be.
 *
 * Which one it is is decided by looking at the file rather than at its name.
 * That is what every Unix does, and the reason is that a name is a claim and
 * the first four bytes are evidence. A file that begins with the ELF magic is
 * a program, and anything else is a script.
 *
 * See M33 in docs/milestones.md.
 */
#ifndef ME_CMDEXEC_H
#define ME_CMDEXEC_H

#include <stdint.h>

#include "cmd.h"

/* Loads and runs a program, and reports what happened to it. `name` is only
 * for the process table and the messages. */
void cmdexec_program(struct cmd_context *context, const char *name,
                     const uint8_t *file, uint64_t bytes);

/* Lists what has run. */
void cmdexec_ps(struct cmd_context *context);

#endif /* ME_CMDEXEC_H */
