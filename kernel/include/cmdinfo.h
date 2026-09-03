/* The commands that report the machine, kept apart from the ones that run it.
 *
 * Internal to the shell, like `vfsnode.h` is to the filesystem. Nothing outside
 * `cmd.c` calls these: they are separate so the file listing every command the
 * shell knows is a file somebody can read in a sitting.
 *
 * See M19 in docs/milestones.md.
 */
#ifndef ME_CMDINFO_H
#define ME_CMDINFO_H

#include "cmd.h"

void cmdinfo_help(struct cmd_out *out);
void cmdinfo_uptime(struct cmd_context *context);
void cmdinfo_mem(struct cmd_context *context);
void cmdinfo_cpu(struct cmd_context *context);
void cmdinfo_windows(struct cmd_context *context);

#endif /* ME_CMDINFO_H */
