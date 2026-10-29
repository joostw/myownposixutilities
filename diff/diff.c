/*
 * diff.c - diff: compare two files (POSIX.1-2024, XCU diff)
 *
 * Synopsis: diff [-c|-e|-f|-u|-C n|-U n] [-br] file1 file2
 *
 * The lines of both files are first numbered by their equivalence class
 * (with -b, after white space is normalized); the two sequences of numbers
 * are compared with Myers' O(ND) algorithm in linear space (the version
 * of "An O(ND) Difference Algorithm and Its Variations", 1986, with the
 * forward and backward searches meeting in the middle).  The result is a
 * minimal edit script, except for very large inputs with very many
 * differences, where, as in GNU diff, the search for a middle snake is
 * cut off after about the square root of the number of diagonals (at
 * least 4096) and the best partial path is taken.
 *
 * Choices where the standard leaves room:
 *  - A file with a NUL byte is not a text file: if the two files differ,
 *    "Files F1 and F2 differ" is written (status 1).
 *  - A last line without a newline differs from the same line with one
 *    (with -b it does not: the missing newline counts as white space);
 *    in the default, -c and -u formats it is followed by the line
 *    "\ No newline at end of file".  -e and -f write it with a newline.
 *  - -e: a text line that is a single "." is written as "..", the text
 *    ended, and the line put right with "s/.//" (then "a" to go on).
 *  - -c: a file's half of a hunk lists its lines only if some of them are
 *    deleted or changed (or, for file2, added); the names and times are
 *    separated by a space.  Standard input is named "-" in -c and -u, its
 *    time is the current time.
 *  - -u: the fraction of the seconds is written with nine digits; an
 *    empty unaffected line is written as a single space; a range of one
 *    line is written as just its line number.
 *  - Directories: subdirectories common to both are reported (without -r)
 *    as "Common subdirectories: D1 and D2"; files of different types, or of
 *    types that are not compared, as "File F1 is a TYPE while file F2 is a
 *    TYPE".  Symbolic links are followed.  The entries are compared in the
 *    order of strcoll().  A directory that is an ancestor of itself (a
 *    loop through symbolic links) is reported and not entered.
 *  - When the output format options are given more than once, the last
 *    one is used; -C 0 is an error (n must be positive), -U 0 is not.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char format = 'n';       /* n (default) c e f u */
static long context = 3;
static bool opt_b, opt_r;
static char *optstring_text;    /* the options as given, for "diff OPTS F1 F2" */
static int status;              /* 0 same, 1 different, 2 trouble */

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "diff: out of memory\n");
        exit(2);
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    if (!(p = realloc(p, n ? n : 1))) {
        fprintf(stderr, "diff: out of memory\n");
        exit(2);
    }
    return p;
}

static void trouble(void)
{
    status = 2;
}

/* ---- output ---- */

static void out(const char *s, size_t n)
{
    if (n && fwrite(s, 1, n, stdout) != n)
        trouble();
}

static void outs(const char *s)
{
    out(s, strlen(s));
}

/* ---- the files ---- */

struct file {
    const char *name;           /* as given, for messages and headers */
    char *buf;
    size_t len;
    size_t *line;               /* start of line i; line[nlines] = len */
    size_t nlines;
    bool incomplete;            /* the last line has no newline */
    bool binary;
    struct stat st;
    bool is_stdin;
    int *id;                    /* equivalence class of each line */
};

