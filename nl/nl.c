/*
 * nl.c - nl: line numbering filter (POSIX.1-2024, XCU nl) [XSI]
 *
 * Synopsis:
 *   nl [-p] [-b type] [-d delim] [-f type] [-h type] [-i incr] [-l num]
 *      [-n format] [-s sep] [-v startnum] [-w width] [file]
 *
 * The input is read as logical pages of a header, a body and a footer,
 * each started by a line of nothing but the delimiter characters (three,
 * two or one times).  Types a, t, n and pBRE (regcomp(), basic regular
 * expressions) choose the lines numbered in each section; -l counts
 * adjacent empty lines for type a.
 *
 * Choices where the standard leaves room:
 *  - A section delimiter line is written as an empty line.
 *  - A new logical page, where the numbering starts again (unless -p),
 *    begins with a header, with a body that does not follow a header, and
 *    with a footer that follows neither a header nor a body; so the lines
 *    of one page are numbered on.  (GNU nl starts again at every
 *    delimiter.)
 *  - A line that is not numbered is written as it is, without number and
 *    separator, as the standard says.  (GNU nl indents it.)
 *  - With -l num, the empty lines that are not numbered do not count for
 *    the numbering; the count of adjacent empty lines starts again after
 *    each numbered one and after a line that is not empty.
 *  - -d with one character keeps ':' as the second; -d with more than two
 *    characters uses them all (the delimiter is that string).
 *  - Numbers are long; -v and -i may be negative.
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

struct type {
    char kind;                  /* a t n p */
    regex_t re;
};

static struct type types[3];    /* 0 header, 1 body, 2 footer */
static long incr = 1, start = 1, width = 6;
static long blanks_per = 1;
static const char *sep = "\t";
static char fmt[3] = "rn";
static bool opt_p;
static char *delim;
static size_t dlen;

static void usage(void)
{
    fprintf(stderr, "usage: nl [-p] [-b type] [-d delim] [-f type] [-h type] [-i incr] [-l num]\n"
                    "          [-n format] [-s sep] [-v startnum] [-w width] [file]\n");
    exit(1);
}

static void set_type(struct type *t, const char *s)
{
    if ((s[0] == 'a' || s[0] == 't' || s[0] == 'n') && !s[1]) {
        t->kind = s[0];
        return;
    }
    if (s[0] == 'p') {
        int e = regcomp(&t->re, s + 1, REG_NOSUB);
        if (e) {
            char msg[256];
            regerror(e, &t->re, msg, sizeof msg);
            fprintf(stderr, "nl: %s: %s\n", s + 1, msg);
            exit(1);
        }
        t->kind = 'p';
        return;
    }
    fprintf(stderr, "nl: %s: not a type (a, t, n or pstring)\n", s);
    exit(1);
}

static long number(const char *s, const char *opt, bool positive)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (!*s || *end || errno || (positive && v <= 0) ||
        !(isdigit((unsigned char)*s) || ((*s == '-' || *s == '+') && isdigit((unsigned char)s[1])))) {
        fprintf(stderr, "nl: -%s %s: not a valid number\n", opt, s);
        exit(1);
    }
    return v;
}

static void out(const char *s, size_t n)
{
    if (n && fwrite(s, 1, n, stdout) != n) {
        fprintf(stderr, "nl: write error: %s\n", strerror(errno));
        exit(1);
    }
}

