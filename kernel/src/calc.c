#include "calc.h"

#define PROMPT "TYPE A SUM"
#define ERROR_TEXT "ERROR"

/* A small recursive descent parser. Precedence, highest first:
 *
 *   name     a stored value, or a number
 *   ^        power, right associative
 *   unary    a leading + or -
 *   * /      multiply and divide
 *   + -      add and subtract
 *
 * Every operation is checked for overflow before it happens, and division by
 * zero is refused rather than trapped, because a division fault in a kernel
 * with no interrupt table would triple fault the machine.
 */

struct parser {
    const char *text;
    uint8_t length;
    uint8_t pos;
    bool ok;
    struct vars *vars;   /* NULL when there are no variables at all */
    /* The name this line assigns to, or empty for none. The value is not
     * stored while parsing: a line like "X=5 6" parses an assignment and only
     * then turns out to have something left over, and a line that is refused
     * must leave the table exactly as it was. */
    char assign[VARS_MAX_NAME + 1];
};

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_operator(char c)
{
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '^';
}

static bool is_sign(char c)
{
    return c == '+' || c == '-';
}

static bool is_letter(char c)
{
    return c >= 'A' && c <= 'Z';
}

/* A name starts with a letter and may go on with letters or digits, so X, X2
 * and SUM1 are all names and 2X is not. Lowercase is not a letter here: the
 * keyboard produces uppercase and the keywords are uppercase, so the whole
 * language is. */
static bool is_name_char(char c)
{
    return is_letter(c) || is_digit(c);
}

/* The words the language keeps for itself. They cannot be variable names. */
static const char *const keywords[] = { "IF", "THEN", "ELSE" };

static bool same_word(const char *a, const char *b)
{
    for (uint8_t i = 0; a[i] != '\0' || b[i] != '\0'; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static bool is_keyword(const char *word)
{
    for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
        if (same_word(word, keywords[i])) {
            return true;
        }
    }
    return false;
}

static void skip_spaces(struct parser *p)
{
    while (p->pos < p->length && p->text[p->pos] == ' ') {
        p->pos++;
    }
}

/* The next character that matters. Spaces are skipped here rather than in a
 * separate pass, which is why "1 + 2" works and "1 2" does not: digits are
 * read without this, so a space ends a number. */
static char peek(struct parser *p)
{
    skip_spaces(p);
    return p->pos < p->length ? p->text[p->pos] : '\0';
}

/* The character right here, whatever it is. Used where a space would change
 * the meaning, as in telling "==" from "= =". */
static char peek_raw(const struct parser *p)
{
    return p->pos < p->length ? p->text[p->pos] : '\0';
}

/* Consumes a keyword if it is next. A keyword has to end where it ends, so
 * neither IFX nor IF1 is IF: both are names. */
static bool match_word(struct parser *p, const char *word)
{
    skip_spaces(p);

    uint8_t at = p->pos;
    for (uint8_t i = 0; word[i] != '\0'; i++) {
        if (at >= p->length || p->text[at] != word[i]) {
            return false;
        }
        at++;
    }
    if (at < p->length && is_name_char(p->text[at])) {
        return false;
    }
    p->pos = at;
    return true;
}

static void copy(char *out, size_t size, const char *text, size_t *written)
{
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (*written + 1 >= size) {
            return;
        }
        out[(*written)++] = text[i];
    }
}

/* Checked arithmetic. Each one refuses rather than wrapping. */

static int64_t add_checked(int64_t a, int64_t b, bool *ok)
{
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        *ok = false;
        return 0;
    }
    return a + b;
}

static int64_t sub_checked(int64_t a, int64_t b, bool *ok)
{
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) {
        *ok = false;
        return 0;
    }
    return a - b;
}

static int64_t mul_checked(int64_t a, int64_t b, bool *ok)
{
    if (a == 0 || b == 0) {
        return 0;
    }

    /* The test has to happen before the multiplication, not after it. Doing it
     * afterwards means overflowing first, and signed overflow is undefined, so
     * the compiler is entitled to delete the check that was supposed to catch
     * it. It did.
     */
    if (a == -1) {
        if (b == INT64_MIN) {
            *ok = false;
            return 0;
        }
        return -b;
    }
    if (b == -1) {
        if (a == INT64_MIN) {
            *ok = false;
            return 0;
        }
        return -a;
    }

    const bool fits = a > 0
        ? (b > 0 ? a <= INT64_MAX / b : b >= INT64_MIN / a)
        : (b > 0 ? a >= INT64_MIN / b : a >= INT64_MAX / b);
    if (!fits) {
        *ok = false;
        return 0;
    }
    return a * b;
}

