/*
 * join.c - join: relational database operator (POSIX.1-2024, XCU join)
 *
 * Synopsis:
 *   join [-a file_number|-v file_number] [-e string] [-o list] [-t char]
 *        [-1 field] [-2 field] file1 file2
 *
 * Both files are read a group at a time: the lines with the same join
 * field.  Join fields are compared with strcoll() (as sort orders them,
 * with -b unless -t is given).  For each pair of equal groups all the
 * combinations are written, in the order of the lines.
 *
 * Choices where the standard leaves room:
 *  - Unpairable lines (-a, -v) are written like paired ones without the
 *    other file's fields: the join field, then the line's other fields;
 *    with -o, the other file's fields are empty.
 *  - -e is used only with -o, as the standard says: without -o, empty
 *    fields are written empty.
 *  - Several -o options are joined into one list.
 *  - -t takes one character, which may be a multibyte one.
 *  - A last line without a newline is taken as a line.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *tsep;              /* -t, as bytes; NULL: blanks */
static size_t tlen;
static size_t jf[3] = { 0, 1, 1 };  /* the join field of each file */
static bool opt_a[3], opt_v[3];
static const char *estr;
struct ofield {
    int file;                   /* 0: the join field */
    size_t field;
};
static struct ofield *olist;
static size_t nolist;
static bool have_o;

static void *xrealloc(void *p, size_t n)
{
    if (!(p = realloc(p, n ? n : 1))) {
        fprintf(stderr, "join: out of memory\n");
        exit(2);
    }
    return p;
}

/* ---- lines and fields ---- */

struct line {
    char *s;
    size_t len;
    const char **f;             /* the fields */
    size_t *fl;
    size_t nf;
};

static void split(struct line *l)
{
    size_t cap = 8;
    l->f = xrealloc(NULL, cap * sizeof *l->f);
    l->fl = xrealloc(NULL, cap * sizeof *l->fl);
    l->nf = 0;
    const char *p = l->s, *end = l->s + l->len;
    if (tsep) {
        for (;;) {
            const char *q = p;
            while (q < end && !((size_t)(end - q) >= tlen && memcmp(q, tsep, tlen) == 0))
                q++;
            if (l->nf == cap) {
                cap *= 2;
                l->f = xrealloc(l->f, cap * sizeof *l->f);
                l->fl = xrealloc(l->fl, cap * sizeof *l->fl);
            }
            l->f[l->nf] = p;
            l->fl[l->nf++] = (size_t)(q - p);
            if (q >= end)
                break;
            p = q + tlen;
        }
        return;
    }
    for (;;) {
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;
        if (p >= end)
            break;
        const char *q = p;
        while (q < end && *q != ' ' && *q != '\t')
            q++;
        if (l->nf == cap) {
            cap *= 2;
            l->f = xrealloc(l->f, cap * sizeof *l->f);
            l->fl = xrealloc(l->fl, cap * sizeof *l->fl);
        }
        l->f[l->nf] = p;
        l->fl[l->nf++] = (size_t)(q - p);
        p = q;
    }
}

static void free_line(struct line *l)
{
    free(l->s);
    free(l->f);
    free(l->fl);
}

struct in {
    const char *name;
    FILE *fp;
    int n;                      /* 1 or 2 */
    struct line next;           /* read ahead */
    bool have_next;
    struct line *group;
    size_t ng, capg;
};

static bool read_line(struct in *f, struct line *l)
{
    char *buf = NULL;
    size_t cap = 0;
    ssize_t n = getline(&buf, &cap, f->fp);
    if (n < 0) {
        free(buf);
        if (ferror(f->fp)) {
            fprintf(stderr, "join: %s: %s\n", f->name, strerror(errno));
            exit(2);
        }
        return false;
    }
    if (n > 0 && buf[n - 1] == '\n')
        buf[--n] = '\0';
    l->s = buf;
    l->len = (size_t)n;
    split(l);
    return true;
}

