/*
 * paste.c - paste: merge corresponding or subsequent lines of files
 * (POSIX.1-2024, XCU paste)
 *
 * Synopsis: paste [-s] [-d list] file...
 *
 * Each "-" reads the next line of the standard input, in turn with the
 * other operands.  Lines may be of any length.  The elements of -d are
 * characters (multibyte ones too) or \n, \t, \\ and \0 (the empty string).
 *
 * Choices where the standard leaves room:
 *  - In -d, a backslash before any other character stands for that
 *    character; a backslash at the end of the list for itself.
 *  - A last line without a newline is taken as a line.
 *  - Without operands paste reads the standard input (as if given "-").
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct elem {
    char s[16];                 /* the delimiter, possibly empty */
    size_t len;
};

static struct elem *delims;
static size_t ndelims;

static void parse_list(const char *s)
{
    size_t n = strlen(s);
    delims = malloc((n + 1) * sizeof *delims);
    if (!delims) {
        fprintf(stderr, "paste: out of memory\n");
        exit(1);
    }
    ndelims = 0;
    mblen(NULL, 0);
    for (size_t i = 0; i < n;) {
        struct elem e = { "", 0 };
        if (s[i] == '\\' && i + 1 < n) {
            char c = s[i + 1];
            if (c == 'n') e.s[0] = '\n', e.len = 1;
            else if (c == 't') e.s[0] = '\t', e.len = 1;
            else if (c == '\\') e.s[0] = '\\', e.len = 1;
            else if (c == '0') e.len = 0;
            else e.s[0] = c, e.len = 1;
            i += 2;
        } else {
            int k = mblen(s + i, n - i);
            if (k <= 0 || k > 15)
                k = 1;
            memcpy(e.s, s + i, (size_t)k);
            e.len = (size_t)k;
            i += (size_t)k;
        }
        delims[ndelims++] = e;
    }
}

static void wr(const char *s, size_t n)
{
    if (n && fwrite(s, 1, n, stdout) != n) {
        fprintf(stderr, "paste: write error: %s\n", strerror(errno));
        exit(1);
    }
}

/* the next line of fp without its newline; false at the end */
static bool get(FILE *fp, const char *name, char **line, size_t *cap, size_t *len)
{
    ssize_t n = getline(line, cap, fp);
    if (n < 0) {
        if (ferror(fp)) {
            fprintf(stderr, "paste: %s: %s\n", name, strerror(errno));
            exit(1);
        }
        return false;
    }
    if (n > 0 && (*line)[n - 1] == '\n')
        n--;
    *len = (size_t)n;
    return true;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool opt_s = false;
    const char *list = "\t";
    int c;
    while ((c = getopt(argc, argv, "d:s")) != -1) {
        if (c == 'd')
            list = optarg;
        else if (c == 's')
            opt_s = true;
        else {
            fprintf(stderr, "usage: paste [-s] [-d list] file...\n");
            return 1;
        }
    }
    parse_list(list);
    static char *dash[] = { "-" };
    char **files = argv + optind;
    int nfiles = argc - optind;
    if (nfiles == 0) {
        files = dash;
        nfiles = 1;
    }
    FILE **fp = malloc((size_t)nfiles * sizeof *fp);
    bool *done = calloc((size_t)nfiles, sizeof *done);
    if (!fp || !done) {
        fprintf(stderr, "paste: out of memory\n");
        return 1;
    }
    char *line = NULL;
    size_t cap = 0, len;
    if (opt_s) {
        int status = 0;
        for (int i = 0; i < nfiles; i++) {
            FILE *f = strcmp(files[i], "-") == 0 ? stdin : fopen(files[i], "r");
            if (!f) {
                fprintf(stderr, "paste: %s: %s\n", files[i], strerror(errno));
                status = 1;
                continue;
            }
            size_t d = 0;
            bool first = true;
            while (get(f, files[i], &line, &cap, &len)) {
                if (!first && ndelims) {
                    wr(delims[d].s, delims[d].len);
                    d = (d + 1) % ndelims;
                }
                wr(line, len);
                first = false;
            }
            wr("\n", 1);
            if (f != stdin)
                fclose(f);
        }
        if (fflush(stdout) != 0) {
            fprintf(stderr, "paste: write error: %s\n", strerror(errno));
            return 1;
        }
        return status;
    }
    for (int i = 0; i < nfiles; i++) {
        fp[i] = strcmp(files[i], "-") == 0 ? stdin : fopen(files[i], "r");
        if (!fp[i]) {
            fprintf(stderr, "paste: %s: %s\n", files[i], strerror(errno));
            return 1;
        }
    }
    int left = nfiles;
    for (;;) {
        /* read a line from each file first: stop when all are at the end */
        char **lines = NULL;
        size_t *lens = NULL;
        lines = malloc((size_t)nfiles * sizeof *lines);
        lens = malloc((size_t)nfiles * sizeof *lens);
        if (!lines || !lens) {
            fprintf(stderr, "paste: out of memory\n");
            return 1;
        }
        bool any = false;
        for (int i = 0; i < nfiles; i++) {
            lines[i] = NULL;
            lens[i] = 0;
            if (done[i])
                continue;
            size_t lcap = 0;
            if (get(fp[i], files[i], &lines[i], &lcap, &lens[i]))
                any = true;
            else {
                done[i] = true;
                left--;
            }
        }
        if (!any) {
            for (int i = 0; i < nfiles; i++)
                free(lines[i]);
            free(lines);
            free(lens);
            break;
        }
        size_t d = 0;
        for (int i = 0; i < nfiles; i++) {
            if (lines[i])
                wr(lines[i], lens[i]);
            if (i + 1 < nfiles && ndelims) {
                wr(delims[d].s, delims[d].len);
                d = (d + 1) % ndelims;
            }
            free(lines[i]);
        }
        wr("\n", 1);
        free(lines);
        free(lens);
    }
    (void)left;
    if (fflush(stdout) != 0) {
        fprintf(stderr, "paste: write error: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