static int64_t div_checked(int64_t a, int64_t b, bool *ok)
{
    if (b == 0) {
        *ok = false;   /* dividing by zero has no answer to give */
        return 0;
    }
    if (a == INT64_MIN && b == -1) {
        *ok = false;   /* the answer is one past the largest value */
        return 0;
    }
    return a / b;      /* whole numbers only, truncated towards zero */
}

static int64_t pow_checked(int64_t base, int64_t exponent, bool *ok)
{
    if (exponent < 0) {
        /* A negative power is a fraction, and this milestone has whole numbers
         * only. The two cases that stay whole are still refused, so the rule is
         * one sentence rather than three. */
        *ok = false;
        return 0;
    }

    int64_t result = 1;
    for (int64_t i = 0; i < exponent; i++) {
        result = mul_checked(result, base, ok);
        if (!*ok) {
            return 0;
        }
    }
    return result;   /* anything to the power of zero is one, including zero */
}

static int64_t parse_expression(struct parser *p);

static int64_t parse_number(struct parser *p)
{
    skip_spaces(p);
    if (!is_digit(peek_raw(p))) {
        p->ok = false;
        return 0;
    }

    int64_t value = 0;
    while (is_digit(peek_raw(p))) {
        const int64_t digit = p->text[p->pos] - '0';
        value = mul_checked(value, 10, &p->ok);
        value = add_checked(value, digit, &p->ok);
        if (!p->ok) {
            return 0;
        }
        p->pos++;
    }
    return value;
}

/* Reads a name into `out`, which holds VARS_MAX_NAME characters and a
 * terminator. False leaves the position where it was, so the caller can try
 * something else: there was no name here, it was a keyword, or it was longer
 * than a slot can hold, which is refused rather than cut short into a
 * different name. */
static bool read_name(struct parser *p, char *out)
{
    skip_spaces(p);

    const uint8_t start = p->pos;
    if (!is_letter(peek_raw(p))) {
        return false;
    }

    uint8_t count = 0;
    while (is_name_char(peek_raw(p))) {
        if (count >= VARS_MAX_NAME) {
            p->pos = start;
            return false;
        }
        out[count++] = p->text[p->pos++];
    }
    out[count] = '\0';

    if (is_keyword(out)) {
        p->pos = start;
        return false;
    }
    return true;
}

/* primary := name | number
 *
 * A name that has never been assigned is an error rather than zero. A typo
 * that quietly reads as zero gives a wrong answer and says nothing about it.
 */
static int64_t parse_primary(struct parser *p)
{
    char name[VARS_MAX_NAME + 1];

    if (read_name(p, name)) {
        int64_t value = 0;
        if (!vars_get(p->vars, name, &value)) {
            p->ok = false;
            return 0;
        }
        return value;
    }
    return parse_number(p);
}

static int64_t parse_unary(struct parser *p);

/* power := primary ('^' unary)?
 *
 * The right hand side is a unary so 2^-1 parses and is then refused as a
 * fraction, rather than failing as a syntax error and blaming the wrong thing.
 */
static int64_t parse_power(struct parser *p)
{
    const int64_t base = parse_primary(p);
    if (!p->ok) {
        return 0;
    }
    if (peek(p) != '^') {
        return base;
    }
    p->pos++;
    const int64_t exponent = parse_unary(p);
    if (!p->ok) {
        return 0;
    }
    return pow_checked(base, exponent, &p->ok);
}

/* unary := ('+' | '-') unary | power
 *
 * A sign binds looser than a power, so -2^2 is -(2^2), which is what the same
 * expression means on paper.
 */
static int64_t parse_unary(struct parser *p)
{
    const char c = peek(p);
    if (is_sign(c)) {
        p->pos++;
        const int64_t value = parse_unary(p);
        if (!p->ok) {
            return 0;
        }
        if (c == '-') {
            return sub_checked(0, value, &p->ok);
        }
        return value;
    }
    return parse_power(p);
}

