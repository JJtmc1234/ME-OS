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

int main(void)
{
    test_an_empty_filesystem_is_a_root_and_nothing_else();
    test_making_and_walking_a_tree();
    test_relative_paths_and_dot_dot();
    test_files_hold_what_was_put_in_them();
    test_the_wrong_kind_of_thing_is_refused();
    test_removing();
    test_the_filesystem_fills_up_honestly();
    test_nonsense_is_refused();

    if (failures > 0) {
        printf("\n%d filesystem check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nfilesystem checks passed\n");
    return 0;
}
