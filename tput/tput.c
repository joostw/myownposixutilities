/*
 * tput.c - tput: change terminal characteristics (POSIX.1-2024, XCU tput)
 *
 * Synopsis: tput [-T type] operand...
 *
 * The information comes from the terminal's compiled terminfo entry
 * (terminfo.c).  The operands of the standard:
 *   clear   clear_screen;
 *   init    init_1string, init_2string, the tab stops every 8 columns
 *           (when the terminal's are not, and it can clear and set them),
 *           the contents of init_file, init_3string;
 *   reset   the same with reset_1string, reset_2string, reset_file and
 *           reset_3string, each one falling back to its init capability.
 * and, as other implementations do, any capability name: a boolean gives
 * the exit status (0 true, 1 false), a number is written ("-1" if
 * absent), a string is written with the operands after it as its
 * parameters (as many as it uses, %s ones as strings; with none given, it
 * is written as it is), and longname writes the terminal's long name
 * (without a newline, as in ncurses).  Padding ($<...>) is left out.
 *
 * Choices where the standard leaves room:
 *  - The default terminal type (no -T, TERM unset or empty) is "dumb".
 *  - init and reset write strings only: they do not run init_prog or
 *    change the terminal modes.
 *  - lines and cols are, as in other implementations, LINES and COLUMNS
 *    when set to positive numbers, else the window size of the terminal on
 *    the standard output, error or input (the TIOCGWINSZ request of
 *    ioctl()), else the terminfo values.
 *  - A string that is absent is not an error for clear, init and reset
 *    (as the standard says) but gives status 1 for a capability name.
 *  - Exit statuses: 2 usage, 3 an unknown terminal type (or an entry that
 *    cannot be read), 4 an unknown capability or a parameter that is not
 *    a number, 5 a write error.  The operands are done in order; the first
 *    error of 3 or more stops.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "terminfo.h"

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static struct terminfo ti;

static const char *str(const char *cap)
{
    long n;
    const char *s = NULL;
    return ti_find(&ti, cap, &n, &s) == TI_STR ? s : NULL;
}

static long num(const char *cap)
{
    long n = -1;
    const char *s;
    return ti_find(&ti, cap, &n, &s) == TI_NUM ? n : -1;
}

static bool put(const char *s)
{
    if (!s)
        return true;
    if (strchr(s, '%')) {
        long p[9] = { 0 };
        char *r = ti_tparm(s, p, NULL);
        bool ok = r && ti_put(r);
        free(r);
        return ok;
    }
    return ti_put(s);
}

static bool put_file(const char *path)
{
    if (!path)
        return true;
    FILE *f = fopen(path, "r");
    if (!f)
        return true;            /* nothing to write */
    int c;
    bool ok = true;
    while ((c = getc(f)) != EOF)
        if (putchar(c) == EOF) {
            ok = false;
            break;
        }
    fclose(f);
    return ok;
}

/* tab stops every 8 columns, if the terminal's are not and it can */
static bool put_tabs(void)
{
    long it = num("it"), cols = num("cols");
    const char *tbc = str("tbc"), *hts = str("hts"), *cr = str("cr");
    if (it == 8 || it < 0 || !tbc || !hts)
        return true;
    if (cols <= 0)
        cols = 80;
    bool ok = put(cr ? cr : "\r") && put(tbc);
    for (long c = 8; ok && c < cols; c += 8) {
        for (int i = 0; i < 8 && ok; i++)
            ok = putchar(' ') != EOF;
        ok = ok && put(hts);
    }
    return ok && put(cr ? cr : "\r");
}

static bool init_or_reset(bool reset)
{
    const char *one = reset && str("rs1") ? str("rs1") : str("is1");
    const char *two = reset && str("rs2") ? str("rs2") : str("is2");
    const char *file = reset && str("rf") ? str("rf") : str("if");
    const char *three = reset && str("rs3") ? str("rs3") : str("is3");
    return put(one) && put(two) && put_tabs() && put_file(file) && put(three);
}

static long window(bool rows)
{
    const char *e = getenv(rows ? "LINES" : "COLUMNS");
    if (e && *e) {
        char *end;
        long v = strtol(e, &end, 10);
        if (!*end && v > 0)
            return v;
    }
    struct winsize w;
    for (int fd = 1; fd <= 3; fd++)
        if (ioctl(fd % 3, TIOCGWINSZ, &w) == 0 && (rows ? w.ws_row : w.ws_col) > 0)
            return rows ? w.ws_row : w.ws_col;
    return -1;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    const char *type = NULL;
    int c;
    while ((c = getopt(argc, argv, "T:")) != -1) {
        if (c != 'T') {
            fprintf(stderr, "usage: tput [-T type] operand...\n");
            return 2;
        }
        type = optarg;
    }
    if (optind == argc) {
        fprintf(stderr, "usage: tput [-T type] operand...\n");
        return 2;
    }
    if (!type) {
        type = getenv("TERM");
        if (!type || !*type)
            type = "dumb";
    }
    int r = ti_load(type, &ti);
    if (r == -1) {
        fprintf(stderr, "tput: %s: unknown terminal type\n", type);
        return 3;
    }
    if (r < 0) {
        fprintf(stderr, "tput: %s: the terminfo entry cannot be read\n", type);
        return 3;
    }
    int status = 0;
    bool ok = true;
    for (int i = optind; i < argc && ok; i++) {
        const char *op = argv[i];
        if (strcmp(op, "clear") == 0)
            ok = put(str("clear"));
        else if (strcmp(op, "init") == 0)
            ok = init_or_reset(false);
        else if (strcmp(op, "reset") == 0)
            ok = init_or_reset(true);
        else if (strcmp(op, "longname") == 0)
            ok = fputs(ti_longname(&ti), stdout) != EOF;
        else {
            long n;
            const char *s = NULL;
            switch (ti_find(&ti, op, &n, &s)) {
            case TI_ABSENT:
                fprintf(stderr, "tput: %s: unknown capability\n", op);
                status = 4;
                ok = false;
                break;
            case TI_BOOL:
                status = n ? 0 : 1;
                break;
            case TI_NUM:
                if (strcmp(op, "lines") == 0 || strcmp(op, "cols") == 0) {
                    long w = window(op[0] == 'l');
                    if (w > 0)
                        n = w;
                }
                ok = printf("%ld\n", n) >= 0;
                break;
            case TI_STR: {
                bool isstr[9];
                int np = ti_nparams(s, isstr);
                long p[9] = { 0 };
                char *sp[9] = { NULL };
                int given = 0;
                for (int k = 0; k < np && i + 1 < argc; k++, given++) {
                    const char *a = argv[++i];
                    if (isstr[k])
                        sp[k] = (char *)a;
                    else {
                        char *end;
                        errno = 0;
                        p[k] = strtol(a, &end, 10);
                        if (!*a || *end || errno) {
                            fprintf(stderr, "tput: %s: %s: not a number\n", op, a);
                            return 4;
                        }
                    }
                }
                if (!s) {
                    status = 1;
                    break;
                }
                if (given > 0) {
                    char *t = ti_tparm(s, p, sp);
                    if (!t) {
                        fprintf(stderr, "tput: out of memory\n");
                        return 5;
                    }
                    ok = ti_put(t);
                    free(t);
                } else
                    ok = ti_put(s);
                break;
            }
            }
            if (status >= 3)
                break;
        }
    }
    if (fflush(stdout) != 0 || ferror(stdout) || (!ok && status < 3)) {
        fprintf(stderr, "tput: write error: %s\n", strerror(errno));
        return 5;
    }
    return status;
}
