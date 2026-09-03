/* Host tests for the M20 in memory filesystem.
 *
 * A filesystem is worth testing where it is easy to get wrong and impossible to
 * see: path resolution with `.` and `..`, the root that must not be climbed out
 * of, unlinking that must not leave a dangling sibling, and the working
 * directory that must not be left pointing at something that was deleted.
 *
 * None of that needs a disk, which is fortunate, because there is not one.
 */
#include <stdio.h>
#include <string.h>

#include "vfs.h"

static int failures;
static struct vfs fs;

static void check(int condition, const char *what)
{
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

static const char *where(void)
{
    static char path[VFS_PATH_MAX];
    vfs_path_of(&fs, fs.cwd, path, sizeof path);
    return path;
}

static const char *path_of(const char *name)
{
    static char path[VFS_PATH_MAX];
    vfs_path_of(&fs, vfs_resolve(&fs, name), path, sizeof path);
    return path;
}

static void test_an_empty_filesystem_is_a_root_and_nothing_else(void)
{
    printf("a new filesystem holds the root and nothing else\n");
    vfs_init(&fs);
    check(vfs_resolve(&fs, "/") == 0, "the root resolves");
    check(vfs_resolve(&fs, "") == 0, "and so does an empty path");
    check(vfs_used_nodes(&fs) == 1, "one entry is in use");
    check(vfs_count_children(&fs, 0) == 0, "with nothing in it");
    check(strcmp(where(), "/") == 0, "and the working directory is the root");
}

static void test_making_and_walking_a_tree(void)
{
    printf("directories and files are made and found again\n");
    vfs_init(&fs);
    check(vfs_mkdir(&fs, "/HOME") == VFS_OK, "a directory at the root");
    check(vfs_mkdir(&fs, "/HOME/JJ") == VFS_OK, "and one inside it");
    check(vfs_create(&fs, "/HOME/JJ/NOTES.TXT") == VFS_OK, "and a file in that");

    check(vfs_resolve(&fs, "/HOME/JJ/NOTES.TXT") != VFS_NONE, "the file is there");
    check(strcmp(path_of("/HOME/JJ/NOTES.TXT"), "/HOME/JJ/NOTES.TXT") == 0,
          "and its full path reads back the same");
    check(vfs_count_children(&fs, vfs_resolve(&fs, "/HOME")) == 1,
          "HOME holds one thing");

    printf("a listing comes out in the order things were made\n");
    check(vfs_create(&fs, "/A") == VFS_OK, "one");
    check(vfs_create(&fs, "/B") == VFS_OK, "two");
    check(vfs_create(&fs, "/C") == VFS_OK, "three");
    int16_t at = vfs_first_child(&fs, 0);
    check(strcmp(vfs_get(&fs, at)->name, "HOME") == 0, "HOME came first");
    at = vfs_next_sibling(&fs, at);
    check(strcmp(vfs_get(&fs, at)->name, "A") == 0, "then A");
    at = vfs_next_sibling(&fs, at);
    check(strcmp(vfs_get(&fs, at)->name, "B") == 0, "then B");
    at = vfs_next_sibling(&fs, at);
    check(strcmp(vfs_get(&fs, at)->name, "C") == 0, "then C");
    check(vfs_next_sibling(&fs, at) == VFS_NONE, "and that is all of them");
}

static void test_relative_paths_and_dot_dot(void)
{
    printf("paths are relative to where you are unless they start with a slash\n");
    vfs_init(&fs);
    vfs_mkdir(&fs, "/HOME");
    vfs_mkdir(&fs, "/HOME/JJ");
    vfs_mkdir(&fs, "/TMP");

    check(vfs_chdir(&fs, "/HOME") == VFS_OK, "move to HOME");
    check(strcmp(where(), "/HOME") == 0, "and that is where we are");
    check(vfs_resolve(&fs, "JJ") != VFS_NONE, "a bare name is found from here");
    check(vfs_resolve(&fs, "TMP") == VFS_NONE, "and one that is not here is not");
    check(vfs_resolve(&fs, "/TMP") != VFS_NONE, "but the same name from the root is");

    printf("dot stays put and dot dot goes up\n");
    check(vfs_chdir(&fs, ".") == VFS_OK, "a dot is a move to here");
    check(strcmp(where(), "/HOME") == 0, "which changes nothing");
    check(vfs_chdir(&fs, "JJ") == VFS_OK, "into JJ");
    check(vfs_chdir(&fs, "..") == VFS_OK, "and back out");
    check(strcmp(where(), "/HOME") == 0, "to HOME again");
    check(vfs_resolve(&fs, "../TMP") != VFS_NONE, "a path can go up and along");
    check(vfs_resolve(&fs, "./JJ/../JJ") != VFS_NONE, "and wander and still arrive");

    /* The check that matters. A path made of enough `..` must stop at the root
     * rather than walking off the front of the node table. */
    printf("no path can climb out of the root\n");
    check(vfs_chdir(&fs, "/") == VFS_OK, "start at the root");
    check(vfs_chdir(&fs, "..") == VFS_OK, "going up from the root is allowed");
    check(strcmp(where(), "/") == 0, "and leaves you at the root");
    check(vfs_resolve(&fs, "../../../../HOME") != VFS_NONE,
          "and a pile of them still finds HOME");
    check(vfs_resolve(&fs, "/../..") == 0, "an absolute one lands on the root too");

    printf("extra and trailing slashes make no difference\n");
    check(vfs_resolve(&fs, "//HOME//JJ//") == vfs_resolve(&fs, "/HOME/JJ"),
          "however many there are");
    check(vfs_resolve(&fs, "/HOME/") == vfs_resolve(&fs, "/HOME"),
          "including one on the end");
}

static void test_files_hold_what_was_put_in_them(void)
{
    printf("writing, reading, appending and replacing\n");
    vfs_init(&fs);
    char text[VFS_FILE_MAX + 1];
    uint64_t length = 0;

    check(vfs_write(&fs, "/NOTES", "HELLO") == VFS_OK, "writing makes the file");
    check(vfs_read(&fs, "/NOTES", text, sizeof text, &length) == VFS_OK, "and reads back");
    check(strcmp(text, "HELLO") == 0, "what was written");
    check(length == 5, "with the right length");

    check(vfs_append(&fs, "/NOTES", " THERE") == VFS_OK, "appending adds to the end");
    vfs_read(&fs, "/NOTES", text, sizeof text, &length);
    check(strcmp(text, "HELLO THERE") == 0, "rather than replacing");

    check(vfs_write(&fs, "/NOTES", "AGAIN") == VFS_OK, "writing again");
    vfs_read(&fs, "/NOTES", text, sizeof text, &length);
    check(strcmp(text, "AGAIN") == 0, "replaces what was there");
    check(length == 5, "and the old length is gone with it");

    printf("an empty file is empty rather than absent\n");
    check(vfs_create(&fs, "/BLANK") == VFS_OK, "a file with nothing in it");
    check(vfs_read(&fs, "/BLANK", text, sizeof text, &length) == VFS_OK, "reads");
    check(length == 0 && text[0] == '\0', "as nothing at all");

    printf("more than a file can hold is refused rather than cut\n");
    char big[VFS_FILE_MAX + 40];
    for (size_t i = 0; i < sizeof big - 1; i++) {
        big[i] = 'X';
    }
    big[sizeof big - 1] = '\0';
    check(vfs_write(&fs, "/BIG", big) == VFS_TOO_BIG, "the write is refused");
    vfs_read(&fs, "/BIG", text, sizeof text, &length);
    check(length == 0, "and the file was left empty rather than half filled");

    /* A file that quietly held less than it was given would be a file whose
     * contents nobody could trust, which is worse than one that will not take
     * them. */
    check(vfs_write(&fs, "/BIG", "SHORT") == VFS_OK, "a small write still works");
    for (int i = 0; i < 100; i++) {
        vfs_append(&fs, "/BIG", "PADDING");
    }
    vfs_read(&fs, "/BIG", text, sizeof text, &length);
    check(length <= VFS_FILE_MAX, "and appending stops at the limit");
}

static void test_the_wrong_kind_of_thing_is_refused(void)
{
    printf("a file is not a directory and a directory is not a file\n");
    vfs_init(&fs);
    vfs_mkdir(&fs, "/D");
    vfs_write(&fs, "/F", "TEXT");
    char text[VFS_FILE_MAX + 1];

    check(vfs_chdir(&fs, "/F") == VFS_NOT_A_DIRECTORY, "you cannot go into a file");
    check(vfs_read(&fs, "/D", text, sizeof text, NULL) == VFS_IS_A_DIRECTORY,
          "nor read a directory as one");
    check(vfs_write(&fs, "/D", "X") == VFS_IS_A_DIRECTORY, "nor write over one");
    check(vfs_resolve(&fs, "/F/INSIDE") == VFS_NONE,
          "and nothing lives inside a file");

    printf("names that are already taken, or unusable, are refused\n");
    check(vfs_mkdir(&fs, "/D") == VFS_EXISTS, "a directory that is already there");
    check(vfs_create(&fs, "/F") == VFS_EXISTS, "and a file that is");
    check(vfs_mkdir(&fs, "/D/.") == VFS_BAD_NAME, "a single dot is not a name");
    check(vfs_mkdir(&fs, "/D/..") == VFS_BAD_NAME, "and nor are two");
    check(vfs_mkdir(&fs, "/NOWHERE/DEEP") == VFS_NOT_FOUND,
          "and nothing can be made inside a directory that is not there");

    char long_name[VFS_PATH_MAX] = "/";
    for (size_t i = 1; i < VFS_NAME_MAX + 4; i++) {
        long_name[i] = 'N';
    }
    long_name[VFS_NAME_MAX + 4] = '\0';
    check(vfs_mkdir(&fs, long_name) == VFS_BAD_NAME, "a name too long is refused");

    printf("reading or entering something that is not there says so\n");
    check(vfs_read(&fs, "/GONE", text, sizeof text, NULL) == VFS_NOT_FOUND, "a read");
    check(vfs_chdir(&fs, "/GONE") == VFS_NOT_FOUND, "a move");
    check(vfs_remove(&fs, "/GONE") == VFS_NOT_FOUND, "and a delete");
}

static void test_removing(void)
{
    printf("removing takes one entry out and leaves the rest linked\n");
    vfs_init(&fs);
    vfs_create(&fs, "/A");
    vfs_create(&fs, "/B");
    vfs_create(&fs, "/C");

    check(vfs_remove(&fs, "/B") == VFS_OK, "the middle one goes");
    check(vfs_resolve(&fs, "/B") == VFS_NONE, "and is gone");
    check(vfs_resolve(&fs, "/A") != VFS_NONE, "the one before it is still there");
    check(vfs_resolve(&fs, "/C") != VFS_NONE, "and so is the one after");
    check(vfs_count_children(&fs, 0) == 2, "two are left");

    check(vfs_remove(&fs, "/A") == VFS_OK, "the first one goes");
    check(vfs_resolve(&fs, "/C") != VFS_NONE, "and the list still holds C");
    check(vfs_count_children(&fs, 0) == 1, "one is left");

    printf("the space is given back, so a full filesystem can be emptied\n");
    const uint64_t before = vfs_used_nodes(&fs);
    vfs_create(&fs, "/D");
    vfs_remove(&fs, "/D");
    check(vfs_used_nodes(&fs) == before, "making and removing costs nothing");

    printf("a directory with things in it is refused rather than emptied\n");
    vfs_init(&fs);
    vfs_mkdir(&fs, "/D");
    vfs_create(&fs, "/D/INSIDE");
    check(vfs_remove(&fs, "/D") == VFS_NOT_EMPTY, "the directory stays");
    check(vfs_resolve(&fs, "/D/INSIDE") != VFS_NONE, "and so does what is in it");
    check(vfs_remove(&fs, "/D/INSIDE") == VFS_OK, "empty it first");
    check(vfs_remove(&fs, "/D") == VFS_OK, "and then it goes");

    printf("the root is not something to delete\n");
    check(vfs_remove(&fs, "/") == VFS_BAD_NAME, "it is refused");
    check(vfs_resolve(&fs, "/") == 0, "and it is still there");

    /* The one that would corrupt the whole filesystem. Deleting the directory
     * you are standing in leaves every relative path resolving into a node
     * nothing points at. */
    printf("the directory you are standing in cannot be pulled out from under you\n");
    vfs_init(&fs);
    vfs_mkdir(&fs, "/HOME");
    vfs_mkdir(&fs, "/HOME/JJ");
    vfs_chdir(&fs, "/HOME/JJ");
    check(vfs_remove(&fs, "/HOME/JJ") == VFS_NOT_EMPTY, "the one you are in");
    check(vfs_remove(&fs, "/HOME") == VFS_NOT_EMPTY, "and any above it");
    check(strcmp(where(), "/HOME/JJ") == 0, "so where you are is still real");
    check(vfs_chdir(&fs, "/") == VFS_OK, "step out");
    check(vfs_remove(&fs, "/HOME/JJ") == VFS_OK, "and then it can go");
}

static void test_the_filesystem_fills_up_honestly(void)
{
    printf("a full filesystem says so rather than losing an entry\n");
    vfs_init(&fs);
    char name[16];
    int made = 0;
    for (int i = 0; i < VFS_MAX_NODES + 10; i++) {
        name[0] = '/';
        name[1] = (char)('A' + (i / 26));
        name[2] = (char)('A' + (i % 26));
        name[3] = (char)('0' + (i % 10));
        name[4] = '\0';
        if (vfs_create(&fs, name) == VFS_OK) {
            made++;
        }
    }
    check(made == VFS_MAX_NODES - 1, "every entry but the root was used");
    check(vfs_create(&fs, "/ONEMORE") == VFS_NO_SPACE, "and the next is refused");
    check(vfs_used_nodes(&fs) == VFS_MAX_NODES, "with nothing lost");
}

static void test_nonsense_is_refused(void)
{
    printf("no filesystem and no path are refused rather than followed\n");
    vfs_init(&fs);
    check(vfs_resolve(NULL, "/") == VFS_NONE, "no filesystem");
    check(vfs_resolve(&fs, NULL) == VFS_NONE, "no path");
    check(vfs_mkdir(NULL, "/A") == VFS_BAD_NAME, "no filesystem to make in");
    check(vfs_mkdir(&fs, NULL) == VFS_BAD_NAME, "and nothing to make");
    check(vfs_get(&fs, -5) == NULL, "a node before the start");
    check(vfs_get(&fs, VFS_MAX_NODES + 3) == NULL, "and one past the end");
    check(vfs_read(&fs, "/", NULL, 10, NULL) == VFS_BAD_NAME, "nowhere to read into");

    char path[4];
    check(vfs_path_of(&fs, 0, path, sizeof path) == 1, "a short buffer holds a slash");
    check(vfs_path_of(&fs, 0, NULL, 10) == 0, "and no buffer holds nothing");
    vfs_mkdir(&fs, "/AVERYLONGNAMEINDEED");
    check(vfs_path_of(&fs, vfs_resolve(&fs, "/AVERYLONGNAMEINDEED"),
                      path, sizeof path) == 0,
          "a path too long for the buffer is refused rather than cut");

    printf("every result has a sentence a person can act on\n");
    const enum vfs_result all[] = {
        VFS_OK, VFS_NOT_FOUND, VFS_EXISTS, VFS_NOT_A_DIRECTORY, VFS_IS_A_DIRECTORY,
        VFS_NOT_EMPTY, VFS_NO_SPACE, VFS_TOO_BIG, VFS_BAD_NAME,
    };
    bool all_explained = true;
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        if (vfs_explain(all[i])[0] == '\0') {
            all_explained = false;
        }
    }
    check(all_explained, "and not one of them is blank");
}

