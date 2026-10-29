/*
 * fold.c - fold: filter for folding lines (POSIX.1-2024, XCU fold)
 *
 * Synopsis: fold [-bs] [-w width] [file...]
 *
 * A line is held as a segment until it would grow past the width, then
 * written with a newline (with -s, up to its last blank, the rest going on
 * to the next segment).  Without -b, widths are display columns of the
 * locale's characters (wcwidth()); a character is never split; <tab>
 * goes to the next multiple of 8, <backspace> goes back one column (never
 * below 0) and <carriage-return> to column 0, and no break is made next to
 * the latter two.  With -b every byte is one position, and characters are
 * still not split.
 *
 * Choices where the standard leaves room:
 *  - A character that is not printable (wcwidth() < 0), and a byte that
 *    is not a character, count one column.
 *  - A width that is not a positive decimal number is an error.
 *  - A last line without a newline is written without one.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

static bool opt_b, opt_s;
static long width = 80;
static int status;

/* the segment being built: its bytes and, for each character, where it
   starts and the column after it */
static char *seg;
static size_t seglen, segcap;
struct ch {
    size_t off, len;
    long colafter;
    bool blank;
};
static struct ch *chs;
static size_t nch, capch;
static long col;

static void out(const char *s, size_t n)
{
    if (n && fwrite(s, 1, n, stdout) != n) {
        fprintf(stderr, "fold: write error: %s\n", strerror(errno));
        exit(1);
    }
}

static long advance(long c, const char *p, size_t len, wchar_t wc, bool valid)
{
    if (opt_b)
        return c + (long)len;
    if (len == 1 && *p == '\b')
        return c > 0 ? c - 1 : 0;
    if (len == 1 && *p == '\r')
        return 0;
    if (len == 1 && *p == '\t')
        return (c / 8 + 1) * 8;
    if (!valid)
        return c + 1;
    int w = wcwidth(wc);
    return c + (w < 0 ? 1 : w);
}

/* recompute the columns of the characters from index i on */
static void recolumn(size_t from)
{
    long c = 0;
    for (size_t i = 0; i < nch; i++) {
        if (i >= from) {
            wchar_t wc = 0;
            mbstate_t st;
            memset(&st, 0, sizeof st);
            size_t k = mbrtowc(&wc, seg + chs[i].off, chs[i].len, &st);
            bool valid = k == chs[i].len;
            chs[i].colafter = advance(c, seg + chs[i].off, chs[i].len, wc, valid);
        }
        c = chs[i].colafter;
    }
    col = nch ? chs[nch - 1].colafter : 0;
}

/* write the first n characters of the segment and a newline; keep the rest */
static void flush_part(size_t n)
{
    size_t bytes = n < nch ? chs[n].off : seglen;
    out(seg, bytes);
    out("\n", 1);
    memmove(seg, seg + bytes, seglen - bytes);
    seglen -= bytes;
    memmove(chs, chs + n, (nch - n) * sizeof *chs);
    nch -= n;
    for (size_t i = 0; i < nch; i++)
        chs[i].off -= bytes;
    recolumn(0);
}

static void add(const char *p, size_t len, wchar_t wc, bool valid)
{
    bool special = !opt_b && len == 1 && (*p == '\b' || *p == '\r');
    long next = advance(col, p, len, wc, valid);
    if (!special && next > width && nch > 0) {
        size_t cut = nch;
        if (opt_s) {
            for (size_t i = nch; i > 0; i--)
                if (chs[i - 1].blank) {
                    cut = i;
                    break;
                }
        }
        flush_part(cut);
        next = advance(col, p, len, wc, valid);
        if (next > width && nch > 0) {
            flush_part(nch);
            next = advance(col, p, len, wc, valid);
        }
    }
    if (seglen + len > segcap) {
        segcap = (seglen + len) * 2 + 64;
        if (!(seg = realloc(seg, segcap))) {
            fprintf(stderr, "fold: out of memory\n");
            exit(1);
        }
    }
    if (nch == capch) {
        capch = capch ? capch * 2 : 128;
        if (!(chs = realloc(chs, capch * sizeof *chs))) {
            fprintf(stderr, "fold: out of memory\n");
            exit(1);
        }
    }
    memcpy(seg + seglen, p, len);
    bool blank = opt_b ? (*p == ' ' || *p == '\t')
                       : valid && (len == 1 ? (*p == ' ' || *p == '\t') : iswblank((wint_t)wc));
    chs[nch++] = (struct ch){ seglen, len, next, blank };
    seglen += len;
    col = next;
}

static void fold(FILE *fp, const char *name)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, fp)) >= 0) {
        bool nl = n > 0 && line[n - 1] == '\n';
        size_t len = (size_t)(nl ? n - 1 : n);
        mbstate_t st;
        memset(&st, 0, sizeof st);
        for (size_t i = 0; i < len;) {
            wchar_t wc = 0;
            size_t k = 1;
            bool valid = false;
            {
                /* with -b too a character is never split */
                k = mbrtowc(&wc, line + i, len - i, &st);
                if (k == (size_t)-1 || k == (size_t)-2) {
                    memset(&st, 0, sizeof st);
                    k = 1;
                } else if (k == 0)
                    k = 1, valid = true;
                else
                    valid = true;
            }
            add(line + i, k, wc, valid);
            i += k;
        }
        out(seg, seglen);
        if (nl)
            out("\n", 1);
        seglen = 0;
        nch = 0;
        col = 0;
    }
    if (ferror(fp)) {
        fprintf(stderr, "fold: %s: %s\n", name, strerror(errno));
        status = 1;
    }
    free(line);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "bsw:")) != -1) {
        switch (c) {
        case 'b': opt_b = true; break;
        case 's': opt_s = true; break;
        case 'w': {
            char *end;
            errno = 0;
            width = strtol(optarg, &end, 10);
            if (!isdigit((unsigned char)*optarg) || *end || errno || width <= 0) {
                fprintf(stderr, "fold: -w %s: not a positive number\n", optarg);
                return 1;
            }
            break;
        }
        default:
            fprintf(stderr, "usage: fold [-bs] [-w width] [file...]\n");
            return 1;
        }
    }
    if (optind == argc)
        fold(stdin, "standard input");
    for (int i = optind; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            fold(stdin, "standard input");
            continue;
        }
        FILE *fp = fopen(argv[i], "r");
        if (!fp) {
            fprintf(stderr, "fold: %s: %s\n", argv[i], strerror(errno));
            status = 1;
            continue;
        }
        fold(fp, argv[i]);
        fclose(fp);
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "fold: write error: %s\n", strerror(errno));
        return 1;
    }
    return status;
}
