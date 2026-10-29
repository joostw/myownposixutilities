/*
 * expr.c - expr: evaluate arguments as an expression (POSIX.1-2024, XCU expr)
 *
 * Synopsis: expr operand...
 *
 * A recursive-descent parser over the arguments, by the precedence of the
 * standard (lowest first): |, &, the comparisons, + and -, * / and %, :,
 * then ( ) and the operands.  Integers are intmax_t; overflow is detected.
 * The : operator matches a basic regular expression at the start of the
 * string (regcomp(), the match must begin at the first character) and
 * gives the number of characters matched, or with \( \) the text of the
 * first subexpression.
 *
 * Choices where the standard leaves room:
 *  - A first argument "--" is discarded.
 *  - An argument in the place of an operand is an operand even if it is an
 *    operator symbol (expr + : + gives 1; expr "" | x gives x); one where
 *    an operator is expected must be one.
 *  - '^' at the start of a pattern of : is an ordinary anchor (it changes
 *    nothing).
 *  - A value is "zero" if it is an integer whose value is 0 ("0", "-0",
 *    "00").
 *  - Division by zero, an integer that does not fit, a non-integer
 *    argument to an arithmetic operator, and an invalid pattern are errors
 *    of the expression (status 2); so is nesting deeper than 1000.
 *  - The strings length, substr, index and match are ordinary strings.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static char **av;
static int ac, ap, depth;

static void error(const char *fmt, const char *arg)
{
    fprintf(stderr, "expr: ");
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    exit(2);
}

/* a value: a string, perhaps also an integer */
struct val {
    char *s;
};

static struct val mkstr(const char *s)
{
    struct val v;
    v.s = strdup(s);
    if (!v.s) {
        fprintf(stderr, "expr: out of memory\n");
        exit(3);
    }
    return v;
}

static struct val mkint(intmax_t n)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%jd", n);
    return mkstr(buf);
}

/* is s an integer (an optional '-' and digits)?  its value in *n */
static bool is_int(const char *s, intmax_t *n)
{
    const char *p = s;
    if (*p == '-')
        p++;
    if (!*p || strspn(p, "0123456789") != strlen(p))
        return false;
    errno = 0;
    intmax_t v = strtoimax(s, NULL, 10);
    if (errno == ERANGE)
        error("%s: the integer does not fit", s);
    *n = v;
    return true;
}

static intmax_t need_int(struct val v)
{
    intmax_t n;
    if (!is_int(v.s, &n))
        error("%s: not an integer", v.s);
    return n;
}

static bool null_or_zero(struct val v)
{
    intmax_t n;
    return !*v.s || (is_int(v.s, &n) && n == 0);
}

static bool is(const char *op)
{
    return ap < ac && strcmp(av[ap], op) == 0;
}

static struct val parse_or(void);

static struct val primary(void)
{
    if (ap >= ac)
        error("%sthe expression is incomplete", "");
    if (is("(")) {
        if (++depth > 1000)
            error("%sthe expression is nested too deeply", "");
        ap++;
        struct val v = parse_or();
        if (!is(")"))
            error("%sa ) is missing", "");
        ap++;
        depth--;
        return v;
    }
    return mkstr(av[ap++]);
}

static struct val match(struct val a, struct val b)
{
    regex_t re;
    int e = regcomp(&re, b.s, 0);
    if (e) {
        char msg[256];
        regerror(e, &re, msg, sizeof msg);
        fprintf(stderr, "expr: %s: %s\n", b.s, msg);
        exit(2);
    }
    regmatch_t m[2];
    bool ok = regexec(&re, a.s, 2, m, 0) == 0 && m[0].rm_so == 0;
    struct val r;
    if (re.re_nsub > 0) {
        if (ok && m[1].rm_so >= 0) {
            size_t n = (size_t)(m[1].rm_eo - m[1].rm_so);
            char *s = malloc(n + 1);
            if (!s) {
                fprintf(stderr, "expr: out of memory\n");
                exit(3);
            }
            memcpy(s, a.s + m[1].rm_so, n);
            s[n] = '\0';
            r.s = s;
        } else
            r = mkstr("");
    } else {
        /* the number of characters matched */
        intmax_t chars = 0;
        if (ok) {
            mbstate_t st;
            memset(&st, 0, sizeof st);
            for (regoff_t i = 0; i < m[0].rm_eo;) {
                size_t k = mbrlen(a.s + i, (size_t)(m[0].rm_eo - i), &st);
                if (k == (size_t)-1 || k == (size_t)-2 || k == 0) {
                    memset(&st, 0, sizeof st);
                    k = 1;
                }
                i += (regoff_t)k;
                chars++;
            }
        }
        r = mkint(chars);
    }
    regfree(&re);
    free(a.s);
    free(b.s);
    return r;
}