static bool read_file(struct file *f, const char *name)
{
    memset(f, 0, sizeof *f);
    f->name = name;
    int fd;
    if (strcmp(name, "-") == 0) {
        fd = 0;
        f->is_stdin = true;
    } else if ((fd = open(name, O_RDONLY)) < 0) {
        fprintf(stderr, "diff: %s: %s\n", name, strerror(errno));
        return false;
    }
    if (fstat(fd, &f->st) != 0) {
        fprintf(stderr, "diff: %s: %s\n", name, strerror(errno));
        if (fd)
            close(fd);
        return false;
    }
    size_t cap = S_ISREG(f->st.st_mode) && f->st.st_size > 0 ? (size_t)f->st.st_size + 1 : 65536;
    f->buf = xmalloc(cap);
    for (;;) {
        if (f->len == cap) {
            cap *= 2;
            f->buf = xrealloc(f->buf, cap);
        }
        ssize_t n = read(fd, f->buf + f->len, cap - f->len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            fprintf(stderr, "diff: %s: %s\n", name, strerror(errno));
            if (fd)
                close(fd);
            return false;
        }
        if (n == 0)
            break;
        f->len += (size_t)n;
    }
    if (fd)
        close(fd);
    f->binary = memchr(f->buf, '\0', f->len) != NULL;
    size_t nl = 0;
    for (size_t i = 0; i < f->len; i++)
        if (f->buf[i] == '\n')
            nl++;
    f->incomplete = f->len > 0 && f->buf[f->len - 1] != '\n';
    f->nlines = nl + (f->incomplete ? 1 : 0);
    f->line = xmalloc((f->nlines + 1) * sizeof *f->line);
    size_t k = 0;
    f->line[k++] = 0;
    for (size_t i = 0; i < f->len; i++)
        if (f->buf[i] == '\n' && i + 1 < f->len)
            f->line[k++] = i + 1;
    f->line[f->nlines] = f->len;
    return true;
}

static void free_file(struct file *f)
{
    free(f->buf);
    free(f->line);
    free(f->id);
}

/* line i of f, without its newline; *nl tells whether it has one */
static const char *line_text(const struct file *f, size_t i, size_t *n, bool *nl)
{
    size_t s = f->line[i], e = f->line[i + 1];
    *nl = e > s && f->buf[e - 1] == '\n';
    *n = e - s - (*nl ? 1 : 0);
    return f->buf + s;
}

/* ---- equivalence classes ---- */

/* the key of a line: with -b, runs of white space are one space and white
   space at the end is gone; the newline is part of the key */
static size_t line_key(const struct file *f, size_t i, char **key, size_t *cap)
{
    size_t n;
    bool nl;
    const char *s = line_text(f, i, &n, &nl);
    if (*cap < n + 2) {
        *cap = (n + 2) * 2;
        *key = xrealloc(*key, *cap);
    }
    size_t k = 0;
    if (!opt_b) {
        memcpy(*key, s, n);
        k = n;
    } else {
        size_t j = 0;
        while (j < n) {
            if (isspace((unsigned char)s[j])) {
                while (j < n && isspace((unsigned char)s[j]))
                    j++;
                if (j < n)
                    (*key)[k++] = ' ';
            } else
                (*key)[k++] = s[j++];
        }
    }
    /* with -b, a missing newline is white space at the end of the line too */
    (*key)[k++] = nl || opt_b ? '\n' : '\0';
    return k;
}

struct cls {
    uint64_t hash;
    const struct file *f;
    size_t line;
    int next;
};

static void classify(struct file *a, struct file *b)
{
    size_t total = a->nlines + b->nlines;
    size_t nb = 1;
    while (nb < total * 2)
        nb <<= 1;
    int *bucket = xmalloc(nb * sizeof *bucket);
    for (size_t i = 0; i < nb; i++)
        bucket[i] = -1;
    struct cls *cl = xmalloc((total + 1) * sizeof *cl);
    int ncl = 0;
    char *key = NULL, *key2 = NULL;
    size_t cap = 0, cap2 = 0;
    struct file *fs[2] = { a, b };
    for (int w = 0; w < 2; w++) {
        struct file *f = fs[w];
        f->id = xmalloc((f->nlines + 1) * sizeof *f->id);
        for (size_t i = 0; i < f->nlines; i++) {
            size_t n = line_key(f, i, &key, &cap);
            uint64_t h = 1469598103934665603ull;
            for (size_t j = 0; j < n; j++)
                h = (h ^ (unsigned char)key[j]) * 1099511628211ull;
            size_t bi = (size_t)(h & (nb - 1));
            int c;
            for (c = bucket[bi]; c >= 0; c = cl[c].next) {
                if (cl[c].hash != h)
                    continue;
                size_t n2 = line_key(cl[c].f, cl[c].line, &key2, &cap2);
                if (n2 == n && memcmp(key, key2, n) == 0)
                    break;
            }
            if (c < 0) {
                c = ncl++;
                cl[c] = (struct cls){ h, f, i, bucket[bi] };
                bucket[bi] = c;
            }
            f->id[i] = c;
        }
    }
    free(key);
    free(key2);
    free(cl);
    free(bucket);
}

