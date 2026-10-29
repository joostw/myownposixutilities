/*
 * sort.c - sort: sort, merge, or sequence check text files (POSIX.1-2024,
 * XCU sort)
 *
 * Synopsis:
 *   sort [-m] [-o output] [-bdfinru] [-t char] [-k keydef]... [file...]
 *   sort [-c|-C] [-bdfinru] [-t char] [-k keydef] [file]
 *
 * The input is read into memory and sorted there (there are no temporary
 * files, so the input must fit in memory); -m merges the files as it
 * reads them, a line from each at a time.  The keys of each line are
 * found once, before sorting: with -d, -f or -i, or in a locale whose
 * collation is not that of the POSIX locale, as a copy of the key in the
 * form that is compared (strcoll() then compares the copies); otherwise as
 * offsets into the line, compared with memcmp().
 *
 * Choices where the standard leaves room:
 *  - Options may also follow file operands (sort is excepted from
 *    Guideline 9); "--" ends them.  The obsolescent +pos1 -pos2 form of
 *    keys is not supported.
 *  - Ordering options given apart from -k apply to every key that has no
 *    modifiers of its own, wherever they appear on the command line; -b
 *    without -k makes the whole-line key start at its first non-blank.
 *  - A key's start or end that is past the end of its field runs on into
 *    the following fields, up to the end of the line (as in GNU sort).
 *  - -n: the numeric string is an optional '-', digits with the locale's
 *    thousands separator between them, and the locale's radix character
 *    with more digits; leading blanks are skipped (with or without -b).
 *    What follows it in the key is not compared.
 *  - -c: the message is "sort: FILE:N: disorder: LINE" (or "duplicate
 *    key"); -c and -C take at most one file.
 *  - -u: of lines with equal keys, the first in the (sorted) input is
 *    kept; -u with -m keeps the first in the order of the operands.
 *  - A last line without a newline gets one, without a warning.
 *  - -t takes one character, which may be a multibyte one.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

/* ---- options ---- */

enum { F_B_START = 1, F_B_END = 2, F_D = 4, F_F = 8, F_I = 16, F_N = 32, F_R = 64 };

struct key {
    size_t sfield, schar;       /* field_start: field, character (1-based) */
    size_t efield, echar;       /* field_end: field, character (0: the last) */
    bool has_end;
    int flags;
    bool own;                   /* has modifiers of its own */
};

static struct key *keys;
static size_t nkeys;
static int gflags;              /* the global ordering options */
static bool opt_c, opt_C, opt_m, opt_u;
static const char *output;
static char *tchar;             /* -t: the separator, as bytes; NULL: blanks */
static size_t tlen;
static bool c_collation;        /* the collation is that of the POSIX locale */
static bool multibyte;
static const char *radix = ".", *thousands = "";
static size_t radixlen = 1, thousandslen = 0;

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "sort: out of memory\n");
        exit(2);
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    if (!(p = realloc(p, n ? n : 1))) {
        fprintf(stderr, "sort: out of memory\n");
        exit(2);
    }
    return p;
}

static void usage(void)
{
    fprintf(stderr, "usage: sort [-m] [-o output] [-bdfinru] [-t char] [-k keydef]... [file...]\n"
                    "       sort [-c|-C] [-bdfinru] [-t char] [-k keydef] [file]\n");
    exit(2);
}

static int mod_flag(char c, bool start)
{
    switch (c) {
    case 'b': return start ? F_B_START : F_B_END;
    case 'd': return F_D;
    case 'f': return F_F;
    case 'i': return F_I;
    case 'n': return F_N;
    case 'r': return F_R;
    }
    return -1;
}

static size_t keynum(const char **p, const char *def, bool zero_ok)
{
    if (!isdigit((unsigned char)**p))
        goto bad;
    size_t v = 0;
    while (isdigit((unsigned char)**p)) {
        if (v > (SIZE_MAX - 9) / 10)
            v = SIZE_MAX / 2;
        else
            v = v * 10 + (size_t)(**p - '0');
        (*p)++;
    }
    if (v == 0 && !zero_ok)
        goto bad;
    return v;
bad:
    fprintf(stderr, "sort: -k %s: not a valid key definition\n", def);
    exit(2);
}

