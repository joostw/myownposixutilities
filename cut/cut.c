/*
 * cut.c - cut: cut out selected fields of each line of a file
 *   (POSIX.1-2024, XCU cut)
 *
 * Synopsis: cut -b list [-n] [file...]
 *           cut -c list [file...]
 *           cut -f list [-d delim] [-s] [file...]
 *
 * list: <comma>- or <blank>-separated numbers and ranges (n, lo-hi, -hi,
 * lo-), in any order and overlapping; what they select is written once,
 * in input order.  A decreasing range (5-2) is an error.
 *
 * -c counts characters of the locale; a byte that does not begin a
 * valid character counts as one character.  -d takes one character,
 * which may be a multibyte one.  -b -n adjusts each element of list
 * per line as the standard describes, so that no character is split.
 * Lines are of unlimited length; a last line without a <newline> is
 * written with one.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#define TO_END LONG_MAX

typedef struct { long lo, hi; } Range;      /* 1-based, inclusive */

static Range *ranges;       /* as given (for -n) */
static size_t nranges;
static Range *merged;       /* sorted, merged */
static size_t nmerged;

enum mode { MODE_NONE, MODE_B, MODE_C, MODE_F };

static enum mode mode = MODE_NONE;
static bool opt_n, opt_s;
static char delim[MB_LEN_MAX + 1] = "\t";
static size_t delim_len = 1;
static bool mb;             /* the locale has multibyte characters */
static int status;

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        perror("cut");
        exit(1);
    }
    return q;
}

static void usage(void)
{
    fputs("usage: cut -b list [-n] [file...]\n"
          "       cut -c list [file...]\n"
          "       cut -f list [-d delim] [-s] [file...]\n", stderr);
    exit(1);
}

static void bad_list(const char *list)
{
    fprintf(stderr, "cut: invalid list: '%s'\n", list);
    exit(1);
}

/* a positive decimal number (saturating), or 0 if there is none */
static long number(const char **sp)
{
    const char *s = *sp;
    long v = 0;
    while (*s >= '0' && *s <= '9') {
        int d = *s++ - '0';
        v = v > (LONG_MAX - 1 - d) / 10 ? LONG_MAX - 1 : v * 10 + d;
    }
    if (s == *sp)
        return 0;
    *sp = s;
    return v ? v : -1;      /* -1: a zero, which is not positive */
}

static int cmp_range(const void *a, const void *b)
{
    const Range *x = a, *y = b;
    return x->lo < y->lo ? -1 : x->lo > y->lo;
}

static void parse_list(const char *list)
{
    const char *s = list;
    nranges = 0;
    for (;;) {
        long lo, hi;
        if (*s == '-') {
            s++;
            lo = 1;
            hi = number(&s);
            if (hi <= 0)
                bad_list(list);
        } else {
            lo = number(&s);
            if (lo <= 0)
                bad_list(list);
            hi = lo;
            if (*s == '-') {
                s++;
                if (*s >= '0' && *s <= '9') {
                    hi = number(&s);
                    if (hi <= 0 || hi < lo)
                        bad_list(list);
                } else {
                    hi = TO_END;
                }
            }
        }
        ranges = xrealloc(ranges, (nranges + 1) * sizeof *ranges);
        ranges[nranges].lo = lo;
        ranges[nranges].hi = hi;
        nranges++;
        if (!*s)
            break;
        if (*s != ',' && *s != ' ' && *s != '\t')
            bad_list(list);
        s++;
    }

    merged = xrealloc(merged, nranges * sizeof *merged);
    memcpy(merged, ranges, nranges * sizeof *merged);
    qsort(merged, nranges, sizeof *merged, cmp_range);
    nmerged = 0;
    for (size_t i = 0; i < nranges; i++) {
        if (nmerged && merged[i].lo <= merged[nmerged - 1].hi
                    + (merged[nmerged - 1].hi < TO_END))
        {
            if (merged[i].hi > merged[nmerged - 1].hi)
                merged[nmerged - 1].hi = merged[i].hi;
        } else {
            merged[nmerged++] = merged[i];
        }
    }
}

/* positions are asked in increasing order within a line: a cursor */
static size_t cur;
static void sel_reset(void) { cur = 0; }
static bool sel(long pos)
{
    while (cur < nmerged && merged[cur].hi < pos)
        cur++;
    return cur < nmerged && merged[cur].lo <= pos;
}

static void out(const void *p, size_t n)
{
    if (n && fwrite(p, 1, n, stdout) != n) {
        perror("cut: write error");
        exit(1);
    }
}

/* length of the character at p (n bytes left): 1 for an invalid byte */
static size_t charlen(const char *p, size_t n)
{
    if (!mb)
        return 1;
    mbstate_t st;
    memset(&st, 0, sizeof st);
    size_t k = mbrtowc(NULL, p, n, &st);
    return k == (size_t)-1 || k == (size_t)-2 || k == 0 ? 1 : k;
}

