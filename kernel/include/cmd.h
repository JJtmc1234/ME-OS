/* What the commands typed at the terminal mean.
 *
 * Split from `term.h` because a terminal that also knew what HELP meant could
 * not be tested without testing every command at the same time. This half knows
 * nothing about pixels: it is handed the machine's answers and a place to write
 * to. See M19 in docs/milestones.md.
 *
 * Every command reports something the kernel actually knows. Nothing here
 * invents a process list or a network, because there are none, and a shell that
 * answers questions the machine cannot answer is a mock up. The filesystem is
 * real, and since M23 so is the disk under it.
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
    /* The time of day, when the clock answered. Empty when it did not, which
     * DATE says rather than printing a plausible one. */
    const char *date;
    const char *time;
    /* Set by EDIT to the file it wants opened. The shell cannot open a window,
     * so it says what it wants and the caller, which can, does it. */
    char open_editor[VFS_PATH_MAX];
    /* The filesystem the file commands act on. */
    struct vfs *fs;
    /* The disk it is saved to, as facts rather than as a device, so the
     * commands can be tested without one anywhere near them. An empty model
     * means the machine found no disk, which DF says plainly rather than
     * printing a size of nothing. */
    const char *disk_model;
    uint64_t disk_sectors;
    uint64_t disk_sector_bytes;
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
void cmdfs_mv(struct cmd_context *context, const char *rest);
void cmdfs_cp(struct cmd_context *context, const char *rest);
void cmdfs_wc(struct cmd_context *context, const char *path);
void cmdfs_tree(struct cmd_context *context, const char *path);
void cmdfs_df(struct cmd_context *context);

#endif /* ME_CMD_H */
