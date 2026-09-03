/* Host tests for the M23 on disk filesystem.
 *
 * The disk here is an array. That is the whole reason `struct disk` is two
 * function pointers: the format and the checking can be exercised on this
 * machine, in a second, including the failures that are hard to arrange on real
 * hardware and impossible to arrange on purpose.
 *
 * Two things are being checked. That what goes onto the disk comes back off it
 * unchanged, and that a disk saying something impossible is refused rather than
 * believed. The second matters more. A wrong round trip loses a file. A
 * believed corrupt disk hangs the machine in `vfs_path_of` or reads past the
 * end of the node table, and neither looks like a filesystem problem when it
 * happens.
 */
#include <stdio.h>
#include <string.h>

#include "vfsdisk.h"

static int failures;

static void check(int condition, const char *what)
{
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

/* A disk made of memory, and a switch to make it fail. */
static uint8_t image[VFSDISK_SECTORS * DISK_SECTOR];
/* How many reads succeed before the disk starts failing. A disk that fails on
 * the very first read and one that fails partway through are different events
 * with different right answers, and a plain on and off switch cannot tell them
 * apart. */
#define NEVER ((uint64_t)-1)
static uint64_t reads_fail_after = NEVER;
static uint64_t reads_done;
static bool writes_fail;

static bool mem_read(void *context, uint64_t sector, void *out, uint64_t count)
{
    (void)context;
    if (reads_done++ >= reads_fail_after) {
        return false;
    }
    memcpy(out, image + sector * DISK_SECTOR, count * DISK_SECTOR);
    return true;
}

static bool mem_write(void *context, uint64_t sector, const void *in, uint64_t count)
{
    (void)context;
    if (writes_fail) {
        return false;
    }
    memcpy(image + sector * DISK_SECTOR, in, count * DISK_SECTOR);
    return true;
}

static const struct disk memory_disk = {
    .read = mem_read,
    .write = mem_write,
    .context = NULL,
    .sectors = VFSDISK_SECTORS,
};

/* The record for node `i`, so a test can break exactly one field of one node. */
static uint8_t *record(int16_t node)
{
    return image + (1 + (uint64_t)node * VFSDISK_NODE_SECTORS) * DISK_SECTOR;
}

static void put16_at(uint8_t *at, int16_t value)
{
    at[0] = (uint8_t)(uint16_t)value;
    at[1] = (uint8_t)((uint16_t)value >> 8);
}

static struct vfs source;
static struct vfs loaded;

/* A filesystem with enough shape to notice if any of it moved. */
static void build(void)
{
    vfs_init(&source);
    vfs_mkdir(&source, "/PROJECTS");
    vfs_mkdir(&source, "/PROJECTS/ME-OS");
    vfs_mkdir(&source, "/HOME");
    vfs_write(&source, "/HOME/NOTES.TXT", "THE FIRST LINE");
    vfs_write(&source, "/PROJECTS/ME-OS/TODO.TXT", "WRITE A DISK DRIVER");
    vfs_create(&source, "/HOME/EMPTY.TXT");
    vfs_chdir(&source, "/HOME");
}

static bool same_text(const struct vfs *fs, const char *path, const char *want)
{
    char out[VFS_FILE_MAX + 1];
    uint64_t length = 0;
    if (vfs_read(fs, path, out, sizeof out, &length) != VFS_OK) {
        return false;
    }
    return length == strlen(want) && memcmp(out, want, length) == 0;
}

/* Saves `source`, loads it back into `loaded`, and hands back what load said. */
static enum vfsdisk_result round_trip(void)
{
    memset(image, 0, sizeof image);
    const enum vfsdisk_result saved = vfsdisk_save(&source, &memory_disk);
    if (saved != VFSDISK_OK) {
        return saved;
    }
    return vfsdisk_load(&loaded, &memory_disk);
}

static void test_what_goes_on_comes_back(void)
{
    printf("a filesystem written to a disk reads back the same\n");
    build();
    check(round_trip() == VFSDISK_OK, "it saved and loaded");
    check(vfsdisk_sound(&loaded), "and what came back is walkable");

    check(vfs_resolve(&loaded, "/PROJECTS/ME-OS") != VFS_NONE, "the tree is there");
    check(same_text(&loaded, "/HOME/NOTES.TXT", "THE FIRST LINE"), "and so is a file");
    check(same_text(&loaded, "/PROJECTS/ME-OS/TODO.TXT", "WRITE A DISK DRIVER"),
          "and one further down");
    check(vfs_resolve(&loaded, "/HOME/EMPTY.TXT") != VFS_NONE, "an empty file survives");
    check(same_text(&loaded, "/HOME/EMPTY.TXT", ""), "and is still empty");
    check(vfs_used_nodes(&loaded) == vfs_used_nodes(&source), "nothing was lost");

    printf("the working directory is not on the disk, so it comes back at the root\n");
    check(loaded.cwd == 0, "which is somewhere that certainly exists");

    printf("an empty filesystem is a real thing to save\n");
    vfs_init(&source);
    check(round_trip() == VFSDISK_OK, "it saves and loads");
    check(vfs_used_nodes(&loaded) == 1, "holding only the root");

    printf("a full one fits\n");
    vfs_init(&source);
    for (int i = 0; vfs_create(&source, "X") == VFS_OK; i++) {
        char name[VFS_NAME_MAX];
        snprintf(name, sizeof name, "F%d", i);
        if (vfs_move(&source, "X", name) != VFS_OK) {
            break;
        }
    }
    const uint64_t before = vfs_used_nodes(&source);
    check(before > VFS_MAX_NODES - 3, "the table is nearly full");
    check(round_trip() == VFSDISK_OK, "and it still round trips");
    check(vfs_used_nodes(&loaded) == before, "with every entry");
}

static void test_a_deleted_file_is_not_written_out(void)
{
    printf("what a deleted file held does not reach the disk\n");
    vfs_init(&source);
    vfs_write(&source, "/SECRET.TXT", "PINEAPPLE ON PIZZA");
    check(vfs_remove(&source, "/SECRET.TXT") == VFS_OK, "the file is deleted");

    memset(image, 0, sizeof image);
    check(vfsdisk_save(&source, &memory_disk) == VFSDISK_OK, "and the disk written");

    /* `vfs_init` and `vfs_remove` mark a node free without clearing it, so the
     * contents are still in memory. Writing them out would put a deleted file
     * somewhere it can be read back. */
    bool found = false;
    for (uint64_t i = 0; i + 18 <= sizeof image; i++) {
        if (memcmp(image + i, "PINEAPPLE ON PIZZA", 18) == 0) {
            found = true;
        }
    }
    check(!found, "and none of it is anywhere on the disk");
}

static void test_a_disk_that_is_not_ours(void)
{
    printf("a blank disk is nothing to load, which is not a fault\n");
    memset(image, 0, sizeof image);
    vfs_init(&loaded);
    check(vfsdisk_load(&loaded, &memory_disk) == VFSDISK_NOT_FORMATTED,
          "it says there is nothing of ours there");

    printf("somebody else's disk is left alone rather than read\n");
    memset(image, 0xE9, sizeof image);
    check(vfsdisk_load(&loaded, &memory_disk) == VFSDISK_NOT_FORMATTED,
          "no magic, so no filesystem");

    build();
    printf("a disk from another version says so rather than being guessed at\n");
    check(round_trip() == VFSDISK_OK, "start from a good one");
    image[8] = (uint8_t)(VFSDISK_VERSION + 1);
    check(vfsdisk_load(&loaded, &memory_disk) == VFSDISK_WRONG_VERSION,
          "and it is refused by version");

    printf("and one written with different limits, which would be read crooked\n");
    check(round_trip() == VFSDISK_OK, "start from a good one again");
    image[12] = (uint8_t)(VFS_MAX_NODES + 1);
    check(vfsdisk_load(&loaded, &memory_disk) == VFSDISK_WRONG_SHAPE,
          "refused by shape");
}

static void test_a_disk_that_is_ours_and_impossible(void)
{
    struct { const char *what; int16_t node; uint64_t at; uint8_t bytes[2]; } breaks[] = {
        { "a parent past the end of the table",      1, 26, { 0x00, 0x40 } },
        { "a parent that is not a node at all",      1, 26, { 0xFF, 0xFE } },
        { "a first child pointing at a free slot",   0, 28, { 0x5F, 0x00 } },
        { "a sibling pointing at a free slot",       1, 30, { 0x5F, 0x00 } },
        { "a file longer than a file can be",        0,  0, { 0x00, 0x00 } },
        { "the root marked free",                    0, 25, { 0x00, 0x00 } },
        { "the root given a parent that is not it",  0, 26, { 0x01, 0x00 } },
    };

    printf("a disk of ours that says something impossible is refused\n");
    for (uint64_t i = 0; i < sizeof breaks / sizeof breaks[0]; i++) {
        build();
        if (round_trip() != VFSDISK_OK) {
            check(0, "the good disk should have loaded");
            continue;
        }
        if (breaks[i].at == 0) {
            /* The one that is a length rather than a link. */
            const int16_t file = vfs_resolve(&source, "/HOME/NOTES.TXT");
            record(file)[32] = 0xFF;
            record(file)[33] = 0xFF;
        } else {
            record(breaks[i].node)[breaks[i].at] = breaks[i].bytes[0];
            record(breaks[i].node)[breaks[i].at + 1] = breaks[i].bytes[1];
        }
        vfs_init(&loaded);
        const enum vfsdisk_result got = vfsdisk_load(&loaded, &memory_disk);
        check(got == VFSDISK_CORRUPT, breaks[i].what);
        /* And what is left is empty rather than half read, because half a
         * filesystem looks exactly like a whole one. */
        check(vfsdisk_sound(&loaded) && vfs_used_nodes(&loaded) == 1,
              "  leaving an empty filesystem that still works");
    }

    printf("a name that never ends would be read past the end of itself\n");
    build();
    check(round_trip() == VFSDISK_OK, "start from a good one");
    memset(record(vfs_resolve(&source, "/HOME")), 'A', VFS_NAME_MAX);
    check(vfsdisk_load(&loaded, &memory_disk) == VFSDISK_CORRUPT, "so it is refused");

    printf("a name holding a slash could never be resolved back\n");
    build();
    check(round_trip() == VFSDISK_OK, "start from a good one");
    record(vfs_resolve(&source, "/HOME"))[1] = '/';
    check(vfsdisk_load(&loaded, &memory_disk) == VFSDISK_CORRUPT, "so it is refused");

    printf("a loop in the parents would hang anything walking up\n");
    build();
    check(round_trip() == VFSDISK_OK, "start from a good one");
    const int16_t projects = vfs_resolve(&source, "/PROJECTS");
    const int16_t meos = vfs_resolve(&source, "/PROJECTS/ME-OS");
    put16_at(record(projects) + 26, meos);
    check(vfsdisk_load(&loaded, &memory_disk) == VFSDISK_CORRUPT, "so it is refused");

    printf("a loop in a sibling list would hang anything listing it\n");
    build();
    check(round_trip() == VFSDISK_OK, "start from a good one");
    const int16_t home = vfs_resolve(&source, "/HOME");
    const int16_t notes = vfs_resolve(&source, "/HOME/NOTES.TXT");
    put16_at(record(notes) + 30, notes);
    check(vfsdisk_load(&loaded, &memory_disk) == VFSDISK_CORRUPT, "so it is refused");
    (void)home;

    printf("a node its own parent does not list could never be deleted\n");
    build();
    check(round_trip() == VFSDISK_OK, "start from a good one");
    put16_at(record(vfs_resolve(&source, "/HOME")) + 28, VFS_NONE);
    check(vfsdisk_load(&loaded, &memory_disk) == VFSDISK_CORRUPT, "so it is refused");
}

static void test_a_disk_that_will_not_answer(void)
{
    printf("a disk that will not answer at all leaves the filesystem alone\n");
    build();
    check(round_trip() == VFSDISK_OK, "a good disk first");
    reads_done = 0;
    reads_fail_after = 0;   /* the header read itself */
    check(vfsdisk_load(&loaded, &memory_disk) == VFSDISK_IO_FAILED, "the read fails");
    reads_fail_after = NEVER;
    /* Nothing was read, so nothing was replaced. Emptying a working filesystem
     * because a disk would not answer would lose more than the failure did. */
    check(same_text(&loaded, "/HOME/NOTES.TXT", "THE FIRST LINE"),
          "and what was already loaded is untouched");

    printf("one that fails partway leaves it empty rather than half read\n");
    reads_done = 0;
    reads_fail_after = 4;   /* the header and three nodes, then nothing */
    check(vfsdisk_load(&loaded, &memory_disk) == VFSDISK_IO_FAILED, "the read fails");
    reads_fail_after = NEVER;
    check(vfsdisk_sound(&loaded), "and what is left is still walkable");
    check(vfs_used_nodes(&loaded) == 1, "and holds nothing, because half a "
                                        "filesystem looks like a whole one");

    writes_fail = true;
    check(vfsdisk_save(&source, &memory_disk) == VFSDISK_IO_FAILED, "so does a write");
    writes_fail = false;

    printf("no disk at all is said plainly rather than treated as an empty one\n");
    const struct disk none = { NULL, NULL, NULL, 0 };
    check(vfsdisk_save(&source, &none) == VFSDISK_NO_DISK, "saving says so");
    check(vfsdisk_load(&loaded, &none) == VFSDISK_NO_DISK, "and so does loading");
    check(vfsdisk_load(&loaded, NULL) == VFSDISK_NO_DISK, "and nothing at all");

    printf("a disk too small for the filesystem cannot be half written\n");
    const struct disk tiny = { mem_read, mem_write, NULL, VFSDISK_SECTORS - 1 };
    check(vfsdisk_save(&source, &tiny) == VFSDISK_IO_FAILED, "the last node is refused");

    printf("and the guards hold whatever they are handed\n");
    check(!disk_read(&memory_disk, 0, NULL, 1), "nowhere to read into");
    check(!disk_write(&memory_disk, 0, NULL, 1), "nothing to write");
    check(!disk_read(&memory_disk, 0, image, 0), "no sectors at all");
    check(!disk_read(&memory_disk, VFSDISK_SECTORS, image, 1), "past the end");
    check(!disk_read(&memory_disk, 1, image, (uint64_t)-1), "a count that would wrap");
    check(!disk_present(&none), "a disk with no functions is no disk");
}

static void test_a_filesystem_built_in_memory_is_sound(void)
{
    printf("everything the filesystem itself builds passes the check\n");
    build();
    check(vfsdisk_sound(&source), "a tree with files in it");
    vfs_init(&source);
    check(vfsdisk_sound(&source), "and an empty one");
    check(!vfsdisk_sound(NULL), "and nothing is not a filesystem");

    /* If this ever fails it means `vfs.c` can produce an arrangement the check
     * calls impossible, which would make the check the bug rather than the
     * disk. Worth knowing immediately. */
    build();
    vfs_remove(&source, "/HOME/EMPTY.TXT");
    vfs_move(&source, "/HOME/NOTES.TXT", "/PROJECTS/NOTES.TXT");
    vfs_copy(&source, "/PROJECTS/NOTES.TXT", "/PROJECTS/COPY.TXT");
    check(vfsdisk_sound(&source), "and one that has been moved about");
}

int main(void)
{
    test_what_goes_on_comes_back();
    test_a_deleted_file_is_not_written_out();
    test_a_disk_that_is_not_ours();
    test_a_disk_that_is_ours_and_impossible();
    test_a_disk_that_will_not_answer();
    test_a_filesystem_built_in_memory_is_sound();

    if (failures > 0) {
        printf("\n%d disk filesystem check(s) FAILED\n", failures);
        return 1;
    }
    printf("\ndisk filesystem checks passed\n");
    return 0;
}
