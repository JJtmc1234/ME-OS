/* Host tests for the M19 terminal and its commands.
 *
 * A terminal is worth testing where it is easy to get wrong and hard to see: the
 * scroll that drops the oldest line, the backspace that must not eat the prompt,
 * the wrap at the right edge, and the size reported for a machine with a lot of
 * memory. None of that needs a framebuffer.
 */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "term.h"
#include "vfs.h"

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

/* One row of the grid as a string, with the trailing blanks removed. */
static const char *row_of(struct term *term, uint32_t row, char *out)
{
    uint32_t end = term->cols;
    while (end > 0 && term->cells[row][end - 1] == ' ') {
        end--;
    }
    for (uint32_t i = 0; i < end; i++) {
        out[i] = term->cells[row][i];
    }
    out[end] = '\0';
    return out;
}

static void type(struct term *term, const char *text)
{
    for (uint64_t i = 0; text[i] != '\0'; i++) {
        term_key(term, text[i], false, false, NULL, 0);
    }
}

static void test_text_lands_where_it_was_put(void)
{
    printf("printing puts text on the grid and moves the cursor down\n");
    struct term term;
    char row[TERM_MAX_COLS + 1];
    term_init(&term, 20, 4);

    term_println(&term, "FIRST");
    term_println(&term, "SECOND");
    check(strcmp(row_of(&term, 0, row), "FIRST") == 0, "the first line");
    check(strcmp(row_of(&term, 1, row), "SECOND") == 0, "and the second under it");
    check(term.row == 2, "the cursor moved down twice");

    printf("a newline inside a string breaks the line\n");
    term_init(&term, 20, 4);
    term_print(&term, "ONE\nTWO");
    check(strcmp(row_of(&term, 0, row), "ONE") == 0, "before the break");
    check(strcmp(row_of(&term, 1, row), "TWO") == 0, "and after it");
}

static void test_a_long_line_wraps_rather_than_vanishing(void)
{
    printf("text past the right edge wraps onto the next row\n");
    struct term term;
    char row[TERM_MAX_COLS + 1];
    term_init(&term, 8, 4);

    term_print(&term, "ABCDEFGHIJKL");
    check(strcmp(row_of(&term, 0, row), "ABCDEFGH") == 0, "the first eight");
    check(strcmp(row_of(&term, 1, row), "IJKL") == 0, "and the rest below");
}

static void test_the_grid_scrolls_when_it_is_full(void)
{
    printf("a full grid scrolls, so the newest line is always visible\n");
    struct term term;
    char row[TERM_MAX_COLS + 1];
    term_init(&term, 20, 3);

    term_println(&term, "ONE");
    term_println(&term, "TWO");
    term_println(&term, "THREE");
    term_println(&term, "FOUR");

    check(strcmp(row_of(&term, 0, row), "TWO") == 0, "the oldest line is gone");
    check(strcmp(row_of(&term, 1, row), "THREE") == 0, "the rest moved up");
    check(strcmp(row_of(&term, 2, row), "FOUR") == 0, "and the newest is at the bottom");

    printf("clearing empties every row and puts the cursor back\n");
    term_clear(&term);
    check(strcmp(row_of(&term, 0, row), "") == 0, "the top row is blank");
    check(strcmp(row_of(&term, 2, row), "") == 0, "and so is the bottom");
    check(term.row == 0 && term.column == 0, "the cursor is back at the start");
}