/* the section a delimiter line starts (0 header, 1 body, 2 footer); -1 */
static int section_of(const char *s, size_t n)
{
    if (n == 0 || n % dlen != 0 || n / dlen > 3)
        return -1;
    for (size_t i = 0; i < n; i += dlen)
        if (memcmp(s + i, delim, dlen) != 0)
            return -1;
    return 3 - (int)(n / dlen);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    types[0].kind = 'n';
    types[1].kind = 't';
    types[2].kind = 'n';
    char dbuf[3] = "\\:";
    delim = dbuf;
    dlen = 2;
    int c;
    while ((c = getopt(argc, argv, "pb:d:f:h:i:l:n:s:v:w:")) != -1) {
        switch (c) {
        case 'p': opt_p = true; break;
        case 'b': set_type(&types[1], optarg); break;
        case 'h': set_type(&types[0], optarg); break;
        case 'f': set_type(&types[2], optarg); break;
        case 'd': {
            size_t n = strlen(optarg);
            if (n == 0) {
                fprintf(stderr, "nl: -d: the delimiter is empty\n");
                return 1;
            }
            int k = mblen(optarg, n);
            if (k <= 0)
                k = 1;
            if ((size_t)k == n) {
                /* one character: the second stays ':' */
                delim = malloc(n + 2);
                if (!delim) {
                    fprintf(stderr, "nl: out of memory\n");
                    return 1;
                }
                memcpy(delim, optarg, n);
                delim[n] = ':';
                delim[n + 1] = '\0';
                dlen = n + 1;
            } else {
                delim = optarg;
                dlen = n;
            }
            break;
        }
        case 'i': incr = number(optarg, "i", false); break;
        case 'l': blanks_per = number(optarg, "l", true); break;
        case 'n':
            if (strcmp(optarg, "ln") && strcmp(optarg, "rn") && strcmp(optarg, "rz")) {
                fprintf(stderr, "nl: -n %s: the format is ln, rn or rz\n", optarg);
                return 1;
            }
            memcpy(fmt, optarg, 3);
            break;
        case 's': sep = optarg; break;
        case 'v': start = number(optarg, "v", false); break;
        case 'w': width = number(optarg, "w", true); break;
        default: usage();
        }
    }
    if (argc - optind > 1)
        usage();
    FILE *fp = stdin;
    const char *name = "standard input";
    if (optind < argc && strcmp(argv[optind], "-") != 0) {
        name = argv[optind];
        if (!(fp = fopen(name, "r"))) {
            fprintf(stderr, "nl: %s: %s\n", name, strerror(errno));
            return 1;
        }
    }
    if (width > 1000000)
        width = 1000000;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int section = 1, last_section = -1;
    long num = start;
    long blank_run = 0;
    while ((n = getline(&line, &cap, fp)) >= 0) {
        bool nl = n > 0 && line[n - 1] == '\n';
        size_t len = (size_t)(nl ? n - 1 : n);
        int s = section_of(line, len);
        if (s >= 0) {
            bool continues = (s == 1 && last_section == 0) ||
                             (s == 2 && (last_section == 0 || last_section == 1));
            if (!continues && !opt_p)
                num = start;
            section = s;
            last_section = s;
            blank_run = 0;
            out("\n", 1);
            continue;
        }
        if (last_section < 0)
            last_section = 1;   /* text before any delimiter: a body */
        struct type *t = &types[section];
        bool numbered;
        line[len] = '\0';
        switch (t->kind) {
        case 'a':
            if (len == 0) {
                blank_run++;
                numbered = blank_run >= blanks_per;
                if (numbered)
                    blank_run = 0;
            } else {
                blank_run = 0;
                numbered = true;
            }
            break;
        case 't': numbered = len > 0; break;
        case 'p': numbered = regexec(&t->re, line, 0, NULL, 0) == 0; break;
        default: numbered = false; break;
        }
        if (numbered) {
            char buf[64];
            int w = (int)width;
            if (fmt[0] == 'l')
                printf("%-*ld", w, num);
            else if (fmt[1] == 'z') {
                /* leading zeros, the sign first */
                snprintf(buf, sizeof buf, "%ld", num < 0 ? -num : num);
                int digits = (int)strlen(buf), pad = w - digits - (num < 0);
                if (num < 0)
                    out("-", 1);
                for (; pad > 0; pad--)
                    out("0", 1);
                out(buf, strlen(buf));
            } else
                printf("%*ld", w, num);
            out(sep, strlen(sep));
            num += incr;
        }
        out(line, len);
        if (nl)
            out("\n", 1);
    }
    if (ferror(fp)) {
        fprintf(stderr, "nl: %s: %s\n", name, strerror(errno));
        return 1;
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "nl: write error: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
