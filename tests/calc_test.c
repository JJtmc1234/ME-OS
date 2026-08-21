/* Host unit test for M6's arithmetic.
 *
 * The whole milestone is a pure function on a small piece of state, so all of
 * it can be checked here: parsing, evaluation, overflow, formatting, and what
 * each key does to the line on screen. The emulator only has to confirm that
 * the same code is reached from a real keyboard.
 */
#include <stdio.h>
#include <string.h>

#include "calc.h"

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

static int evaluates(const char *text, int64_t expected)
{
    int64_t out = 0;
    if (!calc_evaluate(text, (uint8_t)strlen(text), &out)) {
        return 0;
    }
    return out == expected;
}

static int refuses(const char *text)
{
    int64_t out = 12345;
    return !calc_evaluate(text, (uint8_t)strlen(text), &out) && out == 12345;
}

static void type(struct calc *calc, const char *keys)
{
    for (size_t i = 0; keys[i] != '\0'; i++) {
        calc_key(calc, keys[i]);
    }
}

static int line_is(struct calc *calc, const char *expected)
{
    char line[64];
    calc_line(calc, line, sizeof line);
    if (strcmp(line, expected) == 0) {
        return 1;
    }
    printf("        line was \"%s\", expected \"%s\"\n", line, expected);
    return 0;
}

static void test_multiplication_and_division(void)
{
    printf("calc_evaluate multiplies and divides\n");
    check(evaluates("6*7", 42), "multiplication");
    check(evaluates("84/2", 42), "division");
    check(evaluates("2*3*7", 42), "several multiplications");
    check(evaluates("-6*7", -42), "a negative operand");
    check(evaluates("6*-7", -42), "a negative on the right, typed after the operator");
    check(evaluates("-6*-7", 42), "two negatives");

    printf("division keeps whole numbers, truncating towards zero\n");
    check(evaluates("7/2", 3), "a positive remainder is dropped");
    check(evaluates("-7/2", -3), "a negative truncates towards zero, not downwards");
    check(evaluates("7/-2", -3), "a negative divisor");
    check(evaluates("1/2", 0), "a result smaller than one");

    printf("dividing by zero is refused rather than faulting\n");
    check(refuses("1/0"), "one divided by zero");
    check(refuses("0/0"), "zero divided by zero");
    check(refuses("5*3/0"), "a division by zero later in the sum");
    check(evaluates("-9223372036854775807-1/-1", -9223372036854775806LL),
          "the divide binds tighter than the subtraction, so this is not an overflow");

    printf("precedence works the way it does on paper\n");
    check(evaluates("2+3*4", 14), "multiply before add");
    check(evaluates("2*3+4", 10), "multiply before add, the other way round");
    check(evaluates("10-6/2", 7), "divide before subtract");
    check(evaluates("2*3-4*5", -14), "two products either side of a subtraction");
    check(evaluates("100/10/2", 5), "division is left to right");
    check(evaluates("100-10-2", 88), "subtraction is left to right");
}

static void test_powers(void)
{
    printf("calc_evaluate raises to a power\n");
    check(evaluates("2^5", 32), "a small power");
    check(evaluates("10^3", 1000), "a power of ten");
    check(evaluates("7^1", 7), "to the power of one");
    check(evaluates("7^0", 1), "anything to the power of zero is one");
    check(evaluates("0^0", 1), "including zero");
    check(evaluates("0^5", 0), "zero to a positive power");
    check(evaluates("-2^2", -4), "a leading sign binds looser than the power");
    check(evaluates("2^3^2", 512), "powers are right associative, so 2^(3^2)");

    printf("powers combine with everything else at the right precedence\n");
    check(evaluates("2*3^2", 18), "power before multiply");
    check(evaluates("1+2^3", 9), "power before add");
    check(evaluates("2^3*2", 16), "power before multiply, the other way round");
    check(evaluates("100/10^2", 1), "power before divide");

    printf("a power that is not a whole number is refused\n");
    check(refuses("2^-1"), "a negative power");
    check(refuses("1^-1"), "even where the answer would be whole");

    printf("a power that would not fit is refused\n");
    check(refuses("2^63"), "just past the largest value");
    check(refuses("10^19"), "a power of ten too large to hold");
    check(evaluates("2^62", 4611686018427387904LL), "the largest power of two that fits");
    check(refuses("9999^9999"), "an absurd power, without hanging on it");
}