static void test_the_line_editor(void)
{
    printf("typing builds a line and Enter hands it over\n");
    struct term term;
    char line[TERM_INPUT_MAX];
    term_init(&term, 40, 6);

    type(&term, "HELP");
    check(term.input_length == 4, "four characters are held");
    check(!term_key(&term, 0, false, false, line, sizeof line),
          "an empty key completes nothing");
    check(term_key(&term, 0, true, false, line, sizeof line), "Enter completes it");
    check(strcmp(line, "HELP") == 0, "and the line is what was typed");
    check(term.input_length == 0, "the input is emptied afterwards");

    printf("backspace deletes a character and stops at the start\n");
    type(&term, "AB");
    term_key(&term, 0, false, true, NULL, 0);
    check(term.input_length == 1, "one character was removed");
    term_key(&term, 0, false, true, NULL, 0);
    term_key(&term, 0, false, true, NULL, 0);
    term_key(&term, 0, false, true, NULL, 0);
    check(term.input_length == 0, "and it cannot go below empty");

    printf("the prompt line is the prompt plus what has been typed\n");
    char prompt[TERM_MAX_COLS + 1];
    type(&term, "VER");
    const uint64_t length = term_prompt_line(&term, prompt, sizeof prompt);
    check(strcmp(prompt, "ME> VER") == 0, "so the drawing and a test agree");
    check(length == 7, "and its length is reported");

    printf("a character the font cannot draw is refused rather than stored\n");
    term_key(&term, 0, true, false, line, sizeof line);
    check(!term_key(&term, '~', false, false, NULL, 0), "a tilde is not taken");
    check(term.input_length == 0, "so nothing was stored");
    check(term_key(&term, 'A', false, false, NULL, 0) == false &&
          term.input_length == 1, "and a letter still is");

    printf("the input cannot be overrun\n");
    term_key(&term, 0, true, false, line, sizeof line);
    for (int i = 0; i < TERM_INPUT_MAX + 50; i++) {
        term_key(&term, 'X', false, false, NULL, 0);
    }
    check(term.input_length < TERM_INPUT_MAX, "it stops at its own limit");
    term_key(&term, 0, true, false, line, sizeof line);
    check(strlen(line) == term.cols || strlen(line) < TERM_INPUT_MAX,
          "and the completed line is bounded");
}

static void test_resizing_keeps_the_state_sensible(void)
{
    printf("a terminal in a tile too small still has a consistent size\n");
    struct term term;
    term_init(&term, 0, 0);
    check(term.cols >= 1 && term.rows >= 1, "at least one column and one row");

    term_init(&term, 100000, 100000);
    check(term.cols <= TERM_MAX_COLS && term.rows <= TERM_MAX_ROWS,
          "and never more than the grid holds");

    printf("shrinking pulls the cursor back inside\n");
    term_init(&term, 40, 10);
    for (int i = 0; i < 9; i++) {
        term_newline(&term);
    }
    check(term.row == 9, "the cursor is on the last row");
    term_resize(&term, 10, 4);
    check(term.row < term.rows, "and is inside the smaller grid");
    check(term.column < term.cols, "on both axes");

    printf("the size that fits a client area leaves room for the prompt\n");
    check(term_rows_for(200) * 10 < 200, "the rows do not fill the height");
    check(term_cols_for(0) >= 1, "a zero width still gives one column");
    check(term_rows_for(0) >= 1, "and a zero height one row");
}