static void test_moving_and_copying(void)
{
    printf("moving an entry takes it out of one place and puts it in another\n");
    vfs_init(&fs);
    vfs_mkdir(&fs, "/A");
    vfs_mkdir(&fs, "/B");
    vfs_write(&fs, "/A/NOTE.TXT", "CONTENT");

    check(vfs_move(&fs, "/A/NOTE.TXT", "/B/NOTE.TXT") == VFS_OK, "the move works");
    check(vfs_resolve(&fs, "/A/NOTE.TXT") == VFS_NONE, "it is gone from A");
    check(vfs_resolve(&fs, "/B/NOTE.TXT") != VFS_NONE, "and is in B");
    char text[VFS_FILE_MAX + 1];
    vfs_read(&fs, "/B/NOTE.TXT", text, sizeof text, NULL);
    check(strcmp(text, "CONTENT") == 0, "with what was in it");
    check(vfs_count_children(&fs, vfs_resolve(&fs, "/A")) == 0, "A is empty now");

    printf("a move within one directory is a rename\n");
    check(vfs_move(&fs, "/B/NOTE.TXT", "/B/RENAMED.TXT") == VFS_OK, "renaming works");
    check(vfs_resolve(&fs, "/B/RENAMED.TXT") != VFS_NONE, "under the new name");
    check(vfs_resolve(&fs, "/B/NOTE.TXT") == VFS_NONE, "and not the old one");

    printf("a move onto something that is already there is refused\n");
    vfs_write(&fs, "/A/OTHER.TXT", "X");
    check(vfs_move(&fs, "/A/OTHER.TXT", "/B/RENAMED.TXT") == VFS_EXISTS,
          "so nothing is quietly replaced");
    check(vfs_resolve(&fs, "/A/OTHER.TXT") != VFS_NONE, "and the source stays");

    /* The one that would take a whole subtree out of the tree and leave it
     * pointing at its own parent, where nothing could ever reach it again. */
    printf("a directory cannot be moved inside itself\n");
    vfs_mkdir(&fs, "/A/DEEP");
    check(vfs_move(&fs, "/A", "/A/DEEP/A") == VFS_BAD_NAME, "it is refused");
    check(vfs_resolve(&fs, "/A") != VFS_NONE, "and A is still where it was");
    check(vfs_resolve(&fs, "/A/DEEP") != VFS_NONE, "with what was under it");
    check(vfs_move(&fs, "/A", "/A/A") == VFS_BAD_NAME, "nor directly into itself");
    check(vfs_move(&fs, "/", "/A/ROOT") == VFS_BAD_NAME, "and the root moves nowhere");

    printf("moving something that is not there says so\n");
    check(vfs_move(&fs, "/GONE", "/B/GONE") == VFS_NOT_FOUND, "the source");
    check(vfs_move(&fs, "/A/OTHER.TXT", "/NOWHERE/X") == VFS_NOT_FOUND,
          "and the destination directory");

    printf("copying a file makes a second one with the same contents\n");
    vfs_init(&fs);
    vfs_write(&fs, "/ONE", "THE SAME");
    check(vfs_copy(&fs, "/ONE", "/TWO") == VFS_OK, "the copy works");
    vfs_read(&fs, "/TWO", text, sizeof text, NULL);
    check(strcmp(text, "THE SAME") == 0, "with the contents copied");
    vfs_read(&fs, "/ONE", text, sizeof text, NULL);
    check(strcmp(text, "THE SAME") == 0, "and the original untouched");

    printf("and the copy is its own file afterwards\n");
    vfs_write(&fs, "/TWO", "CHANGED");
    vfs_read(&fs, "/ONE", text, sizeof text, NULL);
    check(strcmp(text, "THE SAME") == 0, "changing one does not change the other");

    printf("copying a directory, or onto something, is refused\n");
    vfs_mkdir(&fs, "/D");
    check(vfs_copy(&fs, "/D", "/D2") == VFS_IS_A_DIRECTORY, "a directory");
    check(vfs_copy(&fs, "/ONE", "/TWO") == VFS_EXISTS, "and onto a file that exists");
    check(vfs_copy(&fs, "/GONE", "/X") == VFS_NOT_FOUND, "and one that is not there");
}