static void test_overflow(void)
{
    printf("every operation refuses to wrap\n");
    check(refuses("9223372036854775807+1"), "addition past the largest value");
    check(refuses("-9223372036854775807-2"), "subtraction past the most negative");
    check(refuses("9223372036854775807*2"), "multiplication past the largest value");
    check(refuses("4611686018427387904*2"), "a product one bit too wide");
    check(refuses("-9223372036854775807-1-1"), "one step past the most negative value");

    printf("values right at the edge still work\n");
    check(evaluates("9223372036854775807", INT64_MAX), "the largest value");
    check(evaluates("-9223372036854775807-1", INT64_MIN), "the most negative value");
    check(evaluates("9223372036854775807-1", INT64_MAX - 1), "one below the largest");
    check(evaluates("4611686018427387903*2", 9223372036854775806LL),
          "the largest product that fits");
    check(evaluates("-9223372036854775807-1+1", INT64_MIN + 1),
          "recovering from the most negative value");
}

static void test_evaluation(void)
{
    printf("calc_evaluate adds and subtracts\n");
    check(evaluates("7", 7), "a single number");
    check(evaluates("12+30", 42), "addition");
    check(evaluates("50-8", 42), "subtraction");
    check(evaluates("1+2+3+4", 10), "several additions");
    check(evaluates("100-1-1", 98), "several subtractions");
    check(evaluates("10+5-3+2", 14), "mixed, left to right");
    check(evaluates("8-9", -1), "a negative result");
    check(evaluates("0-0", 0), "zero");
    check(evaluates("007+3", 10), "leading zeros");

    printf("a leading sign belongs to the first number\n");
    check(evaluates("-5", -5), "a negative number on its own");
    check(evaluates("-5+8", 3), "a negative first term");
    check(evaluates("-5-5", -10), "negative, then subtract");
    check(evaluates("+5", 5), "an explicit plus");

    printf("calc_evaluate refuses what it cannot read\n");
    check(refuses(""), "nothing at all");
    check(refuses("+"), "a sign with no number");
    check(refuses("5+"), "a trailing operator");
    check(evaluates("5++3", 8), "a plus then a unary plus");
    check(evaluates("5+-3", 2), "a plus then a unary minus");
    check(refuses("5A3"), "a letter in the middle");
    check(refuses(" 5"), "a leading space");
    check(refuses("5*"), "a trailing multiply");
    check(refuses("*5"), "a leading multiply");
    check(refuses("^5"), "a leading power");
    check(refuses("5**3"), "two multiplies in a row");
    check(refuses("5//3"), "two divides in a row");

    printf("calc_evaluate refuses results that would not fit\n");
    check(refuses("99999999999999999999"), "a number too large to hold");
    check(evaluates("9223372036854775807", INT64_MAX), "the largest value that fits");
    check(refuses("9223372036854775807+1"), "one past the largest value");
    check(refuses("9223372036854775807+9223372036854775807"), "adding two huge values");
    check(evaluates("-9223372036854775807-1", INT64_MIN), "the most negative value");

    int64_t out = 0;
    check(!calc_evaluate(NULL, 3, &out), "a null expression");
    check(!calc_evaluate("1+1", 3, NULL), "nowhere to put the answer");
    check(!calc_evaluate("1+1", 0, &out), "a length of zero");
}

static void test_formatting(void)
{
    char buffer[32];

    printf("calc_format writes numbers the screen can show\n");
    calc_format(0, buffer, sizeof buffer);
    check(strcmp(buffer, "0") == 0, "zero");
    calc_format(42, buffer, sizeof buffer);
    check(strcmp(buffer, "42") == 0, "a positive number");
    calc_format(-42, buffer, sizeof buffer);
    check(strcmp(buffer, "-42") == 0, "a negative number");
    calc_format(INT64_MAX, buffer, sizeof buffer);
    check(strcmp(buffer, "9223372036854775807") == 0, "the largest value");
    calc_format(INT64_MIN, buffer, sizeof buffer);
    check(strcmp(buffer, "-9223372036854775808") == 0,
          "the most negative value, which cannot simply be negated");

    printf("calc_format stays inside a small buffer\n");
    char small[4];
    memset(small, 'x', sizeof small);
    size_t written = calc_format(123456, small, sizeof small);
    check(written == 3 && small[3] == '\0', "truncates rather than overrunning");
    check(calc_format(1, NULL, 8) == 0, "no buffer");
    check(calc_format(1, small, 0) == 0, "a buffer with no room");
}

