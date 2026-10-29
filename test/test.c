/*
 * test.c - test, [: evaluate expression (POSIX.1-2024, XCU test)
 *
 * Synopsis:
 *   test [expression]
 *   [ [expression] ]
 *
 * The program acts as [ when it is called by that name (the last argument
 * must then be ]).  The result is decided by the number of arguments, as
 * the standard lays down for up to four; it has no options, so "--" is an
 * ordinary string.
 *
 * Choices where the standard leaves room:
 *  - Beyond what the standard specifies (more than four arguments, and the
 *    forms it leaves unspecified for fewer), the expression is parsed with
 *    the historical grammar: ! binds tighter than -a, which binds tighter
 *    than -o, and ( ) group; a primary is a binary one if the argument
 *    after the next is there and the next is a binary operator, else a
 *    unary one if it is a unary operator with an operand, else a string.
 *    The three- and four-argument forms ( x ) and ( a b ) are done this
 *    way.
 *  - Integers are decimal, with optional leading and trailing blanks and
 *    a sign; anything else, or one out of range, is an error (status 2).
 *  - -t takes such an integer; -nt and -ot compare the modification times
 *    with their nanoseconds; -r -w -x use the effective user and group
 *    IDs (faccessat with AT_EACCESS).
 *  - < and > compare with strcoll(); = and != compare bytes.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *prog = "test";

static void error(const char *fmt, const char *arg)
{
    fprintf(stderr, "%s: ", prog);
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    exit(2);
}

static intmax_t integer(const char *s)
{
    const char *p = s;
    while (*p == ' ' || *p == '\t')
        p++;
    const char *q = p;
    if (*q == '+' || *q == '-')
        q++;
    if (!isdigit((unsigned char)*q))
        error("%s: an integer expected", s);
    char *end;
    errno = 0;
    intmax_t v = strtoimax(p, &end, 10);
    if (errno == ERANGE)
        error("%s: out of range", s);
    while (*end == ' ' || *end == '\t')
        end++;
    if (*end)
        error("%s: an integer expected", s);
    return v;
}

static bool is_unary(const char *s)
{
    return s[0] == '-' && s[1] && !s[2] && strchr("bcdefghLnprSstuwxz", s[1]);
}

static bool is_binary(const char *s)
{
    static const char *ops[] = { "=", "!=", "<", ">", "-eq", "-ne", "-gt", "-ge",
                                 "-lt", "-le", "-ef", "-nt", "-ot", NULL };
    for (int i = 0; ops[i]; i++)
        if (strcmp(s, ops[i]) == 0)
            return true;
    return false;
}

static bool unary(const char *op, const char *a)
{
    struct stat st;
    switch (op[1]) {
    case 'n': return *a != '\0';
    case 'z': return *a == '\0';
    case 't': {
        intmax_t fd = integer(a);
        return fd >= 0 && fd <= INT_MAX && isatty((int)fd);
    }
    case 'h': case 'L':
        return lstat(a, &st) == 0 && S_ISLNK(st.st_mode);
    case 'r': return faccessat(AT_FDCWD, a, R_OK, AT_EACCESS) == 0;
    case 'w': return faccessat(AT_FDCWD, a, W_OK, AT_EACCESS) == 0;
    case 'x': return faccessat(AT_FDCWD, a, X_OK, AT_EACCESS) == 0;
    }
    if (stat(a, &st) != 0)
        return false;
    switch (op[1]) {
    case 'b': return S_ISBLK(st.st_mode);
    case 'c': return S_ISCHR(st.st_mode);
    case 'd': return S_ISDIR(st.st_mode);
    case 'e': return true;
    case 'f': return S_ISREG(st.st_mode);
    case 'g': return (st.st_mode & S_ISGID) != 0;
    case 'p': return S_ISFIFO(st.st_mode);
    case 'S': return S_ISSOCK(st.st_mode);
    case 's': return st.st_size > 0;
    case 'u': return (st.st_mode & S_ISUID) != 0;
    }
    return false;
}

static int mtime_cmp(const struct stat *a, const struct stat *b)
{
    if (a->st_mtim.tv_sec != b->st_mtim.tv_sec)
        return a->st_mtim.tv_sec < b->st_mtim.tv_sec ? -1 : 1;
    if (a->st_mtim.tv_nsec != b->st_mtim.tv_nsec)
        return a->st_mtim.tv_nsec < b->st_mtim.tv_nsec ? -1 : 1;
    return 0;
}

static bool binary(const char *a, const char *op, const char *b)
{
    if (strcmp(op, "=") == 0) return strcmp(a, b) == 0;
    if (strcmp(op, "!=") == 0) return strcmp(a, b) != 0;
    if (strcmp(op, "<") == 0) return strcoll(a, b) < 0;
    if (strcmp(op, ">") == 0) return strcoll(a, b) > 0;
    if (strcmp(op, "-ef") == 0) {
        struct stat s1, s2;
        return stat(a, &s1) == 0 && stat(b, &s2) == 0 && s1.st_dev == s2.st_dev &&
               s1.st_ino == s2.st_ino;
    }
    if (strcmp(op, "-nt") == 0 || strcmp(op, "-ot") == 0) {
        struct stat s1, s2;
        bool e1 = stat(a, &s1) == 0, e2 = stat(b, &s2) == 0;
        if (op[1] == 'n')
            return e1 && (!e2 || mtime_cmp(&s1, &s2) > 0);
        return e2 && (!e1 || mtime_cmp(&s1, &s2) < 0);
    }
    intmax_t x = integer(a), y = integer(b);
    if (strcmp(op, "-eq") == 0) return x == y;
    if (strcmp(op, "-ne") == 0) return x != y;
    if (strcmp(op, "-gt") == 0) return x > y;
    if (strcmp(op, "-ge") == 0) return x >= y;
    if (strcmp(op, "-lt") == 0) return x < y;
    return x <= y;              /* -le */
}

