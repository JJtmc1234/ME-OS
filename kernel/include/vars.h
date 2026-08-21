/* A fixed table of named whole numbers, for M8.
 *
 * The whole variable system is this: eight slots, a short name in each, and a
 * linear search. There is no allocation, no scope, no lifetime and no type
 * beyond the one integer type the calculator already has. A name that is not
 * in the table is unknown, and asking for it is an error rather than zero,
 * because a typo that silently reads as zero is worse than one that says so.
 *
 * The table is separate from calc.c because it outlives a line of input. The
 * line is cleared whenever a new one is typed; the variables are not.
 */
#ifndef ME_VARS_H
#define ME_VARS_H

#include <stdbool.h>
#include <stdint.h>

/* Eight names is enough to show the idea and small enough that searching the
 * whole table is cheaper than any index would be. */
#define VARS_MAX 8
/* Names are short on purpose: the input line holds 32 characters, and a long
 * name would leave no room for a sum to put it in. */
#define VARS_MAX_NAME 4

struct vars {
    char name[VARS_MAX][VARS_MAX_NAME + 1];
    int64_t value[VARS_MAX];
    uint8_t count;
};

/* Empties the table. Every name becomes unknown again. */
void vars_reset(struct vars *vars);

/* Reads a name. False means nothing has been stored under it, and *out is
 * left alone. */
bool vars_get(const struct vars *vars, const char *name, int64_t *out);

/* Stores a value, replacing whatever that name held before. False means the
 * name will not fit in a slot, or that every slot already holds a different
 * name. Nothing is stored when it returns false. */
bool vars_set(struct vars *vars, const char *name, int64_t value);

#endif /* ME_VARS_H */