static void test_the_commands_answer_with_what_the_machine_knows(void)
{
    printf("splitting a line finds the command and its arguments\n");
    char name[32];
    const char *rest = NULL;
    check(cmd_split("  echo hello there ", name, sizeof name, &rest) == 4,
          "the command word is four characters");
    check(strcmp(name, "ECHO") == 0, "and is uppercased");
    check(strcmp(rest, "hello there ") == 0, "the rest is left alone");
    check(cmd_split("", name, sizeof name, &rest) == 0, "an empty line has none");
    check(cmd_split(NULL, name, sizeof name, &rest) == 0, "and nor has no line");

    /* A command longer than the buffer must not spill into the arguments. */
    char tiny[4];
    check(cmd_split("ABCDEFGH xyz", tiny, sizeof tiny, &rest) == 3,
          "a long command is cut to fit");
    check(strcmp(rest, "xyz") == 0, "and the arguments are still the arguments");

    printf("sizes are reported in units a person can read\n");
    char size[32];
    cmd_format_size(0, size, sizeof size);
    check(strcmp(size, "0 B") == 0, "nothing is nothing");
    cmd_format_size(512, size, sizeof size);
    check(strcmp(size, "512 B") == 0, "small sizes stay in bytes");
    cmd_format_size(1024, size, sizeof size);
    check(strcmp(size, "1.0 KB") == 0, "a kilobyte");
    cmd_format_size(536870912, size, sizeof size);
    check(strcmp(size, "512.0 MB") == 0, "half a gigabyte");
    cmd_format_size(1536u * 1024u * 1024u, size, sizeof size);
    check(strcmp(size, "1.5 GB") == 0, "and one and a half");

    printf("every command prints something true\n");
    struct term term;
    term_init(&term, 60, 20);
    struct cmd_out output;
    cmd_out_to_term(&output, &term);
    struct cmd_context context = {
        .out = &output,
        .term = &term,
        .uptime_seconds = 3725,
        .screen_width = 1280,
        .screen_height = 800,
        .usable_memory = 500u * 1024u * 1024u,
        .total_memory = 512u * 1024u * 1024u,
        .windows_open = 4,
        .windows_visible = 2,
        .cpu_vendor = "GenuineIntel",
        .cpu_brand = "A PROCESSOR",
        .version = "0.19",
    };

    char row[TERM_MAX_COLS + 1];
    cmd_run(&context, "VER");
    check(strcmp(row_of(&term, 1, row), "ME OS 0.19") == 0, "VER says which one");

    term_clear(&term);
    cmd_run(&context, "RES");
    check(strcmp(row_of(&term, 1, row), "1280X800 32 BPP") == 0, "RES says the size");

    term_clear(&term);
    cmd_run(&context, "UPTIME");
    check(strcmp(row_of(&term, 1, row), "UP 1H 2M 5S") == 0, "UPTIME counts properly");

    term_clear(&term);
    cmd_run(&context, "CPU");
    check(strcmp(row_of(&term, 1, row), "VENDOR  GenuineIntel") == 0, "CPU asks the chip");

    term_clear(&term);
    cmd_run(&context, "MEM");
    check(strcmp(row_of(&term, 1, row), "USABLE  500.0 MB") == 0, "MEM says how much");

    term_clear(&term);
    cmd_run(&context, "WINDOWS");
    check(strcmp(row_of(&term, 1, row), "OPEN    4") == 0, "WINDOWS counts them");
    check(strcmp(row_of(&term, 2, row), "SHOWING 2") == 0, "and how many are showing");

    term_clear(&term);
    cmd_run(&context, "ECHO HELLO");
    check(strcmp(row_of(&term, 1, row), "HELLO") == 0, "ECHO says it back");

    printf("a command is echoed above its own output\n");
    term_clear(&term);
    cmd_run(&context, "VER");
    check(strcmp(row_of(&term, 0, row), "ME> VER") == 0,
          "so the history reads as a conversation");

    printf("an unknown command says so rather than doing nothing\n");
    term_clear(&term);
    cmd_run(&context, "SUDO");
    check(strstr(row_of(&term, 1, row), "NO SUCH COMMAND") != NULL, "it is named");
    check(strstr(row_of(&term, 1, row), "SUDO") != NULL, "and quoted back");

    printf("a machine that could not answer says so rather than printing zero\n");
    term_clear(&term);
    context.total_memory = 0;
    context.usable_memory = 0;
    cmd_run(&context, "MEM");
    check(strstr(row_of(&term, 1, row), "NO MEMORY MAP") != NULL, "MEM is honest");

    term_clear(&term);
    context.cpu_vendor = "";
    context.cpu_brand = "";
    cmd_run(&context, "CPU");
    check(strstr(row_of(&term, 1, row), "WOULD NOT SAY") != NULL, "and so is CPU");

    printf("an empty line and a missing terminal are harmless\n");
    term_clear(&term);
    cmd_run(&context, "");
    cmd_run(&context, "    ");
    cmd_run(NULL, "VER");
    struct cmd_context empty = {0};
    cmd_run(&empty, "VER");
    check(strcmp(row_of(&term, 0, row), "ME>") == 0,
          "an empty line still shows the prompt it was typed at");
    check(strcmp(row_of(&term, 2, row), "") == 0, "and nothing else was printed");
}

/* M20. The file commands do the real thing to the real filesystem, so what is
 * checked is what ended up in the tree, not only what was printed. */