/* ---- the historical grammar ---- */

static char **tv;
static int tn, tp;

static bool t_or(void);

static bool t_prim(void)
{
    if (tp >= tn)
        error("%sargument expected", "");
    if (strcmp(tv[tp], "!") == 0) {
        tp++;
        return !t_prim();
    }
    if (strcmp(tv[tp], "(") == 0 && tp + 1 < tn) {
        /* "(" followed by a binary operator and an operand is a comparison */
        if (!(tp + 2 < tn && is_binary(tv[tp + 1]))) {
            tp++;
            bool r = t_or();
            if (tp >= tn || strcmp(tv[tp], ")") != 0)
                error("%s) expected", "");
            tp++;
            return r;
        }
    }
    if (tp + 2 < tn && is_binary(tv[tp + 1])) {
        bool r = binary(tv[tp], tv[tp + 1], tv[tp + 2]);
        tp += 3;
        return r;
    }
    if (is_unary(tv[tp]) && tp + 1 < tn) {
        bool r = unary(tv[tp], tv[tp + 1]);
        tp += 2;
        return r;
    }
    return *tv[tp++] != '\0';
}

static bool t_and(void)
{
    bool r = t_prim();
    while (tp < tn && strcmp(tv[tp], "-a") == 0) {
        tp++;
        bool s = t_prim();
        r = r && s;
    }
    return r;
}

static bool t_or(void)
{
    bool r = t_and();
    while (tp < tn && strcmp(tv[tp], "-o") == 0) {
        tp++;
        bool s = t_and();
        r = r || s;
    }
    return r;
}

static bool grammar(char **a, int n)
{
    tv = a;
    tn = n;
    tp = 0;
    bool r = t_or();
    if (tp < tn)
        error("%s: unexpected argument", tv[tp]);
    return r;
}

/* the algorithm of the standard, by the number of arguments */
static bool test(char **a, int n)
{
    switch (n) {
    case 0:
        return false;
    case 1:
        return *a[0] != '\0';
    case 2:
        if (strcmp(a[0], "!") == 0)
            return *a[1] == '\0';
        if (is_unary(a[0]))
            return unary(a[0], a[1]);
        break;
    case 3:
        if (is_binary(a[1]))
            return binary(a[0], a[1], a[2]);
        if (strcmp(a[0], "!") == 0)
            return !test(a + 1, 2);
        break;
    case 4:
        if (strcmp(a[0], "!") == 0)
            return !test(a + 1, 3);
        break;
    }
    return grammar(a, n);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    const char *name = strrchr(argv[0], '/');
    name = name ? name + 1 : argv[0];
    if (strcmp(name, "[") == 0) {
        prog = "[";
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            fprintf(stderr, "[: missing ]\n");
            return 2;
        }
        argc--;
    }
    return test(argv + 1, argc - 1) ? 0 : 1;
}