static void add_key(const char *def)
{
    struct key k = { 0 };
    const char *p = def;
    k.sfield = keynum(&p, def, false);
    k.schar = 1;
    if (*p == '.') {
        p++;
        k.schar = keynum(&p, def, false);
    }
    for (; *p && *p != ','; p++) {
        int f = mod_flag(*p, true);
        if (f < 0) {
            fprintf(stderr, "sort: -k %s: not a valid key definition\n", def);
            exit(2);
        }
        k.flags |= f;
        k.own = true;
    }
    if (*p == ',') {
        p++;
        k.has_end = true;
        k.efield = keynum(&p, def, false);
        if (*p == '.') {
            p++;
            k.echar = keynum(&p, def, true);
        }
        for (; *p; p++) {
            int f = mod_flag(*p, false);
            if (f < 0) {
                fprintf(stderr, "sort: -k %s: not a valid key definition\n", def);
                exit(2);
            }
            k.flags |= f;
            k.own = true;
        }
    }
    keys = xrealloc(keys, (nkeys + 1) * sizeof *keys);
    keys[nkeys++] = k;
}

/* ---- characters ---- */

/* the length of the character at p (at least 1); its value in *wc */
static size_t char_at(const char *p, const char *end, wint_t *wc)
{
    if (!multibyte || (unsigned char)*p < 0x80) {
        *wc = (unsigned char)*p;
        return 1;
    }
    mbstate_t st;
    memset(&st, 0, sizeof st);
    wchar_t w;
    size_t n = mbrtowc(&w, p, (size_t)(end - p), &st);
    if (n == (size_t)-1 || n == (size_t)-2 || n == 0) {
        *wc = WEOF;             /* not a character: a byte of its own */
        return 1;
    }
    *wc = (wint_t)w;
    return n;
}

static bool blank_at(const char *p, const char *end, size_t *len)
{
    wint_t wc;
    *len = char_at(p, end, &wc);
    if (wc == WEOF)
        return false;
    return multibyte ? iswblank(wc) : isblank((int)wc);
}

/* skip blanks */
static const char *skip_blanks(const char *p, const char *end)
{
    size_t n;
    while (p < end && blank_at(p, end, &n))
        p += n;
    return p;
}

/* advance n characters, not past end */
static const char *advance(const char *p, const char *end, size_t n)
{
    wint_t wc;
    while (n > 0 && p < end) {
        p += char_at(p, end, &wc);
        n--;
    }
    return p;
}

/* ---- fields and keys ---- */

/* the start and end of field n (1-based) of the line [s, end); NULL if the
   line has fewer fields */
static const char *field(const char *s, const char *end, size_t n, const char **fend)
{
    const char *p = s;
    size_t i = 1;
    if (tchar) {
        for (;;) {
            const char *q = p;
            while (q < end && !(end - q >= (ptrdiff_t)tlen && memcmp(q, tchar, tlen) == 0)) {
                wint_t wc;
                q += char_at(q, end, &wc);
            }
            if (i == n) {
                *fend = q;
                return p;
            }
            if (q >= end)
                return NULL;
            p = q + tlen;
            i++;
        }
    }
    for (;;) {
        /* a field: blanks, then non-blanks */
        const char *q = skip_blanks(p, end);
        size_t bl;
        while (q < end && !blank_at(q, end, &bl)) {
            wint_t wc;
            q += char_at(q, end, &wc);
        }
        if (i == n) {
            *fend = q;
            return p;
        }
        if (q >= end)
            return NULL;
        p = q;
        i++;
    }
}

/* the part of the line that key k covers */
static void key_span(const struct key *k, const char *s, const char *end,
                     const char **ks, const char **ke)
{
    const char *fe;
    const char *f = field(s, end, k->sfield, &fe);
    if (!f) {
        *ks = *ke = end;
        return;
    }
    const char *start = f;
    if (k->flags & F_B_START)
        start = skip_blanks(start, end);
    start = advance(start, end, k->schar - 1);
    const char *stop = end;
    if (k->has_end) {
        const char *g = field(s, end, k->efield, &fe);
        if (!g)
            stop = end;
        else if (k->echar == 0)
            stop = fe;
        else {
            /* as in GNU sort, the end may run on past the field */
            const char *q = g;
            if (k->flags & F_B_END)
                q = skip_blanks(q, end);
            stop = advance(q, end, k->echar);
        }
    }
    if (stop < start)
        stop = start;           /* an empty key */
    *ks = start;
    *ke = stop;
}

