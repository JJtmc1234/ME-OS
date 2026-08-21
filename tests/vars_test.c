/* Host unit test for M8's variable table.
 *
 * The table is a fixed array with a linear search and no allocation, so all of
 * it runs here. What the emulator has to show is only that the same table is
 * reached from a real keyboard.
 */
#include <stdio.h>
#include <string.h>

#include "vars.h"

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

static int holds(const struct vars *vars, const char *name, int64_t expected)
{
    int64_t out = 0;
    return vars_get(vars, name, &out) && out == expected;
}

static void test_storing_and_reading(void)
{
    struct vars vars;
    vars_reset(&vars);

    printf("an empty table knows nothing\n");
    check(vars.count == 0, "no names are stored");
    check(!holds(&vars, "X", 0), "reading a name that was never set fails");

    printf("a value can be stored and read back\n");
    check(vars_set(&vars, "X", 5), "storing X");
    check(holds(&vars, "X", 5), "X is 5");
    check(vars.count == 1, "one slot is used");

    printf("storing the same name again replaces it\n");
    check(vars_set(&vars, "X", 42), "storing X again");
    check(holds(&vars, "X", 42), "X is 42 now");
    check(vars.count == 1, "and it did not take a second slot");

    printf("different names are different values\n");
    check(vars_set(&vars, "Y", 7), "storing Y");
    check(holds(&vars, "X", 42) && holds(&vars, "Y", 7), "both are kept");
    check(vars.count == 2, "two slots are used");

    printf("names are compared in full and exactly\n");
    check(vars_set(&vars, "X2", 9), "storing X2");
    check(holds(&vars, "X", 42), "X is untouched");
    check(holds(&vars, "X2", 9), "X2 is its own name");
    check(!holds(&vars, "x", 42), "lowercase x is a different name, and unknown");

    printf("the whole range of values fits\n");
    check(vars_set(&vars, "BIG", INT64_MAX) && holds(&vars, "BIG", INT64_MAX),
          "the largest value");
    check(vars_set(&vars, "NEG", INT64_MIN) && holds(&vars, "NEG", INT64_MIN),
          "the most negative value");
    check(vars_set(&vars, "Z", 0) && holds(&vars, "Z", 0), "zero");

    printf("resetting forgets everything\n");
    vars_reset(&vars);
    check(vars.count == 0 && !holds(&vars, "X", 42), "X is unknown again");
}

static void test_full_table(void)
{
    struct vars vars;
    vars_reset(&vars);

    printf("the table holds exactly VARS_MAX names\n");
    char name[3] = { 'A', '\0', '\0' };
    for (int i = 0; i < VARS_MAX; i++) {
        name[0] = (char)('A' + i);
        check(vars_set(&vars, name, i), "storing a name while there is room");
    }
    check(vars.count == VARS_MAX, "every slot is used");

    printf("a name too many is refused, and nothing is thrown away\n");
    check(!vars_set(&vars, "ZZ", 99), "the ninth name is refused");
    check(!holds(&vars, "ZZ", 99), "and was not stored");
    check(vars.count == VARS_MAX, "the count did not grow");
    check(holds(&vars, "A", 0) && holds(&vars, "H", VARS_MAX - 1),
          "the names already stored are untouched");

    printf("a full table can still be overwritten\n");
    check(vars_set(&vars, "A", 100), "an existing name needs no new slot");
    check(holds(&vars, "A", 100), "and takes the new value");
}

static void test_bad_names(void)
{
    struct vars vars;
    vars_reset(&vars);

    printf("a name that will not fit in a slot is refused\n");
    char too_long[VARS_MAX_NAME + 2];
    memset(too_long, 'A', sizeof too_long - 1);
    too_long[sizeof too_long - 1] = '\0';
    check(!vars_set(&vars, too_long, 1), "one character too long");
    check(vars.count == 0, "and took no slot");

    char longest[VARS_MAX_NAME + 1];
    memset(longest, 'B', sizeof longest - 1);
    longest[sizeof longest - 1] = '\0';
    check(vars_set(&vars, longest, 2), "the longest name that does fit");
    check(holds(&vars, longest, 2), "and reads back");

    printf("an empty name is not a name\n");
    check(!vars_set(&vars, "", 1), "storing nothing");
    check(!holds(&vars, "", 1), "reading nothing");

    printf("nothing crashes on a null table or a null name\n");
    int64_t out = 0;
    check(!vars_set(NULL, "X", 1), "a null table cannot be written");
    check(!vars_get(NULL, "X", &out), "a null table knows nothing");
    check(!vars_set(&vars, NULL, 1), "a null name cannot be written");
    check(!vars_get(&vars, NULL, &out), "a null name cannot be read");
    check(!vars_get(&vars, "B", NULL), "nowhere to put the answer");
    vars_reset(NULL);
    check(1, "resetting a null table does nothing");
}

int main(void)
{
    test_storing_and_reading();
    test_full_table();
    test_bad_names();

    if (failures > 0) {
        printf("\n%d variable check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nvariable checks passed\n");
    return 0;
}