/* ---- Myers' algorithm ---- */

static const int *X, *Y;
static char *xchg, *ychg;       /* deleted / inserted lines */
static long *fd, *bd;           /* the furthest points on each diagonal */
static long too_expensive;

struct part {
    long xmid, ymid;
};

static void diag(long xoff, long xlim, long yoff, long ylim, struct part *p)
{
    const long dmin = xoff - ylim, dmax = xlim - yoff;
    const long fmid = xoff - yoff, bmid = xlim - ylim;
    long fmin = fmid, fmax = fmid, bmin = bmid, bmax = bmid;
    const bool odd = (fmid - bmid) & 1;
    fd[fmid] = xoff;
    bd[bmid] = xlim;
    for (long c = 1;; c++) {
        long d;
        if (fmin > dmin)
            fd[--fmin - 1] = -1;
        else
            ++fmin;
        if (fmax < dmax)
            fd[++fmax + 1] = -1;
        else
            --fmax;
        for (d = fmax; d >= fmin; d -= 2) {
            long tlo = fd[d - 1], thi = fd[d + 1];
            long x = tlo >= thi ? tlo + 1 : thi;
            long y = x - d;
            while (x < xlim && y < ylim && X[x] == Y[y])
                x++, y++;
            fd[d] = x;
            if (odd && bmin <= d && d <= bmax && bd[d] <= x) {
                p->xmid = x;
                p->ymid = y;
                return;
            }
        }
        if (bmin > dmin)
            bd[--bmin - 1] = LONG_MAX;
        else
            ++bmin;
        if (bmax < dmax)
            bd[++bmax + 1] = LONG_MAX;
        else
            --bmax;
        for (d = bmax; d >= bmin; d -= 2) {
            long tlo = bd[d - 1], thi = bd[d + 1];
            long x = tlo < thi ? tlo : thi - 1;
            long y = x - d;
            while (x > xoff && y > yoff && X[x - 1] == Y[y - 1])
                x--, y--;
            bd[d] = x;
            if (!odd && fmin <= d && d <= fmax && x <= fd[d]) {
                p->xmid = x;
                p->ymid = y;
                return;
            }
        }
        if (c >= too_expensive) {
            /* take the point that has gone furthest, forward or backward */
            long fxybest = -1, fxbest = 0, bxybest = LONG_MAX, bxbest = 0;
            for (d = fmax; d >= fmin; d -= 2) {
                long x = fd[d] < xlim ? fd[d] : xlim, y = x - d;
                if (ylim < y)
                    x = ylim + d, y = ylim;
                if (fxybest < x + y)
                    fxybest = x + y, fxbest = x;
            }
            for (d = bmax; d >= bmin; d -= 2) {
                long x = bd[d] > xoff ? bd[d] : xoff, y = x - d;
                if (y < yoff)
                    x = yoff + d, y = yoff;
                if (x + y < bxybest)
                    bxybest = x + y, bxbest = x;
            }
            if ((xlim + ylim) - bxybest < fxybest - (xoff + yoff)) {
                p->xmid = fxbest;
                p->ymid = fxybest - fxbest;
            } else {
                p->xmid = bxbest;
                p->ymid = bxybest - bxbest;
            }
            return;
        }
    }
}

static void compareseq(long xoff, long xlim, long yoff, long ylim)
{
    for (;;) {
        while (xoff < xlim && yoff < ylim && X[xoff] == Y[yoff])
            xoff++, yoff++;
        while (xlim > xoff && ylim > yoff && X[xlim - 1] == Y[ylim - 1])
            xlim--, ylim--;
        if (xoff == xlim) {
            while (yoff < ylim)
                ychg[yoff++] = 1;
            return;
        }
        if (yoff == ylim) {
            while (xoff < xlim)
                xchg[xoff++] = 1;
            return;
        }
        struct part p;
        diag(xoff, xlim, yoff, ylim, &p);
        /* the smaller half by recursion, the other by the loop */
        if ((p.xmid - xoff) + (p.ymid - yoff) < (xlim - p.xmid) + (ylim - p.ymid)) {
            compareseq(xoff, p.xmid, yoff, p.ymid);
            xoff = p.xmid;
            yoff = p.ymid;
        } else {
            compareseq(p.xmid, xlim, p.ymid, ylim);
            xlim = p.xmid;
            ylim = p.ymid;
        }
    }
}