/* The counter the automatic save reads. What matters is not that it counts, but
 * that a refused operation does not move it. If it did, every mistyped command
 * would write the whole filesystem to the disk for nothing, and worse, a
 * command that failed would look from the outside exactly like one that
 * worked. */
static void test_only_real_changes_are_counted(void)
{
    printf("every change moves the counter and nothing else does\n");
    vfs_init(&fs);
    check(fs.changes == 0, "a new filesystem has changed nothing");

    const uint32_t start = fs.changes;
    check(vfs_mkdir(&fs, "/WORK") == VFS_OK, "making a directory");
    check(fs.changes == start + 1, "  counts once");
    check(vfs_create(&fs, "/WORK/A.TXT") == VFS_OK, "making a file");
    check(fs.changes == start + 2, "  counts once");
    check(vfs_write(&fs, "/WORK/A.TXT", "HELLO") == VFS_OK, "writing to it");
    check(fs.changes > start + 2, "  counts");
    uint32_t at = fs.changes;
    check(vfs_append(&fs, "/WORK/A.TXT", " AGAIN") == VFS_OK, "appending");
    check(fs.changes == at + 1, "  counts once");
    at = fs.changes;
    check(vfs_move(&fs, "/WORK/A.TXT", "/WORK/B.TXT") == VFS_OK, "moving it");
    check(fs.changes == at + 1, "  counts once");
    at = fs.changes;
    check(vfs_copy(&fs, "/WORK/B.TXT", "/WORK/C.TXT") == VFS_OK, "copying it");
    check(fs.changes > at, "  counts");
    at = fs.changes;
    check(vfs_remove(&fs, "/WORK/C.TXT") == VFS_OK, "removing it");
    check(fs.changes == at + 1, "  counts once");

    printf("and a command that was refused changed nothing to save\n");
    at = fs.changes;
    check(vfs_mkdir(&fs, "/WORK") == VFS_EXISTS, "a directory that is there");
    check(vfs_remove(&fs, "/NOWHERE") == VFS_NOT_FOUND, "removing nothing");
    check(vfs_remove(&fs, "/WORK") == VFS_NOT_EMPTY, "removing a full directory");
    check(vfs_move(&fs, "/NOWHERE", "/SOMEWHERE") == VFS_NOT_FOUND, "moving nothing");
    check(vfs_copy(&fs, "/WORK", "/COPY") == VFS_IS_A_DIRECTORY, "copying a directory");
    check(vfs_write(&fs, "/WORK", "TEXT") == VFS_IS_A_DIRECTORY, "writing to one");
    check(vfs_mkdir(&fs, "/WORK/") == VFS_EXISTS, "a trailing slash on one");
    check(vfs_mkdir(&fs, "/THIS-NAME-IS-FAR-TOO-LONG-TO-FIT-IN-A-NODE") == VFS_BAD_NAME,
          "a name too long to hold");
    check(fs.changes == at, "none of them moved the counter");

    printf("nor does looking at it\n");
    char out[VFS_FILE_MAX + 1];
    check(vfs_read(&fs, "/WORK/B.TXT", out, sizeof out, NULL) == VFS_OK, "reading");
    check(vfs_chdir(&fs, "/WORK") == VFS_OK, "or moving about in it");
    check(fs.changes == at, "so the disk is not written for nothing");
}