/* term := unary (('*' | '/') unary)* */
static int64_t parse_term(struct parser *p)
{
    int64_t value = parse_unary(p);
    while (p->ok) {
        const char c = peek(p);
        if (c != '*' && c != '/') {
            break;
        }
        p->pos++;
        const int64_t right = parse_unary(p);
        if (!p->ok) {
            break;
        }
        value = c == '*' ? mul_checked(value, right, &p->ok)
                         : div_checked(value, right, &p->ok);
    }
    return value;
}

/* expression := term (('+' | '-') term)* */
static int64_t parse_expression(struct parser *p)
{
    int64_t value = parse_term(p);
    while (p->ok) {
        const char c = peek(p);
        if (c != '+' && c != '-') {
            break;
        }
        p->pos++;
        const int64_t right = parse_term(p);
        if (!p->ok) {
            break;
        }
        value = c == '+' ? add_checked(value, right, &p->ok)
                         : sub_checked(value, right, &p->ok);
    }
    return value;
}

/* comparison := '=' | '==' | '<' | '>' */
enum comparison { CMP_NONE, CMP_EQUAL, CMP_LESS, CMP_GREATER };

static enum comparison parse_comparison(struct parser *p)
{
    const char c = peek(p);
    if (c == '=') {
        p->pos++;
        if (peek_raw(p) == '=') {
            p->pos++;   /* == means the same as =, and reads better */
        }
        return CMP_EQUAL;
    }
    if (c == '<') {
        p->pos++;
        return CMP_LESS;
    }
    if (c == '>') {
        p->pos++;
        return CMP_GREATER;
    }
    return CMP_NONE;
}

/* statement := name '=' expression
 *            | 'IF' expression comparison expression 'THEN' expression
 *              'ELSE' expression
 *            | expression
 *
 * One assignment or one conditional, no nesting and no loops. Both branches
 * are worked out, because the parser has to read past both of them anyway, so
 * a branch that overflows spoils the line whether or not it is the one taken.
 * Assignment is a whole line on its own, so a branch cannot hide one and the
 * order the branches are worked out in cannot matter.
 */
static int64_t parse_statement(struct parser *p)
{
    const uint8_t start = p->pos;
    char name[VARS_MAX_NAME + 1];

    /* One '=' assigns. Two compare, and comparing only happens inside an IF,
     * so X==5 on its own is still refused rather than quietly storing 5. */
    if (read_name(p, name)) {
        skip_spaces(p);
        if (peek_raw(p) == '=' &&
            (p->pos + 1u >= p->length || p->text[p->pos + 1] != '=')) {
            p->pos++;

            const int64_t value = parse_expression(p);
            if (!p->ok) {
                return 0;
            }
            for (uint8_t i = 0; i <= VARS_MAX_NAME; i++) {
                p->assign[i] = name[i];
                if (name[i] == '\0') {
                    break;
                }
            }
            return value;   /* an assignment answers with the value it stores */
        }
    }
    p->pos = start;

    if (!match_word(p, "IF")) {
        return parse_expression(p);
    }

    const int64_t left = parse_expression(p);
    if (!p->ok) {
        return 0;
    }

    const enum comparison how = parse_comparison(p);
    if (how == CMP_NONE) {
        p->ok = false;
        return 0;
    }

    const int64_t right = parse_expression(p);
    if (!p->ok || !match_word(p, "THEN")) {
        p->ok = false;
        return 0;
    }

    const int64_t when_true = parse_expression(p);
    if (!p->ok || !match_word(p, "ELSE")) {
        p->ok = false;
        return 0;
    }

    const int64_t when_false = parse_expression(p);
    if (!p->ok) {
        return 0;
    }

    bool holds = false;
    switch (how) {
    case CMP_EQUAL:   holds = left == right; break;
    case CMP_LESS:    holds = left < right;  break;
    case CMP_GREATER: holds = left > right;  break;
    case CMP_NONE:    break;
    }
    return holds ? when_true : when_false;
}

void calc_init(struct calc *calc, struct vars *vars)
{
    if (calc == NULL) {
        return;
    }
    calc->vars = vars;
    calc_reset(calc);
}

/* Clears the line. calc->vars is deliberately untouched: a variable outlives
 * the line that made it, which is the whole point of having one. */