static void test_the_file_commands(void)
{
    printf("the shell moves around a real tree\n");
    struct term term;
    struct vfs fs;
    char row[TERM_MAX_COLS + 1];
    term_init(&term, 60, 24);
    vfs_init(&fs);

    struct cmd_out output;
    cmd_out_to_term(&output, &term);
    struct cmd_context context = {
        .out = &output, .term = &term, .fs = &fs, .version = "0.20" };

    cmd_run(&context, "PWD");
    check(strcmp(row_of(&term, 1, row), "/") == 0, "PWD starts at the root");

    term_clear(&term);
    cmd_run(&context, "MKDIR HOME");
    check(vfs_resolve(&fs, "/HOME") != VFS_NONE, "MKDIR made the directory");

    term_clear(&term);
    cmd_run(&context, "CD HOME");
    check(fs.cwd == vfs_resolve(&fs, "/HOME"), "CD moved there");
    check(strcmp(row_of(&term, 1, row), "/HOME") == 0, "and said where it went");

    term_clear(&term);
    cmd_run(&context, "TOUCH NOTES.TXT");
    check(vfs_resolve(&fs, "/HOME/NOTES.TXT") != VFS_NONE,
          "TOUCH made a file where we are, not at the root");

    printf("writing and reading a file back\n");
    term_clear(&term);
    cmd_run(&context, "WRITE NOTES.TXT HELLO THERE");
    char text[VFS_FILE_MAX + 1];
    vfs_read(&fs, "/HOME/NOTES.TXT", text, sizeof text, NULL);
    check(strcmp(text, "HELLO THERE") == 0, "WRITE keeps the spaces in the line");

    term_clear(&term);
    cmd_run(&context, "CAT NOTES.TXT");
    check(strcmp(row_of(&term, 1, row), "HELLO THERE") == 0, "CAT shows it");

    printf("an arrow writes to a file instead of printing\n");
    term_clear(&term);
    cmd_run(&context, "ECHO REDIRECTED > OUT.TXT");
    vfs_read(&fs, "/HOME/OUT.TXT", text, sizeof text, NULL);
    check(strcmp(text, "REDIRECTED") == 0, "the text went into the file");
    check(strcmp(row_of(&term, 1, row), "") == 0, "and not onto the screen");

    printf("listing shows what is there and counts it\n");
    term_clear(&term);
    cmd_run(&context, "LS");
    check(strstr(row_of(&term, 1, row), "NOTES.TXT") != NULL, "the first file");
    check(strstr(row_of(&term, 2, row), "OUT.TXT") != NULL, "and the second");
    check(strstr(row_of(&term, 3, row), "2 FILES") != NULL, "with a count");

    term_clear(&term);
    cmd_run(&context, "LS /NOWHERE");
    check(strstr(row_of(&term, 1, row), "NO SUCH FILE") != NULL,
          "a listing of nothing says so");

    printf("removing, and refusing to remove\n");
    term_clear(&term);
    cmd_run(&context, "RM NOTES.TXT");
    check(vfs_resolve(&fs, "/HOME/NOTES.TXT") == VFS_NONE, "RM took the file away");

    term_clear(&term);
    cmd_run(&context, "CD /");
    cmd_run(&context, "RM HOME");
    check(vfs_resolve(&fs, "/HOME") != VFS_NONE, "a directory with a file in it stays");
    check(strstr(row_of(&term, 3, row), "NOT EMPTY") != NULL, "and says why");

    printf("an empty directory lists as empty rather than as nothing\n");
    term_clear(&term);
    cmd_run(&context, "MKDIR TMP");
    cmd_run(&context, "LS TMP");
    check(strstr(row_of(&term, 2, row), "EMPTY") != NULL, "it says so");

    printf("DF says how much room there is and that none of it lasts\n");
    term_clear(&term);
    cmd_run(&context, "DF");
    bool warned = false;
    for (uint32_t y = 0; y < 8; y++) {
        if (strstr(row_of(&term, y, row), "SURVIVES A RESTART") != NULL) {
            warned = true;
        }
    }
    check(warned, "the one surprising thing about it is said every time");

    printf("with no filesystem the file commands are not offered at all\n");
    term_clear(&term);
    struct cmd_out bare_out;
    cmd_out_to_term(&bare_out, &term);
    struct cmd_context bare = {
        .out = &bare_out, .term = &term, .version = "0.20" };
    cmd_run(&bare, "PWD");
    check(strstr(row_of(&term, 1, row), "NO SUCH COMMAND") != NULL,
          "PWD is unknown rather than crashing");
}

