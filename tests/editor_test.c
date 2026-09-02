/* Host tests for the M21 text editor.
 *
 * The parts worth checking are the ones a person would notice within a minute
 * and a compiler never will: typing in the middle of a line has to insert rather
 * than overwrite, Enter has to leave the head of the line where it was and take
 * the tail down with it, backspace at the start of a line has to join it to the
 * one above, and moving down from a long line onto a short one must not leave
 * the cursor in space past the end of it.
 *
 * The round trip matters most. Text that goes in has to come out the same, or
 * saving a file makes it worse than it was.
 */
#include <stdio.h>
#include <string.h>

#include "editor.h"

static int failures;
static struct editor ed;

static void check(int condition, const char *what)
{
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

static void type(const char *text)
{
    for (uint64_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            editor_newline(&ed);
        } else {
            editor_insert(&ed, text[i]);
        }
    }
}

static const char *whole(void)
{
    static char out[EDIT_MAX_LINES * (EDIT_MAX_COLS + 2)];
    editor_text(&ed, out, sizeof out, NULL);
    return out;
}

static void test_an_empty_editor_is_usable(void)
{
    printf("a new editor holds one empty line\n");
    editor_init(&ed);
    check(ed.line_count == 1, "one line");
    check(ed.lines[0][0] == '\0', "and it is empty");
    check(ed.cursor_line == 0 && ed.cursor_column == 0, "with the cursor on it");
    check(!ed.changed, "and nothing changed yet");
    check(strcmp(whole(), "") == 0, "so it reads back as nothing");

    /* Everything below assumes there is a line to work on. A document with no
     * lines would need every operation to check for it. */
    editor_backspace(&ed);
    editor_move(&ed, -5, -5);
    check(ed.line_count == 1, "and it survives being asked to go nowhere");
}

static void test_typing_and_the_round_trip(void)
{
    printf("what is typed comes back out the same\n");
    editor_init(&ed);
    type("FIRST\nSECOND\nTHIRD");
    check(ed.line_count == 3, "three lines");
    check(strcmp(whole(), "FIRST\nSECOND\nTHIRD") == 0, "and they read back whole");
    check(ed.changed, "and the document is marked changed");

    printf("loading replaces everything and forgets the changes\n");
    editor_load(&ed, "ONE\nTWO");
    check(ed.line_count == 2, "two lines now");
    check(strcmp(whole(), "ONE\nTWO") == 0, "and only those");
    check(!ed.changed, "a freshly opened file is not changed");
    check(ed.cursor_line == 0 && ed.cursor_column == 0, "with the cursor at the top");

    printf("loading keeps the name of the file it is editing\n");
    editor_set_path(&ed, "/HOME/NOTES.TXT");
    editor_load(&ed, "ANYTHING");
    check(strcmp(ed.path, "/HOME/NOTES.TXT") == 0, "so saving goes back to it");

    printf("an empty file and a file of empty lines both survive\n");
    editor_load(&ed, "");
    check(ed.line_count == 1 && strcmp(whole(), "") == 0, "nothing stays nothing");
    editor_load(&ed, "\n\n");
    check(ed.line_count == 3, "two newlines make three lines");
    check(strcmp(whole(), "\n\n") == 0, "and come back the same");
}

static void test_typing_in_the_middle_inserts(void)
{
    printf("typing in the middle of a line inserts rather than overwrites\n");
    editor_init(&ed);
    type("HELLO");
    editor_move(&ed, -2, 0);
    check(ed.cursor_column == 3, "the cursor moved back two");
    type("XY");
    check(strcmp(whole(), "HELXYLO") == 0, "and the rest of the line moved right");

    printf("backspace in the middle takes out one character\n");
    editor_backspace(&ed);
    check(strcmp(whole(), "HELXLO") == 0, "the one before the cursor");
    check(ed.cursor_column == 4, "and the cursor followed it");

    printf("a character the font cannot draw is not taken\n");
    editor_init(&ed);
    type("OK");
    editor_insert(&ed, '~');
    check(strcmp(whole(), "OK") == 0, "so the file holds nothing unprintable");

    printf("a full line refuses more rather than writing past its end\n");
    editor_init(&ed);
    for (int i = 0; i < EDIT_MAX_COLS + 20; i++) {
        editor_insert(&ed, 'X');
    }
    check(strlen(ed.lines[0]) == EDIT_MAX_COLS, "it stops at the width");
    check(strstr(ed.status, "FULL") != NULL, "and says why");
}