/* the key text as compared: -d -i -f applied; malloc'ed and NUL-terminated */
static char *transform(const char *s, const char *e, int flags, size_t *len)
{
    char *out = xmalloc((size_t)(e - s) * (multibyte ? MB_CUR_MAX : 1) + 1);
    size_t k = 0;
    while (s < e) {
        wint_t wc;
        size_t n = char_at(s, e, &wc);
        if (wc == WEOF || (!multibyte && (flags & (F_D | F_I | F_F)) == 0)) {
            memcpy(out + k, s, n);
            k += n;
            s += n;
            continue;
        }
        bool keep = true;
        if (flags & F_D)
            keep = multibyte ? (iswblank(wc) || iswalnum(wc)) : (isblank((int)wc) || isalnum((int)wc));
        if (keep && (flags & F_I))
            keep = multibyte ? iswprint(wc) : isprint((int)wc);
        if (keep) {
            if (flags & F_F) {
                if (multibyte) {
                    wint_t u = towupper(wc);
                    mbstate_t st;
                    memset(&st, 0, sizeof st);
                    size_t m = wcrtomb(out + k, (wchar_t)u, &st);
                    if (m == (size_t)-1) {
                        memcpy(out + k, s, n);
                        m = n;
                    }
                    k += m;
                } else
                    out[k++] = (char)toupper((int)wc);
            } else {
                memcpy(out + k, s, n);
                k += n;
            }
        }
        s += n;
    }
    out[k] = '\0';
    *len = k;
    return out;
}

/* ---- numbers ---- */

/* compare the initial numeric strings of a and b */
static int numcmp(const char *a, const char *ae, const char *b, const char *be)
{
    const char *p[2] = { a, b }, *end[2] = { ae, be };
    bool neg[2];
    const char *ip[2], *fp[2];  /* the integer and fraction digits */
    size_t in[2], fn[2];
    for (int w = 0; w < 2; w++) {
        const char *q = skip_blanks(p[w], end[w]);
        neg[w] = false;
        if (q < end[w] && *q == '-') {
            neg[w] = true;
            q++;
        }
        /* the integer digits, without leading zeros or thousands separators */
        while (q < end[w]) {
            if (*q == '0')
                q++;
            else if (thousandslen && end[w] - q > (ptrdiff_t)thousandslen &&
                     memcmp(q, thousands, thousandslen) == 0 && q > p[w] && isdigit((unsigned char)q[-1]) &&
                     isdigit((unsigned char)q[thousandslen]))
                q += thousandslen;
            else
                break;
        }
        ip[w] = q;
        in[w] = 0;
        /* count the digits that matter; the separators are skipped when comparing */
        const char *r = q;
        while (r < end[w]) {
            if (isdigit((unsigned char)*r)) {
                in[w]++;
                r++;
            } else if (thousandslen && end[w] - r > (ptrdiff_t)thousandslen &&
                       memcmp(r, thousands, thousandslen) == 0 && r > p[w] && isdigit((unsigned char)r[-1]) &&
                       isdigit((unsigned char)r[thousandslen]))
                r += thousandslen;
            else
                break;
        }
        fp[w] = NULL;
        fn[w] = 0;
        if (end[w] - r >= (ptrdiff_t)radixlen && memcmp(r, radix, radixlen) == 0) {
            r += radixlen;
            fp[w] = r;
            while (r < end[w] && isdigit((unsigned char)*r))
                r++, fn[w]++;
            while (fn[w] > 0 && fp[w][fn[w] - 1] == '0')
                fn[w]--;
        }
        if (in[w] == 0 && fn[w] == 0)
            neg[w] = false;     /* -0 is 0 */
    }
    if (neg[0] != neg[1])
        return neg[0] ? -1 : 1;
    int sign = neg[0] ? -1 : 1;
    if (in[0] != in[1])
        return in[0] < in[1] ? -sign : sign;
    /* the same number of integer digits: compare them, skipping separators */
    const char *x = ip[0], *y = ip[1];
    for (size_t i = 0; i < in[0]; i++) {
        while (!isdigit((unsigned char)*x))
            x += thousandslen;
        while (!isdigit((unsigned char)*y))
            y += thousandslen;
        if (*x != *y)
            return *x < *y ? -sign : sign;
        x++, y++;
    }
    size_t n = fn[0] < fn[1] ? fn[0] : fn[1];
    for (size_t i = 0; i < n; i++)
        if (fp[0][i] != fp[1][i])
            return fp[0][i] < fp[1][i] ? -sign : sign;
    if (fn[0] != fn[1])
        return fn[0] < fn[1] ? -sign : sign;
    return 0;
}