/* the join field of a line, NUL-terminated (a static buffer per file) */
static const char *key(const struct line *l, int file)
{
    static char *buf[3];
    static size_t cap[3];
    size_t k = jf[file] - 1;
    size_t n = k < l->nf ? l->fl[k] : 0;
    if (n + 1 > cap[file]) {
        cap[file] = n + 1;
        buf[file] = xrealloc(buf[file], cap[file]);
    }
    if (n)
        memcpy(buf[file], l->f[k], n);
    buf[file][n] = '\0';
    return buf[file];
}

static int keycmp(const struct line *a, int fa, const struct line *b, int fb)
{
    char *ka = strdup(key(a, fa));
    if (!ka) {
        fprintf(stderr, "join: out of memory\n");
        exit(2);
    }
    int c = strcoll(ka, key(b, fb));
    free(ka);
    return c;
}

/* read the next group of lines with equal join fields */
static void next_group(struct in *f)
{
    for (size_t i = 0; i < f->ng; i++)
        free_line(&f->group[i]);
    f->ng = 0;
    if (!f->have_next)
        f->have_next = read_line(f, &f->next);
    if (!f->have_next)
        return;
    do {
        if (f->ng == f->capg) {
            f->capg = f->capg ? f->capg * 2 : 8;
            f->group = xrealloc(f->group, f->capg * sizeof *f->group);
        }
        f->group[f->ng++] = f->next;
        f->have_next = read_line(f, &f->next);
    } while (f->have_next && keycmp(&f->next, f->n, &f->group[0], f->n) == 0);
}

/* ---- output ---- */

static bool first_field;

static void put_field(const char *s, size_t n)
{
    if (!first_field) {
        if (tsep) {
            if (fwrite(tsep, 1, tlen, stdout) != tlen)
                goto bad;
        } else if (putchar(' ') == EOF)
            goto bad;
    }
    first_field = false;
    if (n == 0 && have_o && estr)
        s = estr, n = strlen(estr);
    if (n && fwrite(s, 1, n, stdout) != n)
        goto bad;
    return;
bad:
    fprintf(stderr, "join: write error: %s\n", strerror(errno));
    exit(2);
}

static void end_line(void)
{
    if (putchar('\n') == EOF) {
        fprintf(stderr, "join: write error: %s\n", strerror(errno));
        exit(2);
    }
}

/* a line of output from l1 (file 1) and l2 (file 2); either may be NULL */
static void output(const struct line *l1, const struct line *l2)
{
    first_field = true;
    const struct line *l[3] = { NULL, l1, l2 };
    if (have_o) {
        for (size_t i = 0; i < nolist; i++) {
            const struct ofield *o = &olist[i];
            if (o->file == 0) {
                const struct line *src = l1 ? l1 : l2;
                int fn = l1 ? 1 : 2;
                size_t k = jf[fn] - 1;
                put_field(k < src->nf ? src->f[k] : "", k < src->nf ? src->fl[k] : 0);
            } else {
                const struct line *src = l[o->file];
                size_t k = o->field - 1;
                if (src && k < src->nf)
                    put_field(src->f[k], src->fl[k]);
                else
                    put_field("", 0);
            }
        }
        end_line();
        return;
    }
    const struct line *src = l1 ? l1 : l2;
    int fn = l1 ? 1 : 2;
    size_t k = jf[fn] - 1;
    put_field(k < src->nf ? src->f[k] : "", k < src->nf ? src->fl[k] : 0);
    for (int w = 1; w <= 2; w++) {
        if (!l[w])
            continue;
        for (size_t i = 0; i < l[w]->nf; i++)
            if (i != jf[w] - 1)
                put_field(l[w]->f[i], l[w]->fl[i]);
    }
    end_line();
}

/* ---- options ---- */

