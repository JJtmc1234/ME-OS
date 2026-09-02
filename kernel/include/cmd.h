/* What the commands typed at the terminal mean.
 *
 * Split from `term.h` because a terminal that also knew what HELP meant could
 * not be tested without testing every command at the same time. This half knows
 * nothing about pixels: it is handed the machine's answers and a place to write
 * to. See M19 in docs/milestones.md.
 *
 * Every command reports something the kernel actually knows. Nothing here
 * invents a filesystem, a process list or a network, because there are none, and
 * a shell that answers questions the machine cannot answer is a mock up.
 */
#ifndef ME_CMD_H
#define ME_CMD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "term.h"
#include "vfs.h"

/* Everything the commands may report, gathered by the caller, which is the only
 * thing that can see the framebuffer, the clock and the window manager. */
struct cmd_context {
    struct term *term;
    uint64_t uptime_seconds;
    uint64_t screen_width;
    uint64_t screen_height;
    /* Usable memory as the bootloader reported it, in bytes. Zero when nothing
     * answered, which the MEM command says rather than printing a zero. */
    uint64_t usable_memory;
    uint64_t total_memory;
    size_t windows_open;
    size_t windows_visible;
    const char *cpu_vendor;
    const char *cpu_brand;
    const char *version;
    /* The filesystem the file commands act on. In memory, because there is no
     * disk driver yet, and real all the same. */
    struct vfs *fs;
};

/* Runs one line. An empty line is not an error and prints nothing extra, which
 * is what pressing Enter at a prompt does on every other machine. */
void cmd_run(struct cmd_context *context, const char *line);

/* Pure. Splits a line into the command and the rest, uppercasing the command so
 * that what a person types matches whatever the keyboard produced. Returns the
 * length of the command word. */
uint64_t cmd_split(const char *line, char *name, uint64_t capacity,
                   const char **rest);

/* Pure. A size in bytes as a number and a unit, so MEM does not print eleven
 * digits and leave the reader to count them. */
void cmd_format_size(uint64_t bytes, char *out, uint64_t capacity);

/* Pure. Splits a line at an unquoted `>`, so `ECHO HI > NOTES` writes a file.
 * The part before the arrow is copied into `command` and the name after it into
 * `target`. Returns false when there is no arrow, in which case neither is
 * touched and the caller runs the line as it stands. */
bool cmd_split_redirect(const char *line, char *command, uint64_t command_capacity,
                        char *target, uint64_t target_capacity);

/* The filesystem commands. In their own file because moving around a tree and
 * asking the machine what it is are two different jobs. */
void cmdfs_pwd(struct cmd_context *context);
void cmdfs_ls(struct cmd_context *context, const char *path);
void cmdfs_cd(struct cmd_context *context, const char *path);
void cmdfs_mkdir(struct cmd_context *context, const char *path);
void cmdfs_touch(struct cmd_context *context, const char *path);
void cmdfs_cat(struct cmd_context *context, const char *path);
void cmdfs_rm(struct cmd_context *context, const char *path);
void cmdfs_write(struct cmd_context *context, const char *path, const char *text);
void cmdfs_df(struct cmd_context *context);

#endif /* ME_CMD_H */