/* ---- lines ---- */

struct kval {
    const char *s;              /* the key: in the line, or a copy */
    size_t len;
    const char *e;              /* for -n: the end in the line */
};

struct line {
    char *s;                    /* NUL-terminated, without the newline */
    size_t len;
    struct kval *k;
    size_t fileno;              /* for -m, the operand it came from */
    size_t seq;                 /* its place in the input */
};

static struct key whole;        /* the default key: the whole line */

static const struct key *key_of(size_t i)
{
    return nkeys ? &keys[i] : &whole;
}

static int key_flags(const struct key *k)
{
    if (k->own)
        return k->flags;
    return gflags;
}

static bool needs_copy(int flags)
{
    return !(flags & F_N) && ((flags & (F_D | F_F | F_I)) || !c_collation);
}

static void make_keys(struct line *l)
{
    size_t n = nkeys ? nkeys : 1;
    l->k = xmalloc(n * sizeof *l->k);
    const char *end = l->s + l->len;
    for (size_t i = 0; i < n; i++) {
        const struct key *k = key_of(i);
        int flags = key_flags(k);
        struct key kk = *k;
        kk.flags = flags;
        const char *ks, *ke;
        if (k == &whole) {
            ks = (flags & (F_B_START | F_B_END)) ? skip_blanks(l->s, end) : l->s;
            ke = end;
        } else
            key_span(&kk, l->s, end, &ks, &ke);
        l->k[i].e = ke;
        if (needs_copy(flags)) {
            l->k[i].s = transform(ks, ke, flags, &l->k[i].len);
        } else {
            l->k[i].s = ks;
            l->k[i].len = (size_t)(ke - ks);
        }
    }
}

static void free_keys(struct line *l)
{
    size_t n = nkeys ? nkeys : 1;
    for (size_t i = 0; i < n; i++)
        if (needs_copy(key_flags(key_of(i))))
            free((char *)l->k[i].s);
    free(l->k);
}

static int bytecmp(const char *a, size_t an, const char *b, size_t bn)
{
    size_t n = an < bn ? an : bn;
    int c = memcmp(a, b, n);
    if (c)
        return c;
    return an < bn ? -1 : an > bn ? 1 : 0;
}

static int textcmp(const char *a, size_t an, const char *b, size_t bn)
{
    if (c_collation)
        return bytecmp(a, an, b, bn);
    int c = strcoll(a, b);
    return c ? c : 0;
}

/* compare by the keys only */
static int keycmp(const struct line *a, const struct line *b)
{
    size_t n = nkeys ? nkeys : 1;
    for (size_t i = 0; i < n; i++) {
        int flags = key_flags(key_of(i));
        int c;
        if (flags & F_N)
            c = numcmp(a->k[i].s, a->k[i].e, b->k[i].s, b->k[i].e);
        else if (needs_copy(flags) && !c_collation)
            c = strcoll(a->k[i].s, b->k[i].s);
        else
            c = bytecmp(a->k[i].s, a->k[i].len, b->k[i].s, b->k[i].len);
        if (c)
            return (flags & F_R) ? -c : c;
    }
    return 0;
}

/* compare completely: the keys, then the whole line */
static int linecmp(const struct line *a, const struct line *b)
{
    int c = keycmp(a, b);
    if (c || opt_u)
        return c;
    c = textcmp(a->s, a->len, b->s, b->len);
    if (!c)
        c = bytecmp(a->s, a->len, b->s, b->len);
    return (gflags & F_R) ? -c : c;
}

static int qcmp(const void *x, const void *y)
{
    const struct line *a = *(struct line *const *)x, *b = *(struct line *const *)y;
    int c = linecmp(a, b);
    if (c == 0)                 /* keep the order of the input: stable */
        c = a->seq < b->seq ? -1 : a->seq > b->seq;
    return c;
}

/* ---- input ---- */

struct input {
    const char *name;
    FILE *fp;
    char *buf;
    size_t cap;
};