static void test_redirection_is_split_correctly(void)
{
    printf("an arrow is found and both sides are trimmed\n");
    char command[64];
    char target[64];

    check(cmd_split_redirect("ECHO HI > NOTES", command, sizeof command,
                             target, sizeof target),
          "a line with an arrow");
    check(strcmp(command, "ECHO HI") == 0, "the command has no trailing space");
    check(strcmp(target, "NOTES") == 0, "and the name no leading one");

    check(cmd_split_redirect("ECHO HI>NOTES", command, sizeof command,
                             target, sizeof target),
          "with no spaces at all");
    check(strcmp(command, "ECHO HI") == 0, "it still splits");
    check(strcmp(target, "NOTES") == 0, "on both sides");

    check(!cmd_split_redirect("ECHO HI", command, sizeof command,
                              target, sizeof target),
          "a line with no arrow is not a redirection");
    check(!cmd_split_redirect("ECHO HI >", command, sizeof command,
                              target, sizeof target),
          "and nor is an arrow with nothing after it");
    check(!cmd_split_redirect("> NOTES", command, sizeof command,
                              target, sizeof target),
          "or one with nothing before it");
    check(!cmd_split_redirect(NULL, command, sizeof command, target, sizeof target),
          "no line at all is refused");
}

/* M25. Output goes somewhere a command does not know about, which is what makes
 * both the arrow and the bar work for every command rather than only for ECHO.
 */
