/*
 * csplit.c - csplit: split files based on context (POSIX.1-2024, XCU
 * csplit)
 *
 * Synopsis: csplit [-ks] [-f prefix] [-n number] file arg...
 *
 * The file is read whole into memory (it may be the standard input).  Each
 * arg makes a piece of the lines from the current line up to, not
 * including, the line it selects, and makes that line the current one; the
 * lines that are left after the last arg are the last piece.  The pieces
 * are prefix (xx) and a number of number (2) digits: 00, 01, ...; the size
 * of each is written as it is made.
 *
 * Choices where the standard leaves room:
 *  - A line_no that is the current line makes an empty piece; one before
 *    it, or after the last line, is an error.  A /rexp/ with an offset may
 *    select the line after the last one (the piece takes all the rest, and
 *    the last piece is empty); before the current line it is an error.
 *  - {num} after a line_no adds line_no to the line selected before, num
 *    times; after /rexp/ or %rexp% it searches again, num times.
 *  - The last piece is made even when it is empty.
 *  - On an error the pieces made are removed (unless -k), and the status
 *    is 1; usage errors have status 2 and make nothing.  A prefix that
 *    would make names longer than {NAME_MAX}, or more pieces than number
 *    digits can name, is an error.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool opt_k, opt_s;
static const char *prefix = "xx";
static long digits = 2;

static char **lines;            /* each with its newline, if it has one */
static size_t *lens;
static size_t nlines;

static char **made;             /* the pieces made, to remove on an error */
static size_t nmade;
static unsigned long npieces;

static void cleanup_and_exit(void)
{
    if (!opt_k)
        for (size_t i = 0; i < nmade; i++)
            unlink(made[i]);
    exit(1);
}

static void fail(const char *fmt, const char *arg)
{
    fprintf(stderr, "csplit: ");
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    cleanup_and_exit();
}

/* write lines [from, to) to the next piece */
static void piece(size_t from, size_t to)
{
    unsigned long max = 1;
    for (long i = 0; i < digits && max < 1000000000UL; i++)
        max *= 10;
    if (npieces >= max)
        fail("%s: more pieces than the digits of -n can name", prefix);
    size_t n = strlen(prefix) + (size_t)digits + 1;
    char *name = malloc(n);
    if (!name)
        fail("%s", "out of memory");
    snprintf(name, n, "%s%0*lu", prefix, (int)digits, npieces++);
    FILE *f = fopen(name, "w");
    if (!f) {
        fprintf(stderr, "csplit: %s: %s\n", name, strerror(errno));
        free(name);
        cleanup_and_exit();
    }
    made = realloc(made, (nmade + 1) * sizeof *made);
    if (!made)
        fail("%s", "out of memory");
    made[nmade++] = name;
    unsigned long long size = 0;
    for (size_t i = from; i < to; i++) {
        if (fwrite(lines[i], 1, lens[i], f) != lens[i])
            break;
        size += lens[i];
    }
    if (fflush(f) != 0 || ferror(f) || fclose(f) != 0) {
        fprintf(stderr, "csplit: %s: %s\n", name, strerror(errno));
        cleanup_and_exit();
    }
    if (!opt_s) {
        printf("%llu\n", size);
        fflush(stdout);
    }
}

static void usage(void)
{
    fprintf(stderr, "usage: csplit [-ks] [-f prefix] [-n number] file arg...\n");
    exit(2);
}

/* an argument */
struct arg {
    char kind;                  /* '/', '%', 'n' (line_no) */
    regex_t re;
    long offset;
    unsigned long line;
    unsigned long repeat;       /* {num} after it */
    const char *text;
};