/* M24. Files are made of blocks now, and the interesting cases are the ones
 * where the pool runs out partway rather than cleanly. */
static void test_files_are_made_of_blocks(void)
{
    printf("a file takes only the blocks it needs\n");
    vfs_init(&fs);
    check(vfs_used_blocks(&fs) == 0, "an empty filesystem holds no blocks");
    check(vfs_create(&fs, "/A.TXT") == VFS_OK, "an empty file");
    check(vfs_used_blocks(&fs) == 0, "takes no room at all");

    check(vfs_write(&fs, "/A.TXT", "SHORT") == VFS_OK, "a few bytes");
    check(vfs_used_blocks(&fs) == 1, "take one block");

    char big[VFS_BLOCK + 100];
    for (uint64_t i = 0; i < sizeof big - 1; i++) {
        big[i] = 'X';
    }
    big[sizeof big - 1] = '\0';
    check(vfs_write(&fs, "/A.TXT", big) == VFS_OK, "more than one block fits");
    check(vfs_used_blocks(&fs) == 2, "and takes exactly two");

    printf("rewriting a big file with a small one gives the room back\n");
    check(vfs_write(&fs, "/A.TXT", "SHORT AGAIN") == VFS_OK, "written smaller");
    check(vfs_used_blocks(&fs) == 1, "so it is back to one block");

    printf("and deleting it gives all of it back\n");
    check(vfs_remove(&fs, "/A.TXT") == VFS_OK, "removed");
    check(vfs_used_blocks(&fs) == 0, "the pool is empty again");

    printf("a file longer than one block reads back byte for byte\n");
    vfs_init(&fs);
    char pattern[VFS_BLOCK * 3 + 7];
    for (uint64_t i = 0; i < sizeof pattern - 1; i++) {
        pattern[i] = (char)('A' + (i % 26));
    }
    pattern[sizeof pattern - 1] = '\0';
    check(vfs_write(&fs, "/LONG.TXT", pattern) == VFS_OK, "written");
    char out[VFS_FILE_MAX + 1];
    uint64_t length = 0;
    check(vfs_read(&fs, "/LONG.TXT", out, sizeof out, &length) == VFS_OK, "read back");
    check(length == sizeof pattern - 1, "the same length");
    check(memcmp(out, pattern, length) == 0, "and the same bytes across every block");

    printf("appending across a block boundary keeps what was already there\n");
    vfs_init(&fs);
    char nearly[VFS_BLOCK - 3];
    for (uint64_t i = 0; i < sizeof nearly - 1; i++) {
        nearly[i] = 'Q';
    }
    nearly[sizeof nearly - 1] = '\0';
    check(vfs_write(&fs, "/EDGE.TXT", nearly) == VFS_OK, "a file just short of a block");
    check(vfs_used_blocks(&fs) == 1, "in one block");
    check(vfs_append(&fs, "/EDGE.TXT", "ABCDEF") == VFS_OK, "appended over the join");
    check(vfs_used_blocks(&fs) == 2, "which took a second block");
    check(vfs_read(&fs, "/EDGE.TXT", out, sizeof out, &length) == VFS_OK, "read back");
    check(length == sizeof nearly - 1 + 6, "the whole length");
    check(memcmp(out, nearly, sizeof nearly - 1) == 0, "the first part is untouched");
    check(memcmp(out + sizeof nearly - 1, "ABCDEF", 6) == 0, "and the rest followed it");

    printf("a block handed out again does not show the last file's bytes\n");
    vfs_init(&fs);
    check(vfs_write(&fs, "/OLD.TXT", "PINEAPPLE") == VFS_OK, "a file");
    check(vfs_remove(&fs, "/OLD.TXT") == VFS_OK, "deleted");
    check(vfs_write(&fs, "/NEW.TXT", "HI") == VFS_OK, "and another in its place");
    check(vfs_read(&fs, "/NEW.TXT", out, sizeof out, &length) == VFS_OK, "read");
    check(length == 2 && memcmp(out, "HI", 2) == 0, "which holds only its own");
}

