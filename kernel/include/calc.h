/* Integer addition and subtraction, typed in and shown on screen.
 *
 * All of it is arithmetic on a small piece of state: no framebuffer, no ports,
 * no allocation. That is what lets the whole milestone be tested on an
 * ordinary machine, and it is why the kernel side of M6 is a dozen lines.
 *
 * Addition, subtraction, multiplication, whole number division and powers,
 * with the precedence they have on paper: powers first, then multiply and
 * divide, then add and subtract, with a leading sign binding looser than a
 * power so -2^2 is -4.
 *
 * Every operation is checked before it happens. Overflow, division by zero and
 * a negative power are all refused and reported as an error, because a kernel
 * with no interrupt table cannot afford a divide fault.
 *
 * M7 adds one conditional form and nothing else:
 *
 *     IF <expression> <comparison> <expression> THEN <expression> ELSE <expression>
 *
 * with =, ==, < or > as the comparison. There is no nesting and there are no
 * loops. Both branches are worked out, so a branch that overflows makes the
 * whole line an error even when it is not the one taken.
 *
 * M8 adds names for values, and nothing else:
 *
 *     <name> = <expression>
 *
 * A name is an uppercase letter followed by up to three more letters or
 * digits, and a name may be used anywhere a number may be. The values live in
 * a fixed table in vars.h, which the caller owns, because they outlive the
 * line that made them. IF, THEN and ELSE are reserved and cannot be names.
 * There are still no loops, no functions, no arrays, no strings and no scope:
 * one global table is the whole of it.
 */
#ifndef ME_CALC_H
#define ME_CALC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vars.h"

/* Long enough for an expression worth typing without a screen full of digits. */
#define CALC_MAX_INPUT 32
/* A 64 bit value, a sign, and a terminator. */
#define CALC_MAX_NUMBER 21

struct calc {
    char text[CALC_MAX_INPUT + 1];
    uint8_t length;
    int64_t result;
    bool has_result;
    bool error;
    /* Where names are looked up and stored. NULL means there are none, so
     * every name is unknown and no assignment can succeed. The caller owns
     * the table because it has to outlive the line being typed. */
    struct vars *vars;
};

/* Control characters the caller maps its keys onto. Evaluating is enter alone:
 * since M7 the equals key is a comparison someone might want to type. */
#define CALC_EVALUATE '\n'
#define CALC_DELETE   '\b'
#define CALC_CLEAR    '\x1b'

/* Prepares a calculator: an empty line, and the table its names live in.
 * `vars` may be NULL, which means there are no variables and every name is
 * unknown. Call this before anything else, because calc_reset deliberately
 * does not touch the table and so cannot set it up. */
void calc_init(struct calc *calc, struct vars *vars);

/* Clears the line and any result. The variable table is left alone: outliving
 * the line is the point of it. */
void calc_reset(struct calc *calc);

/* Feeds one character: a digit, '+', '-', or one of the control characters
 * above. Returns true when the line to display has changed. */
bool calc_key(struct calc *calc, char key);

/* Evaluates a complete expression, reading and assigning names in `vars`,
 * which may be NULL when there are none. False means it was malformed, a name
 * was unknown, the table was full, or the result would not fit; *out is left
 * alone, and nothing is stored. */
bool calc_evaluate(const char *text, uint8_t length, struct vars *vars,
                   int64_t *out);

/* Writes `value` as decimal digits with a leading '-' if negative. Returns the
 * number of characters written, not counting the terminator. */
size_t calc_format(int64_t value, char *out, size_t size);

/* The line to show: a prompt, the expression as typed, the expression with its
 * result, or an error. */
void calc_line(const struct calc *calc, char *out, size_t size);

#endif /* ME_CALC_H */