static bool parse_offset(const char *s, long *off)
{
    if (!*s) {
        *off = 0;
        return true;
    }
    const char *p = s;
    if (*p == '+' || *p == '-')
        p++;
    if (!*p || strspn(p, "0123456789") != strlen(p))
        return false;
    errno = 0;
    *off = strtol(s, NULL, 10);
    return errno == 0;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "ksf:n:")) != -1) {
        switch (c) {
        case 'k': opt_k = true; break;
        case 's': opt_s = true; break;
        case 'f': prefix = optarg; break;
        case 'n': {
            char *end;
            errno = 0;
            digits = strtol(optarg, &end, 10);
            if (!isdigit((unsigned char)optarg[0]) || *end || errno || digits < 1 || digits > 9) {
                fprintf(stderr, "csplit: -n %s: not a valid number (1 to 9)\n", optarg);
                return 2;
            }
            break;
        }
        default: usage();
        }
    }
    if (argc - optind < 2)
        usage();
    const char *file = argv[optind];

    /* the arguments, checked before anything is made */
    int nargs = argc - optind - 1;
    struct arg *args = calloc((size_t)nargs, sizeof *args);
    int na = 0;
    if (!args)
        return 1;
    for (int i = 0; i < nargs; i++) {
        const char *a = argv[optind + 1 + i];
        if (a[0] == '{') {
            size_t l = strlen(a);
            if (na == 0 || l < 3 || a[l - 1] != '}' || strspn(a + 1, "0123456789") != l - 2) {
                fprintf(stderr, "csplit: %s: not a valid repeat\n", a);
                return 2;
            }
            args[na - 1].repeat = strtoul(a + 1, NULL, 10);
            continue;
        }
        struct arg *g = &args[na++];
        g->text = a;
        if (a[0] == '/' || a[0] == '%') {
            /* the expression: up to the delimiter not after a backslash */
            char delim = a[0];
            size_t l = strlen(a);
            char *re = malloc(l + 1);
            if (!re)
                return 1;
            size_t j = 0, k = 1;
            for (; k < l && a[k] != delim; k++) {
                if (a[k] == '\\' && a[k + 1] == delim) {
                    re[j++] = delim;
                    k++;
                } else if (a[k] == '\\' && a[k + 1]) {
                    re[j++] = a[k++];
                    re[j++] = a[k];
                } else
                    re[j++] = a[k];
            }
            re[j] = '\0';
            if (k >= l || !parse_offset(a + k + 1, &g->offset)) {
                fprintf(stderr, "csplit: %s: not a valid argument\n", a);
                return 2;
            }
            int e = regcomp(&g->re, re, REG_NOSUB);
            if (e) {
                char msg[256];
                regerror(e, &g->re, msg, sizeof msg);
                fprintf(stderr, "csplit: %s: %s\n", a, msg);
                return 2;
            }
            free(re);
            g->kind = delim;
        } else if (isdigit((unsigned char)a[0]) && strspn(a, "0123456789") == strlen(a)) {
            errno = 0;
            g->line = strtoul(a, NULL, 10);
            if (errno || g->line == 0) {
                fprintf(stderr, "csplit: %s: not a valid line number\n", a);
                return 2;
            }
            g->kind = 'n';
        } else {
            fprintf(stderr, "csplit: %s: not a valid argument\n", a);
            return 2;
        }
    }
    /* the names' length */
    const char *slash = strrchr(prefix, '/');
    char *dir = slash ? strndup(prefix, (size_t)(slash - prefix) + (slash == prefix)) : strdup(".");
    long name_max = dir ? pathconf(dir, _PC_NAME_MAX) : -1;
    free(dir);
    if (name_max < 0)
        name_max = NAME_MAX;
    if ((long)strlen(slash ? slash + 1 : prefix) + digits > name_max) {
        fprintf(stderr, "csplit: %s: the names would be longer than {NAME_MAX} (%ld)\n", prefix, name_max);
        return 1;
    }

    /* the file */
    FILE *in = stdin;
    if (strcmp(file, "-") != 0 && !(in = fopen(file, "r"))) {
        fprintf(stderr, "csplit: %s: %s\n", file, strerror(errno));
        return 1;
    }
    size_t cap = 0;
    char *line = NULL;
    size_t lcap = 0;
    ssize_t n;
    while ((n = getline(&line, &lcap, in)) >= 0) {
        if (nlines == cap) {
            cap = cap ? cap * 2 : 1024;
            lines = realloc(lines, cap * sizeof *lines);
            lens = realloc(lens, cap * sizeof *lens);
            if (!lines || !lens) {
                fprintf(stderr, "csplit: out of memory\n");
                return 1;
            }
        }
        lines[nlines] = malloc((size_t)n + 1);
        if (!lines[nlines]) {
            fprintf(stderr, "csplit: out of memory\n");
            return 1;
        }
        memcpy(lines[nlines], line, (size_t)n + 1);
        lens[nlines++] = (size_t)n;
    }
    if (ferror(in)) {
        fprintf(stderr, "csplit: %s: %s\n", file, strerror(errno));
        return 1;
    }

    size_t cur = 0;
    bool searched = false;
    for (int i = 0; i < na; i++) {
        struct arg *g = &args[i];
        unsigned long times = g->repeat + 1;
        size_t prev_line = 0;
        for (unsigned long r = 0; r < times; r++) {
            size_t target;
            if (g->kind == 'n') {
                unsigned long want = r == 0 ? g->line : prev_line + g->line;
                if (want - 1 < cur && !(r == 0 && want - 1 == cur))
                    fail("%s: the line is before the current one", g->text);
                if (want > nlines)
                    fail("%s: the line is past the end of the file", g->text);
                target = want - 1;
                prev_line = want;
            } else {
                size_t start = cur == 0 && !searched ? 0 : cur + 1;
                searched = true;
                size_t m = start;
                for (; m < nlines; m++) {
                    char *s = lines[m];
                    size_t l = lens[m];
                    char save = 0;
                    if (l > 0 && s[l - 1] == '\n') {
                        save = '\n';
                        s[l - 1] = '\0';
                    }
                    int hit = regexec(&g->re, s, 0, NULL, 0) == 0;
                    if (save)
                        s[l - 1] = save;
                    if (hit)
                        break;
                }
                if (m >= nlines)
                    fail("%s: no match", g->text);
                long t = (long)m + g->offset;
                if (t < (long)cur || t > (long)nlines)
                    fail("%s: the offset is outside the file's lines", g->text);
                target = (size_t)t;
            }
            if (g->kind != '%')
                piece(cur, target);
            cur = target;
        }
    }
    piece(cur, nlines);
    return 0;
}