static size_t number(const char *s, const char *what)
{
    char *end;
    if (!isdigit((unsigned char)*s))
        goto bad;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (*end || errno || v == 0)
        goto bad;
    return (size_t)v;
bad:
    fprintf(stderr, "join: %s: not a valid %s\n", s, what);
    exit(2);
}

static int file_number(const char *s)
{
    if (strcmp(s, "1") == 0)
        return 1;
    if (strcmp(s, "2") == 0)
        return 2;
    fprintf(stderr, "join: %s: the file number is 1 or 2\n", s);
    exit(2);
}

static void parse_o(const char *s)
{
    have_o = true;
    const char *p = s;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        size_t n = strcspn(p, ", \t");
        struct ofield o;
        if (n == 1 && *p == '0') {
            o.file = 0;
            o.field = 0;
        } else if (n >= 3 && (p[0] == '1' || p[0] == '2') && p[1] == '.') {
            o.file = p[0] - '0';
            char tmp[32];
            if (n - 2 >= sizeof tmp)
                goto bad;
            memcpy(tmp, p + 2, n - 2);
            tmp[n - 2] = '\0';
            o.field = number(tmp, "field in -o");
        } else
            goto bad;
        olist = xrealloc(olist, (nolist + 1) * sizeof *olist);
        olist[nolist++] = o;
        p += n;
    }
    return;
bad:
    fprintf(stderr, "join: -o %s: not a valid list\n", s);
    exit(2);
}

static FILE *open_in(const char *name)
{
    if (strcmp(name, "-") == 0)
        return stdin;
    FILE *f = fopen(name, "r");
    if (!f) {
        fprintf(stderr, "join: %s: %s\n", name, strerror(errno));
        exit(2);
    }
    return f;
}

static void usage(void)
{
    fprintf(stderr, "usage: join [-a file_number|-v file_number] [-e string] [-o list] [-t char]\n"
                    "            [-1 field] [-2 field] file1 file2\n");
    exit(2);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "a:v:e:o:t:1:2:")) != -1) {
        switch (c) {
        case 'a': opt_a[file_number(optarg)] = true; break;
        case 'v': opt_v[file_number(optarg)] = true; break;
        case 'e': estr = optarg; break;
        case 'o': parse_o(optarg); break;
        case 't': {
            int k = mblen(optarg, strlen(optarg));
            if (k <= 0 || optarg[k]) {
                fprintf(stderr, "join: -t %s: the separator is one character\n", optarg);
                return 2;
            }
            tsep = optarg;
            tlen = (size_t)k;
            break;
        }
        case '1': jf[1] = number(optarg, "field"); break;
        case '2': jf[2] = number(optarg, "field"); break;
        default: usage();
        }
    }
    if (argc - optind != 2)
        usage();
    bool any_v = opt_v[1] || opt_v[2];
    struct in f1 = { argv[optind], open_in(argv[optind]), 1, { 0 }, false, NULL, 0, 0 };
    struct in f2 = { argv[optind + 1], open_in(argv[optind + 1]), 2, { 0 }, false, NULL, 0, 0 };
    next_group(&f1);
    next_group(&f2);
    while (f1.ng || f2.ng) {
        int r = !f1.ng ? 1 : !f2.ng ? -1 : keycmp(&f1.group[0], 1, &f2.group[0], 2);
        if (r < 0) {
            if (opt_a[1] || opt_v[1])
                for (size_t i = 0; i < f1.ng; i++)
                    output(&f1.group[i], NULL);
            next_group(&f1);
        } else if (r > 0) {
            if (opt_a[2] || opt_v[2])
                for (size_t i = 0; i < f2.ng; i++)
                    output(NULL, &f2.group[i]);
            next_group(&f2);
        } else {
            if (!any_v)
                for (size_t i = 0; i < f1.ng; i++)
                    for (size_t j = 0; j < f2.ng; j++)
                        output(&f1.group[i], &f2.group[j]);
            next_group(&f1);
            next_group(&f2);
        }
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "join: write error: %s\n", strerror(errno));
        return 2;
    }
    return 0;
}