/* ---- the changes ---- */

struct change {
    size_t a, na;               /* lines a .. a+na-1 of file1 deleted */
    size_t b, nb;               /* lines b .. b+nb-1 of file2 inserted */
};

static struct change *changes;
static size_t nchanges;

static void find_changes(struct file *a, struct file *b)
{
    size_t m = a->nlines, n = b->nlines;
    xchg = calloc(m + 1, 1);
    ychg = calloc(n + 1, 1);
    long *buf = xmalloc((m + n + 3) * 2 * sizeof *buf);
    fd = buf + n + 1;
    bd = buf + (m + n + 3) + n + 1;
    if (!xchg || !ychg) {
        fprintf(stderr, "diff: out of memory\n");
        exit(2);
    }
    too_expensive = 1;
    for (size_t diags = m + n + 3; diags != 0; diags >>= 2)
        too_expensive <<= 1;
    if (too_expensive < 4096)
        too_expensive = 4096;
    X = a->id;
    Y = b->id;
    compareseq(0, (long)m, 0, (long)n);
    free(buf);
    nchanges = 0;
    size_t cap = 0;
    size_t i = 0, j = 0;
    while (i < m || j < n) {
        if (i < m && j < n && !xchg[i] && !ychg[j]) {
            i++, j++;
            continue;
        }
        struct change c = { i, 0, j, 0 };
        while (i < m && xchg[i])
            i++, c.na++;
        while (j < n && ychg[j])
            j++, c.nb++;
        if (c.na == 0 && c.nb == 0) {   /* cannot happen; keep going */
            i++, j++;
            continue;
        }
        if (nchanges == cap) {
            cap = cap ? cap * 2 : 64;
            changes = xrealloc(changes, cap * sizeof *changes);
        }
        changes[nchanges++] = c;
    }
    free(xchg);
    free(ychg);
}

/* ---- the formats ---- */

static void put_line(const struct file *f, size_t i, const char *prefix, bool mark_incomplete)
{
    size_t n;
    bool nl;
    const char *s = line_text(f, i, &n, &nl);
    outs(prefix);
    out(s, n);
    outs("\n");
    if (!nl && mark_incomplete)
        outs("\\ No newline at end of file\n");
}

/* "a,b" or "a" (1-based first and last) */
static void range(char *buf, size_t first, size_t last, char sep)
{
    if (first == last)
        sprintf(buf, "%zu", first);
    else if (sep == ' ')
        sprintf(buf, "%zu %zu", first, last);
    else
        sprintf(buf, "%zu,%zu", first, last);
}

static void normal_format(const struct file *a, const struct file *b)
{
    char r1[64], r2[64], line[160];
    for (size_t k = 0; k < nchanges; k++) {
        const struct change *c = &changes[k];
        if (c->na == 0) {
            range(r2, c->b + 1, c->b + c->nb, ',');
            snprintf(line, sizeof line, "%zua%s\n", c->a, r2);
        } else if (c->nb == 0) {
            range(r1, c->a + 1, c->a + c->na, ',');
            snprintf(line, sizeof line, "%sd%zu\n", r1, c->b);
        } else {
            range(r1, c->a + 1, c->a + c->na, ',');
            range(r2, c->b + 1, c->b + c->nb, ',');
            snprintf(line, sizeof line, "%sc%s\n", r1, r2);
        }
        outs(line);
        for (size_t i = 0; i < c->na; i++)
            put_line(a, c->a + i, "< ", true);
        if (c->na && c->nb)
            outs("---\n");
        for (size_t i = 0; i < c->nb; i++)
            put_line(b, c->b + i, "> ", true);
    }
}

/* -e text: lines of b, with the handling of a line that is "." */
static void ed_text(const struct file *b, size_t first, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        size_t len;
        bool nl;
        const char *s = line_text(b, first + i, &len, &nl);
        if (len == 1 && s[0] == '.') {
            outs("..\n.\ns/.//\n");
            if (i + 1 < n)
                outs("a\n");
            else
                return;         /* the text is already ended */
            continue;
        }
        out(s, len);
        outs("\n");
    }
    outs(".\n");
}

