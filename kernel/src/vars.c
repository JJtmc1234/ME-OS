#include "vars.h"

#include <stddef.h>

/* Names are compared exactly, so X and x would be two different names. Only
 * uppercase ever reaches here: the keyboard produces uppercase letters and the
 * parser accepts nothing else, so the language is uppercase throughout. */
static bool same_name(const char *a, const char *b)
{
    for (uint8_t i = 0; a[i] != '\0' || b[i] != '\0'; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

/* A slot holds VARS_MAX_NAME characters and a terminator, so anything longer
 * is refused here rather than quietly cut short into a different name. */
static bool fits(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (uint8_t i = 0; i <= VARS_MAX_NAME; i++) {
        if (name[i] == '\0') {
            return true;
        }
    }
    return false;
}

static void store_name(char *slot, const char *name)
{
    uint8_t i = 0;
    while (i < VARS_MAX_NAME && name[i] != '\0') {
        slot[i] = name[i];
        i++;
    }
    slot[i] = '\0';
}

void vars_reset(struct vars *vars)
{
    if (vars == NULL) {
        return;
    }
    for (uint8_t i = 0; i < VARS_MAX; i++) {
        vars->name[i][0] = '\0';
        vars->value[i] = 0;
    }
    vars->count = 0;
}

bool vars_get(const struct vars *vars, const char *name, int64_t *out)
{
    if (vars == NULL || out == NULL || !fits(name)) {
        return false;
    }
    for (uint8_t i = 0; i < vars->count; i++) {
        if (same_name(vars->name[i], name)) {
            *out = vars->value[i];
            return true;
        }
    }
    return false;
}

bool vars_set(struct vars *vars, const char *name, int64_t value)
{
    if (vars == NULL || !fits(name)) {
        return false;
    }
    for (uint8_t i = 0; i < vars->count; i++) {
        if (same_name(vars->name[i], name)) {
            vars->value[i] = value;   /* the same name means the same slot */
            return true;
        }
    }
    if (vars->count >= VARS_MAX) {
        return false;   /* the table is full, and nothing is thrown away */
    }
    store_name(vars->name[vars->count], name);
    vars->value[vars->count] = value;
    vars->count++;
    return true;
}