/* The one that matters. A file that asked for three blocks and got two has a
 * hole in the middle of it, and nothing downstream can tell. */

/* Fills the pool until exactly `leave` blocks are free.
 *
 * With large files, deliberately. Filling it with one block files runs the node
 * table out at ninety six long before the pool runs out at two hundred and
 * fifty six, so the test would be checking the wrong limit and would pass
 * whatever the allocator did.
 */
static void fill_the_pool(uint64_t leave)
{
    static char slab[VFS_FILE_MAX + 1];
    for (uint64_t i = 0; i < VFS_FILE_MAX; i++) {
        slab[i] = 'F';
    }
    slab[VFS_FILE_MAX] = '\0';

    int made = 0;
    while (VFS_MAX_BLOCKS - vfs_used_blocks(&fs) > leave) {
        const uint64_t free_now = VFS_MAX_BLOCKS - vfs_used_blocks(&fs) - leave;
        const uint64_t want = free_now > VFS_DIRECT_BLOCKS ? VFS_DIRECT_BLOCKS
                                                           : free_now;
        char name[VFS_NAME_MAX];
        snprintf(name, sizeof name, "/F%d", made++);
        slab[want * VFS_BLOCK] = '\0';
        const enum vfs_result done = vfs_write(&fs, name, slab);
        slab[want * VFS_BLOCK] = 'F';
        if (done != VFS_OK) {
            break;
        }
    }
}