static void ed_format(const struct file *b, bool reverse)
{
    char r1[64], line[160];
    for (size_t kk = 0; kk < nchanges; kk++) {
        const struct change *c = &changes[reverse ? nchanges - 1 - kk : kk];
        char sep = reverse ? ',' : ' ';
        if (c->na == 0) {
            if (reverse)
                snprintf(line, sizeof line, "%zua\n", c->a);
            else
                snprintf(line, sizeof line, "a%zu\n", c->a);
        } else {
            range(r1, c->a + 1, c->a + c->na, sep);
            char cmd = c->nb ? 'c' : 'd';
            if (reverse)
                snprintf(line, sizeof line, "%s%c\n", r1, cmd);
            else
                snprintf(line, sizeof line, "%c%s\n", cmd, r1);
        }
        outs(line);
        if (c->nb) {
            if (reverse)
                ed_text(b, c->b, c->nb);
            else {
                for (size_t i = 0; i < c->nb; i++) {
                    size_t len;
                    bool nl;
                    const char *s = line_text(b, c->b + i, &len, &nl);
                    out(s, len);
                    outs("\n");
                }
                outs(".\n");
            }
        }
    }
}

static void timestamp(const struct file *f, char *buf, size_t n, bool unified)
{
    struct timespec ts;
    if (f->is_stdin)
        clock_gettime(CLOCK_REALTIME, &ts);
    else
        ts = f->st.st_mtim;
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    if (!unified) {
        strftime(buf, n, "%a %b %e %T %Y", &tm);
        return;
    }
    char date[64], zone[16];
    strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", &tm);
    strftime(zone, sizeof zone, "%z", &tm);
    snprintf(buf, n, "%s.%09ld %s", date, (long)ts.tv_nsec, zone);
}

/* the hunks: runs of changes no more than 2 * context unchanged lines apart */
static size_t hunk_end(size_t k)
{
    size_t e = k + 1;
    while (e < nchanges) {
        const struct change *p = &changes[e - 1], *c = &changes[e];
        size_t gap = c->a - (p->a + p->na);
        if (gap > 2 * (size_t)context)
            break;
        e++;
    }
    return e;
}

static void context_format(const struct file *a, const struct file *b)
{
    char t1[128], t2[128], line[200];
    timestamp(a, t1, sizeof t1, false);
    timestamp(b, t2, sizeof t2, false);
    outs("*** ");
    outs(a->name);
    outs(" ");
    outs(t1);
    outs("\n--- ");
    outs(b->name);
    outs(" ");
    outs(t2);
    outs("\n");
    size_t ctx = (size_t)context;
    for (size_t k = 0; k < nchanges;) {
        size_t e = hunk_end(k);
        const struct change *first = &changes[k], *last = &changes[e - 1];
        size_t a0 = first->a > ctx ? first->a - ctx : 0;
        size_t a1 = last->a + last->na + ctx;
        if (a1 > a->nlines)
            a1 = a->nlines;
        size_t b0 = first->b > ctx ? first->b - ctx : 0;
        size_t b1 = last->b + last->nb + ctx;
        if (b1 > b->nlines)
            b1 = b->nlines;
        outs("***************\n");
        if (a1 - a0 >= 2)
            snprintf(line, sizeof line, "*** %zu,%zu ****\n", a0 + 1, a1);
        else
            snprintf(line, sizeof line, "*** %zu ****\n", a1);
        outs(line);
        bool any = false;
        for (size_t i = k; i < e; i++)
            if (changes[i].na)
                any = true;
        if (any) {
            size_t i = a0;
            for (size_t c = k; c < e; c++) {
                for (; i < changes[c].a; i++)
                    put_line(a, i, "  ", true);
                const char *mark = changes[c].nb ? "! " : "- ";
                for (; i < changes[c].a + changes[c].na; i++)
                    put_line(a, i, mark, true);
            }
            for (; i < a1; i++)
                put_line(a, i, "  ", true);
        }
        if (b1 - b0 >= 2)
            snprintf(line, sizeof line, "--- %zu,%zu ----\n", b0 + 1, b1);
        else
            snprintf(line, sizeof line, "--- %zu ----\n", b1);
        outs(line);
        any = false;
        for (size_t i = k; i < e; i++)
            if (changes[i].nb)
                any = true;
        if (any) {
            size_t j = b0;
            for (size_t c = k; c < e; c++) {
                for (; j < changes[c].b; j++)
                    put_line(b, j, "  ", true);
                const char *mark = changes[c].na ? "! " : "+ ";
                for (; j < changes[c].b + changes[c].nb; j++)
                    put_line(b, j, mark, true);
            }
            for (; j < b1; j++)
                put_line(b, j, "  ", true);
        }
        k = e;
    }
}

