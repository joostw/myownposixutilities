/*
 * tabs.c - tabs: set terminal tabs (POSIX.1-2024, XCU tabs)
 *
 * Synopsis:
 *   tabs [-n|-a|-a2|-c|-c2|-c3|-f|-p|-s|-u] [-T type]     [XSI: the lists]
 *   tabs [-T type] n[[sep[+]n]...]
 *
 * The terminal's capabilities come from its compiled terminfo entry
 * (terminfo.c, the same as tput's): the tab stops are cleared with
 * clear_all_tabs, then set with set_tab, the cursor moved to each column
 * with spaces after a carriage_return; another carriage_return ends.
 *
 * Choices where the standard leaves room:
 *  - The values of a list, and of the lists of -a ... -u, are column
 *    numbers counted from 1, as in historical tabs and in ncurses: 1 is the
 *    first column.  -n sets stops every n columns from the first: at
 *    columns 1, n+1, 2n+1, ... (tab-stop positions n, 2n, ... and a stop at
 *    the first column, as ncurses sets them).
 *  - Stops are set up to the terminal's columns (80 if it has none); a
 *    stop of a list past the last column is set at the last column.  The
 *    output is that of ncurses' tabs, byte for byte.
 *  - Of several tab specifications, the last one counts.  A list may also
 *    be given as several operands, which are joined.
 *  - The default terminal type (no -T, TERM unset or empty) is "dumb".
 *  - A terminal that cannot clear tab stops, or set them (for -0, only
 *    clear), and an unknown terminal type: a diagnostic, status 1.  A list
 *    that is not valid and an unknown option are usage errors (status 2).
 */

#define _POSIX_C_SOURCE 200809L

#include "terminfo.h"

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    const char *opt;
    const char *list;
} canned[] = {
    { "a", "1,10,16,36,72" }, { "a2", "1,10,16,40,72" }, { "c", "1,8,12,16,20,55" },
    { "c2", "1,6,10,14,49" }, { "c3", "1,6,10,14,18,22,26,30,34,38,42,46,50,54,58,62,67" },
    { "f", "1,7,11,15,19,23" }, { "p", "1,5,9,13,17,21,25,29,33,37,41,45,49,53,57,61" },
    { "s", "1,10,55" }, { "u", "1,12,20,44" },
};

static void usage(void)
{
    fprintf(stderr, "usage: tabs [-n|-a|-a2|-c|-c2|-c3|-f|-p|-s|-u] [-T type]\n"
                    "       tabs [-T type] n[[sep[+]n]...]\n");
    exit(2);
}

static long stops[1024];
static int nstops;

/* a list of columns: n[[sep[+]n]...], sep a comma or blanks */
static bool parse_list(const char *s)
{
    nstops = 0;
    const char *p = s;
    long prev = 0;
    while (*p) {
        bool plus = false;
        if (*p == '+') {
            if (nstops == 0)
                return false;
            plus = true;
            p++;
        }
        if (!isdigit((unsigned char)*p))
            return false;
        char *end;
        errno = 0;
        long v = strtol(p, &end, 10);
        if (errno || v > 100000)
            return false;
        if (plus)
            v += prev;
        if (v <= 0 || v <= prev || nstops == (int)(sizeof stops / sizeof stops[0]))
            return false;
        stops[nstops++] = prev = v;
        p = end;
        if (!*p)
            break;
        if (*p == ',')
            p++;
        else if (*p == ' ' || *p == '\t')
            while (*p == ' ' || *p == '\t')
                p++;
        else
            return false;
        if (!*p)
            return false;
    }
    return nstops > 0;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    const char *type = NULL;
    const char *list = NULL;
    char *joined = NULL;
    int every = 8;              /* -n; -1: a list */
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (a[0] != '-' || !a[1])
            break;
        if (a[1] == 'T') {
            if (a[2])
                type = a + 2;
            else if (++i < argc)
                type = argv[i];
            else
                usage();
            continue;
        }
        if (isdigit((unsigned char)a[1]) && !a[2]) {
            every = a[1] - '0';
            list = NULL;
            continue;
        }
        size_t k;
        for (k = 0; k < sizeof canned / sizeof canned[0]; k++)
            if (strcmp(a + 1, canned[k].opt) == 0)
                break;
        if (k == sizeof canned / sizeof canned[0]) {
            fprintf(stderr, "tabs: %s: unknown option\n", a);
            usage();
        }
        list = canned[k].list;
        every = -1;
    }
    if (i < argc) {
        /* the operand: a list (several operands are joined with blanks) */
        size_t len = 1;
        for (int k = i; k < argc; k++)
            len += strlen(argv[k]) + 1;
        joined = malloc(len);
        if (!joined) {
            fprintf(stderr, "tabs: out of memory\n");
            return 1;
        }
        joined[0] = '\0';
        for (int k = i; k < argc; k++) {
            if (k > i)
                strcat(joined, " ");
            strcat(joined, argv[k]);
        }
        list = joined;
        every = -1;
    }
    if (!type) {
        type = getenv("TERM");
        if (!type || !*type)
            type = "dumb";
    }
    struct terminfo ti;
    int r = ti_load(type, &ti);
    if (r < 0) {
        fprintf(stderr, "tabs: %s: %s\n", type, r == -1 ? "unknown terminal type" :
                "the terminfo entry cannot be read");
        return 1;
    }
    long n, cols = -1;
    const char *tbc = NULL, *hts = NULL, *cr = NULL, *s;
    if (ti_find(&ti, "tbc", &n, &s) == TI_STR)
        tbc = s;
    if (ti_find(&ti, "hts", &n, &s) == TI_STR)
        hts = s;
    if (ti_find(&ti, "cr", &n, &s) == TI_STR)
        cr = s;
    if (ti_find(&ti, "cols", &n, &s) == TI_NUM)
        cols = n;
    if (cols <= 0)
        cols = 80;

    if (every < 0) {
        if (!parse_list(list)) {
            fprintf(stderr, "tabs: %s: not a valid list of tab stops\n", list);
            usage();
        }
        /* a stop past the last column is set at the last column */
        for (int k = 0; k < nstops; k++)
            if (stops[k] > cols)
                stops[k] = cols;
    } else {
        /* 1, n+1, 2n+1, ..., and the first one past the last column */
        nstops = 0;
        if (every > 0)
            for (long c = 1; nstops < 1024; c += every) {
                stops[nstops++] = c;
                if (c > cols)
                    break;
            }
    }
    if (!tbc || (nstops > 0 && !hts)) {
        fprintf(stderr, "tabs: %s: the terminal cannot %s tab stops\n", type,
                !tbc ? "clear" : "set");
        return 1;
    }
    if (!cr)
        cr = "\r";
    /* the stops: spaces to each column (not past the last), a set_tab
       there, as ncurses' tabs does it */
    bool ok = ti_put(tbc) && ti_put(cr);
    long last = 1;
    for (int k = 0; k < nstops && ok; k++) {
        long stop = stops[k];
        while (ok && last < stop) {
            if (++last > cols)
                break;
            ok = putchar(' ') != EOF;
        }
        if (stop > cols)
            break;
        ok = ok && ti_put(hts);
        last = stop;
    }
    if (nstops > 0)
        ok = ok && ti_put(cr);
    if (!ok || fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "tabs: write error: %s\n", strerror(errno));
        return 1;
    }
    free(joined);
    return 0;
}