static void test_enter_splits_a_line_the_right_way_round(void)
{
    printf("Enter leaves the head where it was and takes the tail down\n");
    editor_init(&ed);
    type("HELLOWORLD");
    editor_move(&ed, -5, 0);
    editor_newline(&ed);
    check(ed.line_count == 2, "there are two lines");
    check(strcmp(ed.lines[0], "HELLO") == 0, "the head stayed above");
    check(strcmp(ed.lines[1], "WORLD") == 0, "and the tail went below");
    check(ed.cursor_line == 1 && ed.cursor_column == 0,
          "with the cursor at the start of the new line");

    printf("Enter in the middle of a document pushes the lines below it down\n");
    editor_load(&ed, "A\nB\nC");
    editor_move(&ed, 0, 1);
    editor_end(&ed);
    editor_newline(&ed);
    check(strcmp(whole(), "A\nB\n\nC") == 0, "and nothing below is lost");

    printf("backspace at the start of a line joins it to the one above\n");
    editor_load(&ed, "ONE\nTWO");
    editor_move(&ed, 0, 1);
    editor_home(&ed);
    editor_backspace(&ed);
    check(ed.line_count == 1, "one line now");
    check(strcmp(whole(), "ONETWO") == 0, "joined in the right order");
    check(ed.cursor_column == 3, "with the cursor where the join is");

    printf("and at the very start there is nothing to join to\n");
    editor_home(&ed);
    editor_move(&ed, 0, -5);
    editor_backspace(&ed);
    check(strcmp(whole(), "ONETWO") == 0, "so nothing happens");

    printf("two lines that will not fit as one are refused rather than cut\n");
    editor_init(&ed);
    for (int i = 0; i < EDIT_MAX_COLS - 2; i++) {
        editor_insert(&ed, 'A');
    }
    editor_newline(&ed);
    for (int i = 0; i < 10; i++) {
        editor_insert(&ed, 'B');
    }
    editor_home(&ed);
    editor_backspace(&ed);
    check(ed.line_count == 2, "the two lines stay two");
    check(strstr(ed.status, "WILL NOT FIT") != NULL, "and it says why");
}

static void test_the_cursor_stays_somewhere_real(void)
{
    printf("the cursor never sits past the end of the line it is on\n");
    editor_load(&ed, "AVERYLONGLINE\nAB");
    editor_end(&ed);
    check(ed.cursor_column == 13, "at the end of the long line");
    editor_move(&ed, 0, 1);
    check(ed.cursor_column == 2, "and pulled in when it moves onto a short one");

    printf("it cannot move above the first line or below the last\n");
    editor_move(&ed, 0, -50);
    check(ed.cursor_line == 0, "the top holds it");
    editor_move(&ed, 0, 50);
    check(ed.cursor_line == ed.line_count - 1, "and so does the bottom");

    printf("nor before the first column\n");
    editor_home(&ed);
    editor_move(&ed, -50, 0);
    check(ed.cursor_column == 0, "the left edge holds it");

    printf("the view follows the cursor rather than the other way round\n");
    editor_init(&ed);
    editor_fit(&ed, 400, 60);   /* a few lines tall */
    const uint32_t visible = ed.visible_lines;
    check(visible >= 1, "at least one line is visible");
    for (uint32_t i = 0; i < visible + 5; i++) {
        editor_newline(&ed);
    }
    check(ed.cursor_line >= ed.top_line, "the cursor is not above the view");
    check(ed.cursor_line < ed.top_line + ed.visible_lines,
          "and not below it either");

    editor_move(&ed, 0, -100);
    check(ed.top_line == 0, "going back to the top scrolls the view back");
}

static void test_the_limits_are_honest(void)
{
    printf("a file with more lines than fit is cut and says so\n");
    char big[EDIT_MAX_LINES * 4 + 40];
    uint64_t at = 0;
    for (int i = 0; i < EDIT_MAX_LINES + 8; i++) {
        big[at++] = 'L';
        big[at++] = '\n';
    }
    big[at] = '\0';
    editor_load(&ed, big);
    check(ed.line_count <= EDIT_MAX_LINES, "it holds no more than it can");
    check(strstr(ed.status, "DID NOT ALL FIT") != NULL, "and says so plainly");

    printf("a document at its line limit refuses another rather than losing one\n");
    editor_init(&ed);
    for (int i = 0; i < EDIT_MAX_LINES + 5; i++) {
        editor_newline(&ed);
    }
    check(ed.line_count == EDIT_MAX_LINES, "it stops at the limit");
    check(strstr(ed.status, "AS MANY LINES") != NULL, "and says why");

    printf("text that does not fit the buffer it is written into is reported\n");
    editor_load(&ed, "ONE\nTWO\nTHREE");
    char small[6];
    bool complete = true;
    const uint64_t written = editor_text(&ed, small, sizeof small, &complete);
    check(!complete, "the caller is told it did not all fit");
    check(written < sizeof small, "and what was written is terminated");

    char plenty[256];
    check(editor_text(&ed, plenty, sizeof plenty, &complete) == 13, "a big enough one");
    check(complete, "says it is complete");
    check(editor_text(NULL, plenty, sizeof plenty, NULL) == 0, "no editor gives none");
    check(editor_text(&ed, NULL, 10, NULL) == 0, "and nowhere to write gives none");
}

int main(void)
{
    test_an_empty_editor_is_usable();
    test_typing_and_the_round_trip();
    test_typing_in_the_middle_inserts();
    test_enter_splits_a_line_the_right_way_round();
    test_the_cursor_stays_somewhere_real();
    test_the_limits_are_honest();

    if (failures > 0) {
        printf("\n%d editor check(s) FAILED\n", failures);
        return 1;
    }
    printf("\ntext editor checks passed\n");
    return 0;
}