static bool open_input(struct input *in, const char *name)
{
    in->name = name;
    in->buf = NULL;
    in->cap = 0;
    if (strcmp(name, "-") == 0) {
        in->fp = stdin;
        return true;
    }
    if (!(in->fp = fopen(name, "r"))) {
        fprintf(stderr, "sort: %s: %s\n", name, strerror(errno));
        return false;
    }
    return true;
}

/* the next line, NUL-terminated without its newline; false at the end */
static bool next_line(struct input *in, char **s, size_t *len)
{
    ssize_t n = getline(&in->buf, &in->cap, in->fp);
    if (n < 0) {
        if (ferror(in->fp)) {
            fprintf(stderr, "sort: %s: %s\n", in->name, strerror(errno));
            exit(2);
        }
        return false;
    }
    if (n > 0 && in->buf[n - 1] == '\n')
        in->buf[--n] = '\0';
    *s = in->buf;
    *len = (size_t)n;
    return true;
}

static void close_input(struct input *in)
{
    if (in->fp && in->fp != stdin)
        fclose(in->fp);
    free(in->buf);
}

static struct line *new_line(const char *s, size_t len)
{
    struct line *l = xmalloc(sizeof *l);
    l->s = xmalloc(len + 1);
    memcpy(l->s, s, len);
    l->s[len] = '\0';
    l->len = len;
    make_keys(l);
    return l;
}

static void free_line(struct line *l)
{
    free_keys(l);
    free(l->s);
    free(l);
}

/* ---- output ---- */

static FILE *out;
static const char *outname = "standard output";

static void put(const struct line *l)
{
    if (fwrite(l->s, 1, l->len, out) != l->len || putc('\n', out) == EOF) {
        fprintf(stderr, "sort: %s: %s\n", outname, strerror(errno));
        exit(2);
    }
}

static void open_output(void)
{
    out = stdout;
    if (output) {
        if (!(out = fopen(output, "w"))) {
            fprintf(stderr, "sort: %s: %s\n", output, strerror(errno));
            exit(2);
        }
        outname = output;
    }
}

static void close_output(void)
{
    if (fflush(out) != 0 || ferror(out) || (out != stdout && fclose(out) != 0)) {
        fprintf(stderr, "sort: %s: %s\n", outname, strerror(errno));
        exit(2);
    }
}

/* ---- the three functions ---- */

static int check(const char *name)
{
    struct input in;
    if (!open_input(&in, name))
        return 2;
    struct line *prev = NULL;
    char *s;
    size_t len, n = 0;
    int r = 0;
    while (next_line(&in, &s, &len)) {
        n++;
        struct line *l = new_line(s, len);
        if (prev) {
            int c = linecmp(prev, l);
            if (c > 0 || (opt_u && c == 0)) {
                if (!opt_C)
                    fprintf(stderr, "sort: %s:%zu: %s: %s\n", name, n,
                            c > 0 ? "disorder" : "duplicate key", l->s);
                r = 1;
                free_line(l);
                break;
            }
            free_line(prev);
        }
        prev = l;
    }
    if (prev)
        free_line(prev);
    close_input(&in);
    return r;
}

static bool same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static void sort_all(char **files, int nfiles)
{
    struct line **v = NULL;
    size_t n = 0, cap = 0;
    for (int i = 0; i < nfiles; i++) {
        struct input in;
        if (!open_input(&in, files[i]))
            exit(2);
        char *s;
        size_t len;
        while (next_line(&in, &s, &len)) {
            if (n == cap) {
                cap = cap ? cap * 2 : 1024;
                v = xrealloc(v, cap * sizeof *v);
            }
            v[n] = new_line(s, len);
            v[n]->fileno = (size_t)i;
            v[n]->seq = n;
            n++;
        }
        close_input(&in);
    }
    qsort(v, n, sizeof *v, qcmp);
    open_output();
    for (size_t i = 0; i < n; i++) {
        if (!opt_u || i == 0 || keycmp(v[i - 1], v[i]) != 0)
            put(v[i]);
    }
    close_output();
    for (size_t i = 0; i < n; i++)
        free_line(v[i]);
    free(v);
}