static void test_typing(void)
{
    struct calc calc;

    printf("typing builds a sum\n");
    calc_reset(&calc);
    check(line_is(&calc, "TYPE A SUM"), "an empty calculator prompts");

    type(&calc, "12+30");
    check(line_is(&calc, "12+30"), "the sum as typed");

    calc_key(&calc, CALC_EVALUATE);
    check(line_is(&calc, "12+30=42"), "the result appears after evaluating");
    check(calc.has_result && calc.result == 42 && !calc.error, "and is recorded");

    printf("typing after a result starts a new sum\n");
    calc_key(&calc, '7');
    check(line_is(&calc, "7"), "the old sum is gone");

    printf("editing\n");
    calc_reset(&calc);
    type(&calc, "45");
    calc_key(&calc, CALC_DELETE);
    check(line_is(&calc, "4"), "backspace removes the last character");
    calc_key(&calc, CALC_DELETE);
    check(line_is(&calc, "TYPE A SUM"), "deleting everything returns to the prompt");
    check(!calc_key(&calc, CALC_DELETE), "deleting nothing changes nothing");

    calc_reset(&calc);
    type(&calc, "9-3");
    calc_key(&calc, CALC_CLEAR);
    check(line_is(&calc, "TYPE A SUM"), "clear empties the line");
    check(!calc_key(&calc, CALC_CLEAR), "clearing an empty line changes nothing");

    printf("the calculator refuses keys that would spoil the sum\n");
    calc_reset(&calc);
    type(&calc, "5");
    check(calc_key(&calc, '+'), "an operator after a number is allowed");
    check(!calc_key(&calc, '-'), "a sign after a sign is refused");
    check(line_is(&calc, "5+"), "and does not change the line");
    check(!calc_key(&calc, 'A'), "a letter is refused");
    check(!calc_key(&calc, ' '), "a space is refused");

    printf("a sign may follow a multiply, divide or power\n");
    calc_reset(&calc);
    type(&calc, "5*-3");
    check(line_is(&calc, "5*-3"), "a negative operand can be typed");
    calc_key(&calc, CALC_EVALUATE);
    check(line_is(&calc, "5*-3=-15"), "and evaluates");
    calc_reset(&calc);
    type(&calc, "5*");
    check(!calc_key(&calc, '*'), "but a second multiply is still refused");

    printf("dividing by zero shows an error rather than crashing\n");
    calc_reset(&calc);
    type(&calc, "8/0");
    calc_key(&calc, CALC_EVALUATE);
    check(line_is(&calc, "8/0=ERROR"), "the line says so");
    check(calc.error && !calc.has_result, "and it is recorded as an error");

    printf("a power shows its answer\n");
    calc_reset(&calc);
    type(&calc, "2^5");
    calc_key(&calc, CALC_EVALUATE);
    check(line_is(&calc, "2^5=32"), "two to the fifth");

    printf("an unreadable sum says so\n");
    calc_reset(&calc);
    type(&calc, "5+");
    calc_key(&calc, CALC_EVALUATE);
    check(line_is(&calc, "5+=ERROR"), "a trailing operator is an error");
    check(calc.error && !calc.has_result, "and is recorded as one");

    printf("the input has a limit\n");
    calc_reset(&calc);
    for (int i = 0; i < CALC_MAX_INPUT + 10; i++) {
        calc_key(&calc, '1');
    }
    check(calc.length == CALC_MAX_INPUT, "typing stops at the limit");
    check(!calc_key(&calc, '1'), "and further keys change nothing");

    printf("a whole negative sum\n");
    calc_reset(&calc);
    type(&calc, "-8+3");
    calc_key(&calc, CALC_EVALUATE);
    check(line_is(&calc, "-8+3=-5"), "negative input and negative result");

    printf("nothing crashes on a null calculator\n");
    check(!calc_key(NULL, '1'), "a null calculator");
    char line[8];
    calc_line(NULL, line, sizeof line);
    check(line[0] == '\0', "a null calculator has an empty line");
    calc_reset(NULL);
}

int main(void)
{
    test_evaluation();
    test_multiplication_and_division();
    test_powers();
    test_overflow();
    test_formatting();
    test_typing();

    if (failures > 0) {
        printf("\n%d arithmetic check(s) FAILED\n", failures);
        return 1;
    }
    printf("\narithmetic checks passed\n");
    return 0;
}