static void test_running_out_of_room_changes_nothing(void)
{
    printf("a write the pool cannot cover leaves the file as it was\n");
    vfs_init(&fs);
    check(vfs_write(&fs, "/KEEP.TXT", "ORIGINAL") == VFS_OK, "a file with something in it");

    fill_the_pool(1);
    check(VFS_MAX_BLOCKS - vfs_used_blocks(&fs) == 1, "one block is left");

    char wants_more[VFS_BLOCK * 4];
    for (uint64_t i = 0; i < sizeof wants_more - 1; i++) {
        wants_more[i] = 'Z';
    }
    wants_more[sizeof wants_more - 1] = '\0';

    const uint64_t before = vfs_used_blocks(&fs);
    check(vfs_append(&fs, "/KEEP.TXT", wants_more) == VFS_NO_SPACE,
          "a write bigger than what is left is refused");
    check(vfs_used_blocks(&fs) == before,
          "and every block it took on the way is given back");

    char out[VFS_FILE_MAX + 1];
    uint64_t length = 0;
    check(vfs_read(&fs, "/KEEP.TXT", out, sizeof out, &length) == VFS_OK, "the file reads");
    check(length == 8 && memcmp(out, "ORIGINAL", 8) == 0,
          "and holds exactly what it did before");

    printf("what the last free block will hold still fits\n");
    check(vfs_append(&fs, "/KEEP.TXT", "MORE") == VFS_OK, "a small append");
    check(vfs_used_blocks(&fs) == before, "in the room the file already had");

    printf("a copy that will not fit leaves no half copy behind\n");
    fill_the_pool(0);
    check(vfs_used_blocks(&fs) == VFS_MAX_BLOCKS, "the pool is full");
    check(vfs_copy(&fs, "/KEEP.TXT", "/COPY.TXT") == VFS_NO_SPACE,
          "copying with no room is refused");
    check(vfs_resolve(&fs, "/COPY.TXT") == VFS_NONE,
          "and the name it would have taken is not there either");
    check(vfs_used_blocks(&fs) == VFS_MAX_BLOCKS, "with nothing lost either way");

    printf("a file bigger than a file can be is refused before anything moves\n");
    vfs_init(&fs);
    static char too_big[VFS_FILE_MAX + 2];
    for (uint64_t i = 0; i < sizeof too_big - 1; i++) {
        too_big[i] = 'Y';
    }
    too_big[sizeof too_big - 1] = '\0';
    check(vfs_write(&fs, "/HUGE.TXT", too_big) == VFS_TOO_BIG, "refused");
    check(vfs_used_blocks(&fs) == 0, "having taken nothing");

    printf("a file exactly as big as a file can be fits\n");
    too_big[VFS_FILE_MAX] = '\0';
    check(vfs_write(&fs, "/FULL.TXT", too_big) == VFS_OK, "written");
    check(vfs_used_blocks(&fs) == VFS_DIRECT_BLOCKS, "using every block it may");
}

int main(void)
{
    test_an_empty_filesystem_is_a_root_and_nothing_else();
    test_making_and_walking_a_tree();
    test_relative_paths_and_dot_dot();
    test_files_hold_what_was_put_in_them();
    test_the_wrong_kind_of_thing_is_refused();
    test_removing();
    test_moving_and_copying();
    test_the_filesystem_fills_up_honestly();
    test_nonsense_is_refused();
    test_only_real_changes_are_counted();
    test_files_are_made_of_blocks();
    test_running_out_of_room_changes_nothing();

    if (failures > 0) {
        printf("\n%d filesystem check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nfilesystem checks passed\n");
    return 0;
}
