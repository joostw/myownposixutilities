/*
 * expand.c - expand: convert tabs to spaces (POSIX.1-2024, XCU expand)
 *
 * Synopsis: expand [-t tablist] [file...]
 *
 * Columns count display widths (wcwidth()) of the locale's characters;
 * <backspace> is copied and takes the column back one (not below 0).  A
 * <tab> past the last stop of a list becomes one space.
 *
 * Choices where the standard leaves room:
 *  - A byte that is not a character, and a character that is not
 *    printable, count one column; so do the other control characters.
 *  - The list may also be a single stop given with separators ("4,").
 *  - A last line without a newline is written without one.
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

static void expand(FILE *fp, const char *name)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, fp)) >= 0) {
        long col = 0;
        mbstate_t st;
        memset(&st, 0, sizeof st);
        for (size_t i = 0; i < (size_t)n;) {
            char c = line[i];
            if (c == '\t') {
                long s = next_stop(col);
                if (s < 0)
                    s = col + 1;
                for (; col < s; col++)
                    out("expand", " ", 1);
                i++;
                continue;
            }
            if (c == '\b') {
                out("expand", "\b", 1);
                if (col > 0)
                    col--;
                i++;
                continue;
            }
            if (c == '\n') {
                out("expand", "\n", 1);
                col = 0;
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
            out("expand", line + i, k);
            col += char_width(line + i, k, valid, wc);
            i += k;
        }
    }
    if (ferror(fp)) {
        fprintf(stderr, "expand: %s: %s\n", name, strerror(errno));
        status = 1;
    }
    free(line);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "t:")) != -1) {
        if (c == 't')
            tab_list("expand", optarg);
        else {
            fprintf(stderr, "usage: expand [-t tablist] [file...]\n");
            return 1;
        }
    }
    if (optind == argc)
        expand(stdin, "standard input");
    for (int i = optind; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            expand(stdin, "standard input");
            continue;
        }
        FILE *fp = fopen(argv[i], "r");
        if (!fp) {
            fprintf(stderr, "expand: %s: %s\n", argv[i], strerror(errno));
            status = 1;
            continue;
        }
        expand(fp, argv[i]);
        fclose(fp);
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "expand: write error: %s\n", strerror(errno));
        return 1;
    }
    return status;
}