static void urange(char *buf, size_t first0, size_t count)
{
    /* first0: the 0-based first line */
    if (count == 0)
        sprintf(buf, "%zu,0", first0);
    else if (count == 1)
        sprintf(buf, "%zu", first0 + 1);
    else
        sprintf(buf, "%zu,%zu", first0 + 1, count);
}

static void unified_format(const struct file *a, const struct file *b)
{
    char t1[128], t2[128], r1[64], r2[64], line[200];
    timestamp(a, t1, sizeof t1, true);
    timestamp(b, t2, sizeof t2, true);
    outs("--- ");
    outs(a->is_stdin ? "-" : a->name);
    outs("\t");
    outs(t1);
    outs("\n+++ ");
    outs(b->is_stdin ? "-" : b->name);
    outs("\t");
    outs(t2);
    outs("\n");
    size_t ctx = (size_t)context;
    for (size_t k = 0; k < nchanges;) {
        size_t e = hunk_end(k);
        const struct change *first = &changes[k], *last = &changes[e - 1];
        size_t a0 = first->a > ctx ? first->a - ctx : 0;
        size_t a1 = last->a + last->na + ctx;
        if (a1 > a->nlines)
            a1 = a->nlines;
        size_t b0 = first->b > ctx ? first->b - ctx : 0;
        size_t b1 = last->b + last->nb + ctx;
        if (b1 > b->nlines)
            b1 = b->nlines;
        urange(r1, a0, a1 - a0);
        urange(r2, b0, b1 - b0);
        snprintf(line, sizeof line, "@@ -%s +%s @@\n", r1, r2);
        outs(line);
        size_t i = a0;
        for (size_t c = k; c < e; c++) {
            for (; i < changes[c].a; i++)
                put_line(a, i, " ", true);
            for (; i < changes[c].a + changes[c].na; i++)
                put_line(a, i, "-", true);
            for (size_t j = changes[c].b; j < changes[c].b + changes[c].nb; j++)
                put_line(b, j, "+", true);
        }
        for (; i < a1; i++)
            put_line(a, i, " ", true);
        k = e;
    }
}

/* ---- comparing two files ---- */

/* compare two named files (not directories); header: the line to write
   first if they differ (directories), or NULL */
static void diff_files(const char *n1, const char *n2, const char *header)
{
    struct file a, b;
    if (!read_file(&a, n1)) {
        trouble();
        return;
    }
    if (!read_file(&b, n2)) {
        free_file(&a);
        trouble();
        return;
    }
    bool same_file = !a.is_stdin && !b.is_stdin && a.st.st_dev == b.st.st_dev &&
                     a.st.st_ino == b.st.st_ino;
    if (same_file || (a.len == b.len && memcmp(a.buf, b.buf, a.len) == 0)) {
        free_file(&a);
        free_file(&b);
        return;
    }
    if (a.binary || b.binary) {
        outs("Files ");
        outs(n1);
        outs(" and ");
        outs(n2);
        outs(" differ\n");
        if (status < 1)
            status = 1;
        free_file(&a);
        free_file(&b);
        return;
    }
    classify(&a, &b);
    find_changes(&a, &b);
    if (nchanges > 0) {
        if (header)
            outs(header);
        if (status < 1)
            status = 1;
        switch (format) {
        case 'n': normal_format(&a, &b); break;
        case 'e': ed_format(&b, true); break;
        case 'f': ed_format(&b, false); break;
        case 'c': context_format(&a, &b); break;
        case 'u': unified_format(&a, &b); break;
        }
    }
    free(changes);
    changes = NULL;
    nchanges = 0;
    free_file(&a);
    free_file(&b);
}

/* ---- directories ---- */