static struct val parse_match(void)
{
    struct val a = primary();
    while (is(":")) {
        ap++;
        struct val b = primary();
        a = match(a, b);
    }
    return a;
}

static struct val parse_mul(void)
{
    struct val a = parse_match();
    while (is("*") || is("/") || is("%")) {
        char op = av[ap++][0];
        struct val b = parse_match();
        intmax_t x = need_int(a), y = need_int(b), r;
        if (op == '*') {
            if ((x > 0 && y > 0 && x > INTMAX_MAX / y) || (x < 0 && y < 0 && x < INTMAX_MAX / y) ||
                (x > 0 && y < 0 && y < INTMAX_MIN / x) || (x < 0 && y > 0 && x < INTMAX_MIN / y))
                error("%sthe result does not fit", "");
            r = x * y;
        } else {
            if (y == 0)
                error("%sdivision by zero", "");
            if (x == INTMAX_MIN && y == -1)
                error("%sthe result does not fit", "");
            r = op == '/' ? x / y : x % y;
        }
        free(a.s);
        free(b.s);
        a = mkint(r);
    }
    return a;
}

static struct val parse_add(void)
{
    struct val a = parse_mul();
    while (is("+") || is("-")) {
        char op = av[ap++][0];
        struct val b = parse_mul();
        intmax_t x = need_int(a), y = need_int(b), r;
        if (op == '+') {
            if ((y > 0 && x > INTMAX_MAX - y) || (y < 0 && x < INTMAX_MIN - y))
                error("%sthe result does not fit", "");
            r = x + y;
        } else {
            if ((y < 0 && x > INTMAX_MAX + y) || (y > 0 && x < INTMAX_MIN + y))
                error("%sthe result does not fit", "");
            r = x - y;
        }
        free(a.s);
        free(b.s);
        a = mkint(r);
    }
    return a;
}

static bool is_cmp(void)
{
    return is("=") || is("!=") || is("<") || is("<=") || is(">") || is(">=");
}

static struct val parse_cmp(void)
{
    struct val a = parse_add();
    while (is_cmp()) {
        const char *op = av[ap++];
        struct val b = parse_add();
        intmax_t x, y;
        int c;
        if (is_int(a.s, &x) && is_int(b.s, &y))
            c = x < y ? -1 : x > y;
        else
            c = strcoll(a.s, b.s);
        bool r;
        if (strcmp(op, "=") == 0) r = c == 0;
        else if (strcmp(op, "!=") == 0) r = c != 0;
        else if (strcmp(op, "<") == 0) r = c < 0;
        else if (strcmp(op, "<=") == 0) r = c <= 0;
        else if (strcmp(op, ">") == 0) r = c > 0;
        else r = c >= 0;
        free(a.s);
        free(b.s);
        a = mkint(r);
    }
    return a;
}

static struct val parse_and(void)
{
    struct val a = parse_cmp();
    while (is("&")) {
        ap++;
        struct val b = parse_cmp();
        if (null_or_zero(a) || null_or_zero(b)) {
            free(a.s);
            a = mkint(0);
        }
        free(b.s);
    }
    return a;
}

static struct val parse_or(void)
{
    struct val a = parse_and();
    while (is("|")) {
        ap++;
        struct val b = parse_and();
        if (null_or_zero(a)) {
            free(a.s);
            if (*b.s)
                a = b;
            else {
                free(b.s);
                a = mkint(0);
            }
        } else
            free(b.s);
    }
    return a;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    av = argv;
    ac = argc;
    ap = 1;
    if (ap < ac && strcmp(av[ap], "--") == 0)
        ap++;
    if (ap >= ac) {
        fprintf(stderr, "usage: expr operand...\n");
        return 2;
    }
    struct val v = parse_or();
    if (ap < ac)
        error("%s: unexpected", av[ap]);
    if (puts(v.s) == EOF || fflush(stdout) != 0) {
        fprintf(stderr, "expr: write error: %s\n", strerror(errno));
        return 3;
    }
    return null_or_zero(v) ? 1 : 0;
}
