/*
 * unexpand.c - unexpand: convert spaces to tabs (POSIX.1-2024, XCU
 * unexpand)
 *
 * Synopsis: unexpand [-a|-t tablist] [file...]
 *
 * A run of blanks that may be converted is held back until it ends; a run
 * of two or more blanks, or one with a <tab>, is then written as the most
 * tabs and the fewest spaces that fill the same columns.  Blanks at the
 * start of a line (before anything else, a <backspace> included) may
 * always be converted; with -a, or with -t, all others too.  Columns count display widths (wcwidth());
 * <backspace> is copied and takes the column back one.
 *
 * Choices where the standard leaves room:
 *  - A run of a single <space> stays a space, at the start of a line too
 *    (a tab would fill no fewer columns).
 *  - Past the last stop of a list nothing is converted, and a <tab> there
 *    counts as moving to the next column.
 *  - A byte that is not a character, and a character that is not
 *    printable, count one column.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

static int status;

static void out(const char *prog, const char *s, size_t n)
{
    if (n && fwrite(s, 1, n, stdout) != n) {
        fprintf(stderr, "%s: write error: %s\n", prog, strerror(errno));
        exit(1);
    }
}

/* ---- the tab stops ---- */

static long *stops;             /* a list of stops (0-based columns) */
static size_t nstops;
static long every = 8;          /* or one every so many columns */

static void tab_list(const char *prog, const char *s)
{
    const char *p = s;
    long last = 0;
    size_t cap = 0;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        if (!isdigit((unsigned char)*p))
            goto bad;
        char *end;
        errno = 0;
        long v = strtol(p, &end, 10);
        if (errno || v <= 0 || (nstops && v <= last))
            goto bad;
        if (nstops == cap) {
            cap = cap ? cap * 2 : 8;
            if (!(stops = realloc(stops, cap * sizeof *stops))) {
                fprintf(stderr, "%s: out of memory\n", prog);
                exit(1);
            }
        }
        stops[nstops++] = v;
        last = v;
        p = end;
        if (*p && *p != ',' && *p != ' ' && *p != '\t')
            goto bad;
    }
    if (nstops == 0)
        goto bad;
    if (nstops == 1) {
        every = stops[0];
        nstops = 0;
    }
    return;
bad:
    fprintf(stderr, "%s: -t %s: not a valid list of tab stops\n", prog, s);
    exit(1);
}

/* the next tab stop after column c; -1 if there is none (past the list) */
static long next_stop(long c)
{
    if (nstops == 0)
        return (c / every + 1) * every;
    for (size_t i = 0; i < nstops; i++)
        if (stops[i] > c)
            return stops[i];
    return -1;
}

/* the display width of the character at p (len bytes, or not valid) */
static int char_width(const char *p, size_t len, bool valid, wchar_t wc)
{
    if (!valid || (len == 1 && (unsigned char)*p < 0x80))
        return 1;
    int w = wcwidth(wc);
    return w < 0 ? 1 : w;
}

static bool opt_a;

/* the run of blanks held back, and the column where it started */
static char *run;
static size_t nrun, caprun;
static long run_start;

/* write the run: if it is two or more blanks, or holds a tab, as the most
   tabs and then the fewest spaces that fill the same columns (up to col);
   otherwise as it is */
static void flush_run(long col)
{
    if (nrun == 0)
        return;
    if (nrun >= 2 || memchr(run, '\t', nrun)) {
        long c = run_start;
        for (;;) {
            long s = next_stop(c);
            if (s < 0 || s > col)
                break;
            out("unexpand", "\t", 1);
            c = s;
        }
        for (; c < col; c++)
            out("unexpand", " ", 1);
    } else
        out("unexpand", run, nrun);
    nrun = 0;
}

static void unexpand(FILE *fp, const char *name)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, fp)) >= 0) {
        long col = 0;
        bool leading = true;
        nrun = 0;
        mbstate_t st;
        memset(&st, 0, sizeof st);
        for (size_t i = 0; i < (size_t)n;) {
            char c = line[i];
            if (c == ' ' || c == '\t') {
                long s = next_stop(col);
                if ((leading || opt_a) && s >= 0) {
                    if (nrun == 0)
                        run_start = col;
                    if (nrun == caprun) {
                        caprun = caprun ? caprun * 2 : 64;
                        if (!(run = realloc(run, caprun))) {
                            fprintf(stderr, "unexpand: out of memory\n");
                            exit(1);
                        }
                    }
                    run[nrun++] = c;
                    col = c == '\t' ? s : col + 1;
                } else {
                    /* not converted: after the start without -a, or past the list */
                    flush_run(col);
                    out("unexpand", &c, 1);
                    col = c == '\t' && s >= 0 ? s : col + 1;
                }
                i++;
                continue;
            }
            flush_run(col);
            leading = false;
            if (c == '\b') {
                out("unexpand", "\b", 1);
                if (col > 0)
                    col--;
                i++;
                continue;
            }
            if (c == '\n') {
                out("unexpand", "\n", 1);
                i++;
                continue;
            }
            wchar_t wc = 0;
            size_t k = mbrtowc(&wc, line + i, (size_t)n - i, &st);
            bool valid = true;
            if (k == (size_t)-1 || k == (size_t)-2) {
                memset(&st, 0, sizeof st);
                k = 1;
                valid = false;
            } else if (k == 0)
                k = 1;
            out("unexpand", line + i, k);
            col += char_width(line + i, k, valid, wc);
            i += k;
        }
        flush_run(col);
    }
    if (ferror(fp)) {
        fprintf(stderr, "unexpand: %s: %s\n", name, strerror(errno));
        status = 1;
    }
    free(line);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    bool have_t = false;
    while ((c = getopt(argc, argv, "at:")) != -1) {
        if (c == 'a')
            opt_a = true;
        else if (c == 't') {
            tab_list("unexpand", optarg);
            have_t = true;
        } else {
            fprintf(stderr, "usage: unexpand [-a|-t tablist] [file...]\n");
            return 1;
        }
    }
    if (have_t)
        opt_a = true;           /* -t converts everywhere */
    if (optind == argc)
        unexpand(stdin, "standard input");
    for (int i = optind; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            unexpand(stdin, "standard input");
            continue;
        }
        FILE *fp = fopen(argv[i], "r");
        if (!fp) {
            fprintf(stderr, "unexpand: %s: %s\n", argv[i], strerror(errno));
            status = 1;
            continue;
        }
        unexpand(fp, argv[i]);
        fclose(fp);
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "unexpand: write error: %s\n", strerror(errno));
        return 1;
    }
    return status;
}