static char *join(const char *dir, const char *name)
{
    size_t dl = strlen(dir), nl = strlen(name);
    char *p = xmalloc(dl + nl + 2);
    memcpy(p, dir, dl);
    size_t k = dl;
    if (dl == 0 || dir[dl - 1] != '/')
        p[k++] = '/';
    memcpy(p + k, name, nl + 1);
    return p;
}

static int cmp_names(const void *x, const void *y)
{
    return strcoll(*(char *const *)x, *(char *const *)y);
}

static char **list_dir(const char *dir, size_t *count)
{
    DIR *d = opendir(dir);
    *count = 0;
    if (!d) {
        fprintf(stderr, "diff: %s: %s\n", dir, strerror(errno));
        trouble();
        return NULL;
    }
    char **v = NULL;
    size_t n = 0, cap = 0;
    struct dirent *e;
    errno = 0;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            v = xrealloc(v, cap * sizeof *v);
        }
        size_t l = strlen(e->d_name) + 1;
        v[n++] = memcpy(xmalloc(l), e->d_name, l);
    }
    if (errno) {
        fprintf(stderr, "diff: %s: %s\n", dir, strerror(errno));
        trouble();
    }
    closedir(d);
    qsort(v, n, sizeof *v, cmp_names);
    *count = n;
    return v;
}

static const char *type_name(mode_t m)
{
    if (S_ISREG(m)) return "regular file";
    if (S_ISDIR(m)) return "directory";
    if (S_ISFIFO(m)) return "fifo";
    if (S_ISCHR(m)) return "character special file";
    if (S_ISBLK(m)) return "block special file";
    if (S_ISSOCK(m)) return "socket";
    return "file of another type";
}

/* the directories being compared, for loops */
struct id {
    dev_t dev;
    ino_t ino;
};
static struct id *anc1, *anc2;
static size_t nanc, capanc;

static bool on_stack(const struct id *v, const struct stat *st)
{
    for (size_t i = 0; i < nanc; i++)
        if (v[i].dev == st->st_dev && v[i].ino == st->st_ino)
            return true;
    return false;
}

static void diff_dirs(const char *d1, const char *d2, const struct stat *s1, const struct stat *s2);

/* two entries of the same name in the directories being compared */
static void diff_entry(const char *p1, const char *p2)
{
    struct stat s1, s2;
    if (stat(p1, &s1) != 0) {
        fprintf(stderr, "diff: %s: %s\n", p1, strerror(errno));
        trouble();
        return;
    }
    if (stat(p2, &s2) != 0) {
        fprintf(stderr, "diff: %s: %s\n", p2, strerror(errno));
        trouble();
        return;
    }
    if (S_ISDIR(s1.st_mode) && S_ISDIR(s2.st_mode)) {
        if (!opt_r) {
            outs("Common subdirectories: ");
            outs(p1);
            outs(" and ");
            outs(p2);
            outs("\n");
            return;
        }
        diff_dirs(p1, p2, &s1, &s2);
        return;
    }
    if (S_ISREG(s1.st_mode) && S_ISREG(s2.st_mode)) {
        size_t n = strlen(optstring_text) + strlen(p1) + strlen(p2) + 16;
        char *header = xmalloc(n);
        snprintf(header, n, "diff%s%s %s %s\n", *optstring_text ? " " : "", optstring_text, p1, p2);
        diff_files(p1, p2, header);
        free(header);
        return;
    }
    /* not compared */
    outs("File ");
    outs(p1);
    outs(" is a ");
    outs(type_name(s1.st_mode));
    outs(" while file ");
    outs(p2);
    outs(" is a ");
    outs(type_name(s2.st_mode));
    outs("\n");
    if (!(s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino) && status < 1)
        status = 1;
}