static void test_pipes_and_general_redirection(void)
{
    struct term term;
    struct vfs fs;
    char row[TERM_MAX_COLS + 1];
    char text[VFS_FILE_MAX + 1];
    term_init(&term, 60, 24);
    vfs_init(&fs);
    struct cmd_out output;
    cmd_out_to_term(&output, &term);
    struct cmd_context context = {
        .out = &output, .term = &term, .fs = &fs, .version = "0.25" };

    printf("a sink writes to a buffer instead of a screen\n");
    char small[8];
    struct cmd_out into;
    cmd_out_to_buffer(&into, small, sizeof small);
    cmd_print(&into, "ABC");
    check(strcmp(cmd_out_text(&into), "ABC") == 0, "what was written is there");
    check(!into.overflowed, "and it fitted");
    cmd_print(&into, "DEFGHIJKL");
    check(into.overflowed, "more than fits is reported");
    check(strlen(cmd_out_text(&into)) == sizeof small - 1,
          "and what is there is terminated rather than running off the end");

    printf("any command can be written to a file, not only ECHO\n");
    cmd_run(&context, "MKDIR WORK");
    cmd_run(&context, "CD WORK");
    cmd_run(&context, "WRITE PEAR.TXT A PEAR");
    cmd_run(&context, "WRITE APPLE.TXT AN APPLE");
    term_clear(&term);
    cmd_run(&context, "LS > LISTING.TXT");
    check(vfs_read(&fs, "LISTING.TXT", text, sizeof text, NULL) == VFS_OK,
          "the listing went into a file");
    check(strstr(text, "PEAR.TXT") != NULL, "holding what LS would have shown");
    check(strstr(text, "APPLE.TXT") != NULL, "all of it");
    check(strcmp(row_of(&term, 1, row), "") == 0, "and nothing went to the screen");

    printf("and what lands in the file has no newline stuck on the end\n");
    term_clear(&term);
    cmd_run(&context, "ECHO HELLO > ONE.TXT");
    check(vfs_read(&fs, "ONE.TXT", text, sizeof text, NULL) == VFS_OK, "written");
    check(strcmp(text, "HELLO") == 0, "exactly what was said, and no more");

    printf("a bar hands one command's output to the next\n");
    term_clear(&term);
    cmd_run(&context, "LS | GREP PEAR");
    check(strstr(row_of(&term, 1, row), "PEAR.TXT") != NULL, "the line that matched");
    check(strstr(row_of(&term, 2, row), "APPLE.TXT") == NULL, "and not the ones that did not");

    printf("a filter reads a file when it is given one\n");
    term_clear(&term);
    cmd_run(&context, "GREP PEAR LISTING.TXT");
    check(strstr(row_of(&term, 1, row), "PEAR.TXT") != NULL, "the same answer");

    printf("and says so when nothing matched rather than printing nothing\n");
    term_clear(&term);
    cmd_run(&context, "GREP BANANA LISTING.TXT");
    check(strstr(row_of(&term, 1, row), "NOTHING MATCHED") != NULL, "it says so");

    printf("three stages work, and the arrow applies to the whole line\n");
    cmd_run(&context, "WRITE LIST.TXT ZEBRA");
    term_clear(&term);
    cmd_run(&context, "CAT LIST.TXT | GREP ZEBRA | HEAD 1 > FOUND.TXT");
    check(vfs_read(&fs, "FOUND.TXT", text, sizeof text, NULL) == VFS_OK,
          "the far end of the pipe went into the file");
    check(strcmp(text, "ZEBRA") == 0, "holding what came out of it");

    printf("HEAD and TAIL take a count, or ten when nobody says\n");
    check(vfs_write(&fs, "MANY.TXT", "L1\nL2\nL3\nL4\nL5") == VFS_OK, "five lines");
    term_clear(&term);
    cmd_run(&context, "HEAD 2 MANY.TXT");
    check(strcmp(row_of(&term, 1, row), "L1") == 0, "the first");
    check(strcmp(row_of(&term, 2, row), "L2") == 0, "and the second");
    check(strcmp(row_of(&term, 3, row), "") == 0, "and no more");

    term_clear(&term);
    cmd_run(&context, "TAIL 2 MANY.TXT");
    check(strcmp(row_of(&term, 1, row), "L4") == 0, "the last but one");
    check(strcmp(row_of(&term, 2, row), "L5") == 0, "and the last");

    term_clear(&term);
    cmd_run(&context, "HEAD MANY.TXT");
    check(strcmp(row_of(&term, 5, row), "L5") == 0,
          "with no count it shows them all, since there are fewer than ten");

    printf("SORT puts lines in order whatever order they arrived in\n");
    check(vfs_write(&fs, "WORDS.TXT", "PEAR\nAPPLE\nMANGO") == VFS_OK, "three words");
    term_clear(&term);
    cmd_run(&context, "SORT WORDS.TXT");
    check(strcmp(row_of(&term, 1, row), "APPLE") == 0, "first");
    check(strcmp(row_of(&term, 2, row), "MANGO") == 0, "second");
    check(strcmp(row_of(&term, 3, row), "PEAR") == 0, "third");

    printf("a filter given neither a file nor a pipe says so\n");
    term_clear(&term);
    cmd_run(&context, "GREP SOMETHING");
    check(strstr(row_of(&term, 1, row), "NEEDS A NAME") != NULL, "rather than nothing");

    printf("CLEAR still reaches the screen even though it writes nothing\n");
    cmd_run(&context, "ECHO SOMETHING TO WIPE");
    cmd_run(&context, "CLEAR");
    check(strcmp(row_of(&term, 0, row), "") == 0, "the screen is empty");

    printf("an arrow with nothing after it is refused rather than guessed at\n");
    term_clear(&term);
    cmd_run(&context, "LS >");
    check(strstr(row_of(&term, 1, row), "AN ARROW NEEDS") != NULL, "it says so");
    check(strstr(row_of(&term, 1, row), "NO SUCH FILE") == NULL,
          "rather than reporting a file called an arrow");
    term_clear(&term);
    cmd_run(&context, "> NAME.TXT");
    check(strstr(row_of(&term, 1, row), "AN ARROW NEEDS") != NULL,
          "and the same with nothing in front of it");

    printf("splitting a line at the bar is exact about the blanks\n");
    char first[TERM_INPUT_MAX];
    const char *rest = NULL;
    check(cmd_split_pipe("LS | GREP TXT", first, sizeof first, &rest), "there is a bar");
    check(strcmp(first, "LS") == 0, "the part before it, trimmed");
    check(strcmp(rest, "GREP TXT") == 0, "and the part after it, trimmed");
    check(!cmd_split_pipe("LS", first, sizeof first, &rest), "a line with no bar");
}

int main(void)
{
    test_text_lands_where_it_was_put();
    test_a_long_line_wraps_rather_than_vanishing();
    test_the_grid_scrolls_when_it_is_full();
    test_the_line_editor();
    test_resizing_keeps_the_state_sensible();
    test_the_commands_answer_with_what_the_machine_knows();
    test_redirection_is_split_correctly();
    test_the_file_commands();
    test_pipes_and_general_redirection();

    if (failures > 0) {
        printf("\n%d terminal check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nterminal and shell checks passed\n");
    return 0;
}