static void merge(char **files, int nfiles)
{
    /* an output that is also an input: read everything first */
    if (output)
        for (int i = 0; i < nfiles; i++)
            if (strcmp(files[i], "-") != 0 && same_file(files[i], output)) {
                sort_all(files, nfiles);
                return;
            }
    struct input *in = xmalloc((size_t)nfiles * sizeof *in);
    struct line **cur = xmalloc((size_t)nfiles * sizeof *cur);
    for (int i = 0; i < nfiles; i++) {
        if (!open_input(&in[i], files[i]))
            exit(2);
        char *s;
        size_t len;
        cur[i] = NULL;
        if (next_line(&in[i], &s, &len)) {
            cur[i] = new_line(s, len);
            cur[i]->fileno = (size_t)i;
        }
    }
    open_output();
    struct line *last = NULL;
    for (;;) {
        int best = -1;
        for (int i = 0; i < nfiles; i++)
            if (cur[i] && (best < 0 || linecmp(cur[i], cur[best]) < 0))
                best = i;
        if (best < 0)
            break;
        struct line *l = cur[best];
        if (!opt_u || !last || keycmp(last, l) != 0) {
            put(l);
            if (last)
                free_line(last);
            last = l;
        } else
            free_line(l);
        char *s;
        size_t len;
        cur[best] = NULL;
        if (next_line(&in[best], &s, &len)) {
            cur[best] = new_line(s, len);
            cur[best]->fileno = (size_t)best;
        }
    }
    if (last)
        free_line(last);
    close_output();
    for (int i = 0; i < nfiles; i++)
        close_input(&in[i]);
    free(in);
    free(cur);
}

/* ---- main ---- */

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    multibyte = MB_CUR_MAX > 1;
    const char *coll = setlocale(LC_COLLATE, NULL);
    c_collation = !coll || strcmp(coll, "C") == 0 || strcmp(coll, "POSIX") == 0;
    struct lconv *lc = localeconv();
    if (lc->decimal_point && *lc->decimal_point)
        radix = lc->decimal_point;
    if (lc->thousands_sep)
        thousands = lc->thousands_sep;
    radixlen = strlen(radix);
    thousandslen = strlen(thousands);

    char **files = xmalloc((size_t)argc * sizeof *files);
    int nfiles = 0;
    bool end_of_options = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (end_of_options || a[0] != '-' || a[1] == '\0') {
            files[nfiles++] = argv[i];
            continue;
        }
        if (strcmp(a, "--") == 0) {
            end_of_options = true;
            continue;
        }
        for (const char *p = a + 1; *p; p++) {
            const char *arg = NULL;
            if (strchr("otk", *p)) {
                if (p[1])
                    arg = p + 1;
                else if (i + 1 < argc)
                    arg = argv[++i];
                else {
                    fprintf(stderr, "sort: -%c needs an argument\n", *p);
                    usage();
                }
            }
            switch (*p) {
            case 'c': opt_c = true; opt_C = false; break;
            case 'C': opt_C = true; opt_c = false; break;
            case 'm': opt_m = true; break;
            case 'u': opt_u = true; break;
            case 'b': gflags |= F_B_START | F_B_END; break;
            case 'd': gflags |= F_D; break;
            case 'f': gflags |= F_F; break;
            case 'i': gflags |= F_I; break;
            case 'n': gflags |= F_N; break;
            case 'r': gflags |= F_R; break;
            case 'o': output = arg; break;
            case 'k': add_key(arg); break;
            case 't': {
                wint_t wc;
                size_t n = *arg ? char_at(arg, arg + strlen(arg), &wc) : 0;
                if (n == 0 || arg[n]) {
                    fprintf(stderr, "sort: -t %s: the separator is one character\n", arg);
                    exit(2);
                }
                tchar = xmalloc(n + 1);
                memcpy(tchar, arg, n);
                tchar[n] = '\0';
                tlen = n;
                break;
            }
            default:
                fprintf(stderr, "sort: -%c: unknown option\n", *p);
                usage();
            }
            if (arg)
                break;
        }
    }
    whole.sfield = 1;
    whole.schar = 1;
    whole.own = false;
    static char *dash[] = { "-" };
    if (opt_c || opt_C) {
        if (nfiles > 1) {
            fprintf(stderr, "sort: -c and -C take one file\n");
            usage();
        }
        return check(nfiles ? files[0] : "-");
    }
    if (nfiles == 0) {
        files = dash;
        nfiles = 1;
    }
    if (opt_m)
        merge(files, nfiles);
    else
        sort_all(files, nfiles);
    return 0;
}