/* -b -n: the byte positions of one line, adjusted element by element */
static void bytes_n(const char *line, size_t len)
{
    static bool *keep;
    static size_t keepcap;
    static size_t *cstart;      /* the start of the character holding each byte */
    static size_t cscap;
    if (len == 0)
        return;
    if (len > keepcap) {
        keep = xrealloc(keep, len);
        keepcap = len;
    }
    if (len > cscap) {
        cstart = xrealloc(cstart, len * sizeof *cstart);
        cscap = len;
    }
    memset(keep, 0, len);
    for (size_t i = 0; i < len; ) {
        size_t k = charlen(line + i, len - i);
        for (size_t j = 0; j < k; j++)
            cstart[i + j] = i;
        i += k;
    }
    for (size_t r = 0; r < nranges; r++) {
        /* 0-based, inclusive */
        size_t lo = (size_t)ranges[r].lo - 1, hi;
        if (lo >= len)
            continue;
        lo = cstart[lo];
        if (ranges[r].hi == TO_END || (size_t)ranges[r].hi >= len) {
            hi = len - 1;
        } else {
            hi = (size_t)ranges[r].hi - 1;
            bool last = hi + 1 == len || cstart[hi + 1] == hi + 1;
            if (!last) {
                if (cstart[hi] == 0)
                    continue;               /* high becomes zero */
                hi = cstart[hi] - 1;
            }
        }
        if (lo > hi)
            continue;
        memset(keep + lo, 1, hi - lo + 1);
    }
    for (size_t i = 0; i < len; ) {
        size_t j = i;
        while (j < len && keep[j] == keep[i])
            j++;
        if (keep[i])
            out(line + i, j - i);
        i = j;
    }
}

/* is there a delimiter character at line[i]?  (k: the character's length) */
static bool is_delim(const char *line, size_t i, size_t k)
{
    return k == delim_len && memcmp(line + i, delim, k) == 0;
}

static void do_line(const char *line, size_t len)
{
    sel_reset();
    switch (mode) {
    case MODE_B:
        if (opt_n && mb) {
            bytes_n(line, len);
        } else {
            for (size_t i = 0; i < len; ) {
                size_t j = i;
                bool s = sel((long)i + 1);
                while (j < len && sel((long)j + 1) == s)
                    j++;
                if (s)
                    out(line + i, j - i);
                i = j;
            }
        }
        break;
    case MODE_C: {
        long pos = 1;
        for (size_t i = 0; i < len; pos++) {
            size_t k = charlen(line + i, len - i);
            if (sel(pos))
                out(line + i, k);
            i += k;
        }
        break;
    }
    case MODE_F: {
        bool found = false;
        for (size_t i = 0; i < len; ) {
            size_t k = charlen(line + i, len - i);
            if (is_delim(line, i, k)) {
                found = true;
                break;
            }
            i += k;
        }
        if (!found) {
            if (opt_s)
                return;
            out(line, len);
            break;
        }
        long field = 1;
        size_t start = 0;
        bool any = false;
        for (size_t i = 0; ; ) {
            size_t k = i < len ? charlen(line + i, len - i) : 0;
            if (i >= len || is_delim(line, i, k)) {
                if (sel(field)) {
                    if (any)
                        out(delim, delim_len);
                    out(line + start, i - start);
                    any = true;
                }
                if (i >= len)
                    break;
                field++;
                start = i + k;
            }
            i += k;
        }
        break;
    }
    default:
        break;
    }
    out("\n", 1);
}

static void do_file(FILE *fp, const char *name)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, fp)) >= 0) {
        size_t len = (size_t)n;
        if (len && line[len - 1] == '\n')
            len--;
        do_line(line, len);
    }
    if (ferror(fp)) {
        fprintf(stderr, "cut: %s: %s\n", name, strerror(errno));
        status = 1;
    }
    free(line);
}

static void set_mode(enum mode m)
{
    if (mode != MODE_NONE && mode != m) {
        fputs("cut: only one of -b, -c and -f may be given\n", stderr);
        usage();
    }
    mode = m;
}

int main(int argc, char *argv[])
{
    const char *list = NULL;
    int opt;

    setlocale(LC_ALL, "");
    mb = MB_CUR_MAX > 1;

    while ((opt = getopt(argc, argv, "b:c:d:f:ns")) != -1) {
        switch (opt) {
        case 'b': set_mode(MODE_B); list = optarg; break;
        case 'c': set_mode(MODE_C); list = optarg; break;
        case 'f': set_mode(MODE_F); list = optarg; break;
        case 'd': {
            size_t n = strlen(optarg);
            int k = n ? mblen(optarg, n) : -1;
            if (k <= 0 || (size_t)k != n) {
                fprintf(stderr, "cut: the delimiter must be a single character: '%s'\n", optarg);
                return 1;
            }
            memcpy(delim, optarg, n + 1);
            delim_len = n;
            break;
        }
        case 'n': opt_n = true; break;
        case 's': opt_s = true; break;
        default:
            usage();
        }
    }
    if (mode == MODE_NONE) {
        fputs("cut: one of -b, -c and -f is required\n", stderr);
        usage();
    }
    parse_list(list);

    if (optind >= argc) {
        do_file(stdin, "standard input");
    } else {
        for (int i = optind; i < argc; i++) {
            if (strcmp(argv[i], "-") == 0) {
                do_file(stdin, "standard input");
                clearerr(stdin);
                continue;
            }
            FILE *fp = fopen(argv[i], "r");
            if (!fp) {
                fprintf(stderr, "cut: %s: %s\n", argv[i], strerror(errno));
                status = 1;
                continue;
            }
            do_file(fp, argv[i]);
            fclose(fp);
        }
    }
    if (fflush(stdout) == EOF || ferror(stdout)) {
        perror("cut: write error");
        return 1;
    }
    return status;
}