void calc_reset(struct calc *calc)
{
    if (calc == NULL) {
        return;
    }
    calc->text[0] = '\0';
    calc->length = 0;
    calc->result = 0;
    calc->has_result = false;
    calc->error = false;
}

bool calc_evaluate(const char *text, uint8_t length, struct vars *vars,
                   int64_t *out)
{
    if (text == NULL || out == NULL || length == 0) {
        return false;
    }

    struct parser parser = { text, length, 0, true, vars, { '\0' } };
    const int64_t value = parse_statement(&parser);

    skip_spaces(&parser);
    if (!parser.ok || parser.pos != length) {
        return false;   /* malformed, or something left over at the end */
    }
    /* The line is whole, so an assignment can finally happen. It still fails
     * when every slot holds a different name. */
    if (parser.assign[0] != '\0' && !vars_set(vars, parser.assign, value)) {
        return false;
    }
    *out = value;
    return true;
}

bool calc_key(struct calc *calc, char key)
{
    if (calc == NULL) {
        return false;
    }

    if (key == CALC_CLEAR) {
        if (calc->length == 0 && !calc->has_result && !calc->error) {
            return false;
        }
        calc_reset(calc);
        return true;
    }

    if (key == CALC_DELETE) {
        if (calc->length == 0) {
            return false;
        }
        calc->text[--calc->length] = '\0';
        calc->has_result = false;
        calc->error = false;
        return true;
    }

    if (key == CALC_EVALUATE) {
        if (calc->length == 0) {
            return false;
        }
        calc->has_result = calc_evaluate(calc->text, calc->length, calc->vars,
                                         &calc->result);
        calc->error = !calc->has_result;
        return true;
    }

    const bool comparison = key == '=' || key == '<' || key == '>';
    if (!is_digit(key) && !is_operator(key) && !is_letter(key) &&
        key != ' ' && !comparison) {
        return false;   /* not a key these milestones know about */
    }

    /* Until M8 a letter was only accepted where one of the three keywords
     * could still be forming, so a key pressed to see its name on the key line
     * could not end up in the sum. Variables need names, so every letter is
     * part of the language now and every letter types. The parser is what
     * decides whether the line makes sense. */
    if (calc->length >= CALC_MAX_INPUT) {
        return false;
    }

    /* Two operators in a row are only allowed when the second is a sign, so
     * 5*-3 can be typed but 5**3 cannot. */
    if (is_operator(key) && calc->length > 0) {
        const char last = calc->text[calc->length - 1];
        if (is_operator(last) && !(is_sign(key) && !is_sign(last))) {
            return false;
        }
    }

    /* Typing after a result starts a new sum rather than editing the old one. */
    if (calc->has_result || calc->error) {
        calc_reset(calc);
    }

    calc->text[calc->length++] = key;
    calc->text[calc->length] = '\0';
    return true;
}

size_t calc_format(int64_t value, char *out, size_t size)
{
    if (out == NULL || size == 0) {
        return 0;
    }

    /* Take the magnitude as unsigned, so the most negative value has somewhere
     * to go: negating it would overflow. */
    uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;

    char digits[CALC_MAX_NUMBER];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude > 0 && count < sizeof digits);

    size_t written = 0;
    if (value < 0 && written + 1 < size) {
        out[written++] = '-';
    }
    while (count > 0 && written + 1 < size) {
        out[written++] = digits[--count];
    }
    out[written] = '\0';
    return written;
}

void calc_line(const struct calc *calc, char *out, size_t size)
{
    if (out == NULL || size == 0) {
        return;
    }
    out[0] = '\0';
    if (calc == NULL) {
        return;
    }

    size_t written = 0;
    if (calc->length == 0) {
        copy(out, size, PROMPT, &written);
        out[written] = '\0';
        return;
    }

    for (uint8_t i = 0; i < calc->length && written + 1 < size; i++) {
        out[written++] = calc->text[i];
    }

    if (calc->error) {
        copy(out, size, "=", &written);
        copy(out, size, ERROR_TEXT, &written);
    } else if (calc->has_result) {
        char number[CALC_MAX_NUMBER];
        calc_format(calc->result, number, sizeof number);
        copy(out, size, "=", &written);
        copy(out, size, number, &written);
    }

    out[written] = '\0';
}