static void diff_dirs(const char *d1, const char *d2, const struct stat *s1, const struct stat *s2)
{
    if (on_stack(anc1, s1) || on_stack(anc2, s2)) {
        fprintf(stderr, "diff: %s and %s: a directory loop (not entered)\n", d1, d2);
        trouble();
        return;
    }
    if (nanc == capanc) {
        capanc = capanc ? capanc * 2 : 16;
        anc1 = xrealloc(anc1, capanc * sizeof *anc1);
        anc2 = xrealloc(anc2, capanc * sizeof *anc2);
    }
    anc1[nanc] = (struct id){ s1->st_dev, s1->st_ino };
    anc2[nanc] = (struct id){ s2->st_dev, s2->st_ino };
    nanc++;
    size_t n1, n2;
    char **v1 = list_dir(d1, &n1), **v2 = list_dir(d2, &n2);
    size_t i = 0, j = 0;
    while (i < n1 || j < n2) {
        int c = i == n1 ? 1 : j == n2 ? -1 : strcoll(v1[i], v2[j]);
        if (c == 0 && strcmp(v1[i], v2[j]) != 0)
            c = strcmp(v1[i], v2[j]);
        if (c < 0) {
            outs("Only in ");
            outs(d1);
            outs(": ");
            outs(v1[i]);
            outs("\n");
            if (status < 1)
                status = 1;
            free(v1[i++]);
        } else if (c > 0) {
            outs("Only in ");
            outs(d2);
            outs(": ");
            outs(v2[j]);
            outs("\n");
            if (status < 1)
                status = 1;
            free(v2[j++]);
        } else {
            char *p1 = join(d1, v1[i]), *p2 = join(d2, v2[j]);
            diff_entry(p1, p2);
            free(p1);
            free(p2);
            free(v1[i++]);
            free(v2[j++]);
        }
    }
    free(v1);
    free(v2);
    nanc--;
}

/* ---- main ---- */

static long number(const char *s, bool positive)
{
    char *end;
    if (!isdigit((unsigned char)*s))
        goto bad;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (*end || errno || (positive && v == 0))
        goto bad;
    return v;
bad:
    fprintf(stderr, "diff: %s: not a valid number of lines of context\n", s);
    exit(2);
}

static void usage(void)
{
    fprintf(stderr, "usage: diff [-c|-e|-f|-u|-C n|-U n] [-br] file1 file2\n");
    exit(2);
}

static char *last_component(const char *s)
{
    size_t n = strlen(s);
    while (n > 1 && s[n - 1] == '/')
        n--;
    size_t i = n;
    while (i > 0 && s[i - 1] != '/')
        i--;
    char *r = xmalloc(n - i + 1);
    memcpy(r, s + i, n - i);
    r[n - i] = '\0';
    return r;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "bcC:efruU:")) != -1) {
        switch (c) {
        case 'b': opt_b = true; break;
        case 'r': opt_r = true; break;
        case 'c': format = 'c'; context = 3; break;
        case 'C': format = 'c'; context = number(optarg, true); break;
        case 'e': format = 'e'; break;
        case 'f': format = 'f'; break;
        case 'u': format = 'u'; context = 3; break;
        case 'U': format = 'u'; context = number(optarg, false); break;
        default: usage();
        }
    }
    if (argc - optind != 2)
        usage();
    /* the options as they were given */
    size_t ol = 1;
    for (int i = 1; i < optind; i++)
        ol += strlen(argv[i]) + 1;
    optstring_text = xmalloc(ol);
    optstring_text[0] = '\0';
    for (int i = 1; i < optind; i++) {
        if (strcmp(argv[i], "--") == 0)
            continue;
        if (*optstring_text)
            strcat(optstring_text, " ");
        strcat(optstring_text, argv[i]);
    }
    const char *f1 = argv[optind], *f2 = argv[optind + 1];
    struct stat s1, s2;
    bool d1 = strcmp(f1, "-") != 0 && stat(f1, &s1) == 0 && S_ISDIR(s1.st_mode);
    bool d2 = strcmp(f2, "-") != 0 && stat(f2, &s2) == 0 && S_ISDIR(s2.st_mode);
    if (d1 && d2) {
        if (s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino)
            return 0;
        diff_dirs(f1, f2, &s1, &s2);
    } else if (d1 || d2) {
        const char *file = d1 ? f2 : f1;
        if (strcmp(file, "-") == 0) {
            fprintf(stderr, "diff: cannot compare standard input with a directory\n");
            return 2;
        }
        char *last = last_component(file);
        char *in = join(d1 ? f1 : f2, last);
        if (d1)
            diff_files(in, f2, NULL);
        else
            diff_files(f1, in, NULL);
        free(in);
        free(last);
    } else {
        diff_files(f1, f2, NULL);
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "diff: write error: %s\n", strerror(errno));
        return 2;
    }
    return status;
}
