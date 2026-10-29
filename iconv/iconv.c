/*
 * iconv.c - iconv: codeset conversion (POSIX.1-2024, XCU iconv)
 *
 * Synopsis:
 *   iconv [-cs] -f frommap -t tomap [file...]
 *   iconv -f fromcode [-cs] [-t tocode] [file...]
 *   iconv -t tocode [-cs] [-f fromcode] [file...]
 *   iconv -l
 *
 * Codesets named without a slash are those of iconv_open(); the text goes
 * from the input codeset to UTF-32 and from there to the output codeset,
 * so that a character that is not valid input and one that the output
 * codeset lacks can be told apart (and skipped whole).  A name with a
 * slash is a charmap file (XBD 6.4); two charmaps are joined on their
 * symbolic names.  A missing -f or -t is the codeset of the locale.
 *
 * Choices where the standard leaves room (the "system documentation"):
 *  - A character that is not valid in the input codeset, or that has no
 *    counterpart in the output codeset, is reported (unless -s); without
 *    -c the conversion of that file stops there, with -c the character is
 *    left out and the conversion goes on.  Either way the status is 1.
 *  - An incomplete character at the end of a file is reported the same
 *    way.  Each file starts in the initial shift state, and the output is
 *    brought back to it at the end of each file.
 *  - Only two charmaps can be joined: a charmap with a codeset name, or a
 *    charmap with the locale's codeset, is an error.  A charmap byte
 *    sequence is found by the longest match; ranges (<a0>...<a9>, decimal,
 *    and <a0>..<af>, hexadecimal) are understood, the WIDTH sections and
 *    anything after END CHARMAP are ignored.
 *  - -l writes one name a line: those that the iconv() of the system
 *    accepts, from a list of its own and from the gconv-modules files of
 *    the C library, when there are any; not those with a slash, which
 *    would name charmaps.
 *  - Usage errors exit with status 2; other errors with status 1.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool opt_c, opt_s;
static int status;
static const char *fname;       /* the current input, for messages */

static void usage(void)
{
    fprintf(stderr, "usage: iconv [-cs] [-f fromcode] [-t tocode] [file...]\n"
                    "       iconv [-cs] -f frommap -t tomap [file...]\n"
                    "       iconv -l\n");
    exit(2);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "iconv: %s\n", strerror(ENOMEM));
        exit(1);
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        fprintf(stderr, "iconv: %s\n", strerror(ENOMEM));
        exit(1);
    }
    return p;
}

/* a bad character at byte off of the current file */
static void bad(const char *what, unsigned long long off)
{
    if (!opt_s)
        fprintf(stderr, "iconv: %s: %s at byte %llu\n", fname, what, off);
    status = 1;
}

static void put(const char *p, size_t n)
{
    if (n && fwrite(p, 1, n, stdout) != n) {
        fprintf(stderr, "iconv: standard output: %s\n", strerror(errno));
        exit(1);
    }
}

/* ---- charmaps ---- */

struct entry {
    char *name;
    unsigned char bytes[8];
    int len;
};

struct charmap {
    struct entry *e;
    size_t n, cap;
};

static struct charmap from_map, to_map;

static int cmp_name(const void *a, const void *b)
{
    return strcmp(((const struct entry *)a)->name, ((const struct entry *)b)->name);
}

static int cmp_bytes(const void *a, const void *b)
{
    const struct entry *x = a, *y = b;
    int n = x->len < y->len ? x->len : y->len;
    int c = memcmp(x->bytes, y->bytes, (size_t)n);
    return c ? c : x->len - y->len;
}

static void add(struct charmap *m, const char *name, const unsigned char *b, int len)
{
    if (m->n == m->cap) {
        m->cap = m->cap ? 2 * m->cap : 256;
        m->e = xrealloc(m->e, m->cap * sizeof *m->e);
    }
    struct entry *e = &m->e[m->n++];
    e->name = xmalloc(strlen(name) + 1);
    strcpy(e->name, name);
    memcpy(e->bytes, b, (size_t)len);
    e->len = len;
}

/* a symbolic name <...> at *pp, into buf; false if there is none */
static bool symbol(char **pp, char esc, char *buf, size_t size)
{
    char *p = *pp;
    size_t n = 0;
    if (*p != '<')
        return false;
    for (p++; *p && *p != '>'; p++) {
        if (*p == esc && p[1])
            p++;
        if (n + 1 < size)
            buf[n++] = *p;
    }
    if (*p != '>')
        return false;
    buf[n] = '\0';
    *pp = p + 1;
    return true;
}

/* the value of a digit in base, or -1 */
static int digit(int c, int base)
{
    int v = isdigit(c) ? c - '0' : isxdigit(c) ? tolower(c) - 'a' + 10 : 99;
    return v < base ? v : -1;
}

/* the encoding (escaped bytes) at *pp; its length, or -1 */
static int encoding(char **pp, char esc, unsigned char *b)
{
    char *p = *pp;
    int n = 0;
    while (*p == esc && n < 8) {
        int base, max;
        p++;
        if (*p == 'x' || *p == 'X')
            base = 16, max = 2, p++;
        else if (*p == 'd' || *p == 'D')
            base = 10, max = 3, p++;
        else
            base = 8, max = 3;
        unsigned v = 0;
        int k, d;
        for (k = 0; k < max && (d = digit((unsigned char)*p, base)) >= 0; k++, p++)
            v = v * (unsigned)base + (unsigned)d;
        if (k == 0 || v > 255)
            return -1;
        b[n++] = (unsigned char)v;
    }
    *pp = p;
    return n ? n : -1;
}

/* the length of the prefix that the names of a range have in common
   (the rest is the number); 0 if they do not make a range */
static size_t range_prefix(const char *a, const char *z, int base)
{
    size_t n = strlen(a), k = 0;
    if (strlen(z) != n)
        return 0;
    while (k < n && a[k] == z[k])
        k++;
    if (k == n)
        k--;
    for (size_t i = k; i < n; i++)
        if (digit((unsigned char)a[i], base) < 0 || digit((unsigned char)z[i], base) < 0)
            return 0;
    return k == 0 && n == 0 ? 0 : k + 1;   /* plus one: 0 means none */
}

static void read_charmap(const char *path, struct charmap *m)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "iconv: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    char esc = '\\', com = '#';
    char *line = NULL;
    size_t cap = 0;
    bool in = false;
    unsigned long lineno = 0;
    while (getline(&line, &cap, f) > 0) {
        lineno++;
        char *p = line;
        line[strcspn(line, "\n")] = '\0';
        while (isblank((unsigned char)*p))
            p++;
        if (*p == '\0' || *p == com)
            continue;
        if (!in) {
            char kw[64];
            if (sscanf(p, "<escape_char> %63s", kw) == 1 && kw[0])
                esc = kw[0];
            else if (sscanf(p, "<comment_char> %63s", kw) == 1 && kw[0])
                com = kw[0];
            else if (strncmp(p, "CHARMAP", 7) == 0 && (p[7] == '\0' || isblank((unsigned char)p[7])))
                in = true;
            continue;
        }
        if (strncmp(p, "END", 3) == 0 && isblank((unsigned char)p[3]))
            break;
        char a[256], z[256];
        if (!symbol(&p, esc, a, sizeof a))
            goto bad;
        int base = 0;
        if (strncmp(p, "...", 3) == 0)
            base = 10, p += 3;
        else if (strncmp(p, "..", 2) == 0)
            base = 16, p += 2;
        if (base && !symbol(&p, esc, z, sizeof z))
            goto bad;
        while (isblank((unsigned char)*p))
            p++;
        unsigned char b[8];
        int len = encoding(&p, esc, b);
        if (len < 0)
            goto bad;
        if (!base) {
            add(m, a, b, len);
            continue;
        }
        size_t pa = range_prefix(a, z, base);
        if (pa-- == 0)
            goto bad;
        unsigned long na = strtoul(a + pa, NULL, base), nz = strtoul(z + pa, NULL, base);
        if (nz < na || nz - na > 1000000)
            goto bad;
        int width = (int)(strlen(a) - pa);
        bool lower = false;
        for (const char *q = a + pa; *q; q++)
            lower |= islower((unsigned char)*q) != 0;
        for (const char *q = z + pa; *q; q++)
            lower |= islower((unsigned char)*q) != 0;
        for (unsigned long v = na; v <= nz; v++) {
            char name[300];
            snprintf(name, sizeof name, base == 16 ? "%.*s%0*lX" : "%.*s%0*lu", (int)pa, a, width, v);
            if (lower)
                for (char *q = name + pa; *q; q++)
                    *q = (char)tolower((unsigned char)*q);
            add(m, name, b, len);
            /* the next encoding: one more, as a number */
            for (int k = len - 1; k >= 0 && ++b[k] == 0; k--)
                ;
        }
        continue;
bad:
        fprintf(stderr, "iconv: %s: line %lu: not understood\n", path, lineno);
        exit(1);
    }
    free(line);
    fclose(f);
    if (!in || m->n == 0) {
        fprintf(stderr, "iconv: %s: not a charmap\n", path);
        exit(1);
    }
}

/* the to_map entry for a name, or NULL */
static const struct entry *by_name(const char *name)
{
    struct entry key = { .name = (char *)name };
    return bsearch(&key, to_map.e, to_map.n, sizeof key, cmp_name);
}

static int maxlen;               /* the longest from_map sequence */

/* the from_map entry for bytes b[0..len), or NULL */
static const struct entry *by_bytes(const unsigned char *b, int len)
{
    struct entry key;
    memcpy(key.bytes, b, (size_t)len);
    key.len = len;
    return bsearch(&key, from_map.e, from_map.n, sizeof key, cmp_bytes);
}

/* does some from_map sequence start with b[0..len)? */
static bool is_prefix(const unsigned char *b, int len)
{
    size_t lo = 0, hi = from_map.n;
    struct entry key;
    memcpy(key.bytes, b, (size_t)len);
    key.len = len;
    while (lo < hi) {           /* the first entry not below b */
        size_t mid = (lo + hi) / 2;
        if (cmp_bytes(&from_map.e[mid], &key) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < from_map.n && from_map.e[lo].len > len
        && memcmp(from_map.e[lo].bytes, b, (size_t)len) == 0;
}

/* convert buf[0..n) with the charmaps; the bytes used (an incomplete
   character at the end stays for more input, unless at the end) */
static size_t map_convert(const unsigned char *buf, size_t n, bool end,
                          unsigned long long base, bool *stop)
{
    size_t i = 0;
    while (i < n) {
        const struct entry *best = NULL;
        int rest = n - i < (size_t)maxlen ? (int)(n - i) : maxlen;
        for (int len = rest; len >= 1 && !best; len--)
            best = by_bytes(buf + i, len);
        if (!best || best->len < rest) {
            /* a longer sequence may need more input */
            if (!end && rest < maxlen && is_prefix(buf + i, rest))
                return i;
        }
        if (!best) {
            bad(end && is_prefix(buf + i, rest) ? "incomplete character" : "invalid character", base + i);
            if (!opt_c) {
                *stop = true;
                return n;
            }
            i++;
            continue;
        }
        const struct entry *t = by_name(best->name);
        if (!t) {
            bad("character not in the output codeset", base + i);
            if (!opt_c) {
                *stop = true;
                return n;
            }
        } else {
            put((const char *)t->bytes, (size_t)t->len);
        }
        i += (size_t)best->len;
    }
    return i;
}

/* ---- iconv() ---- */

static iconv_t cd1, cd2;        /* to UTF-32, from UTF-32; or cd1 alone */
static bool two;

/* run the second step over the UTF-32 in mid */
static void step2(char *mid, size_t n, bool *stop)
{
    char obuf[16384];
    char *in = mid;
    size_t left = n;
    while (left > 0 && !*stop) {
        char *o = obuf;
        size_t ol = sizeof obuf;
        size_t r = iconv(cd2, &in, &left, &o, &ol);
        put(obuf, (size_t)(o - obuf));
        if (r != (size_t)-1)
            continue;
        if (errno == E2BIG)
            continue;
        unsigned char *u = (unsigned char *)in;
        if (!opt_s)
            fprintf(stderr, "iconv: %s: U+%04lX: not in the output codeset\n", fname,
                    (unsigned long)u[0] << 24 | (unsigned long)u[1] << 16 | (unsigned long)u[2] << 8 | u[3]);
        status = 1;
        if (!opt_c) {
            *stop = true;
            return;
        }
        in += 4, left -= 4;     /* one character of UTF-32 */
    }
}

static void finish(iconv_t cd)
{
    char obuf[64];
    char *o = obuf;
    size_t ol = sizeof obuf;
    iconv(cd, NULL, NULL, &o, &ol);
    put(obuf, (size_t)(o - obuf));
}

static void do_fd(int fd)
{
    static unsigned char buf[65536 + 16];
    size_t have = 0;
    unsigned long long off = 0;     /* of buf[0] in the file */
    bool stop = false, eof = false;
    if (!from_map.n) {
        iconv(cd1, NULL, NULL, NULL, NULL);
        if (two)
            iconv(cd2, NULL, NULL, NULL, NULL);
    }
    while (!stop) {
        if (!eof) {
            ssize_t r = read(fd, buf + have, sizeof buf - 16 - have);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                fprintf(stderr, "iconv: %s: %s\n", fname, strerror(errno));
                status = 1;
                break;
            }
            if (r == 0)
                eof = true;
            have += (size_t)r;
        }
        if (have == 0 && eof)
            break;
        size_t used;
        if (from_map.n) {
            used = map_convert(buf, have, eof, off, &stop);
        } else {
            static char mid[4 * (65536 + 16) + 64];
            char *in = (char *)buf;
            size_t left = have;
            while (left > 0 && !stop) {
                char *m = mid;
                size_t ml = sizeof mid;
                size_t r = iconv(cd1, &in, &left, &m, &ml);
                int e = errno;
                if (two)
                    step2(mid, (size_t)(m - mid), &stop);
                else
                    put(mid, (size_t)(m - mid));
                if (r != (size_t)-1 || stop)
                    continue;
                if (e == E2BIG)
                    continue;
                if (e == EINVAL && !eof)
                    break;      /* the rest of a character: read more */
                unsigned long long at = off + (size_t)(in - (char *)buf);
                if (e == EINVAL)
                    bad("incomplete character at the end", at);
                else if (!two)
                    bad("invalid character, or one not in the output codeset", at);
                else
                    bad("invalid character", at);
                if (!opt_c || e == EINVAL) {
                    stop = true;
                    break;
                }
                in++, left--;
            }
            used = have - left;
        }
        memmove(buf, buf + used, have - used);
        have -= used;
        off += used;
        if (eof && !stop && have > 0 && used == 0)
            break;
        if (eof && have == 0)
            break;
    }
    if (!from_map.n)
        finish(two ? cd2 : cd1);
}

static void list(void)
{
    static const char *const known[] = {
        "ASCII", "ANSI_X3.4-1968", "US-ASCII", "UTF-8", "UTF-16", "UTF-16BE",
        "UTF-16LE", "UTF-32", "UTF-32BE", "UTF-32LE", "UCS-2", "UCS-2BE",
        "UCS-2LE", "UCS-4", "UCS-4BE", "UCS-4LE", "UTF-7", "WCHAR_T",
        "ISO-8859-1", "ISO-8859-2", "ISO-8859-3", "ISO-8859-4", "ISO-8859-5",
        "ISO-8859-6", "ISO-8859-7", "ISO-8859-8", "ISO-8859-9", "ISO-8859-10",
        "ISO-8859-11", "ISO-8859-13", "ISO-8859-14", "ISO-8859-15",
        "ISO-8859-16", "KOI8-R", "KOI8-U", "CP437", "CP850", "CP852", "CP866",
        "CP1250", "CP1251", "CP1252", "CP1253", "CP1254", "CP1255", "CP1256",
        "CP1257", "CP1258", "MACINTOSH", "EUC-JP", "SHIFT_JIS", "ISO-2022-JP",
        "EUC-KR", "ISO-2022-KR", "EUC-TW", "GB2312", "GBK", "GB18030", "BIG5",
        "BIG5-HKSCS", "TIS-620", "VISCII", "EBCDIC-US", "IBM037", "IBM500",
        NULL
    };
    char **names = NULL;
    size_t n = 0, cap = 0;
#define PUSH(s) do { \
        if (n == cap) names = xrealloc(names, (cap = cap ? 2 * cap : 512) * sizeof *names); \
        names[n] = xmalloc(strlen(s) + 1); strcpy(names[n++], s); } while (0)
    for (int i = 0; known[i]; i++)
        PUSH(known[i]);
    static const char *const dirs[] = {
        "/usr/lib/x86_64-linux-gnu/gconv", "/usr/lib/aarch64-linux-gnu/gconv",
        "/usr/lib64/gconv", "/usr/lib/gconv", "/usr/local/lib/gconv", NULL
    };
    for (int d = 0; dirs[d]; d++) {
        char path[512];
        for (int k = 0; k < 2; k++) {
            DIR *dp = NULL;
            struct dirent *de = NULL;
            if (k == 1) {
                snprintf(path, sizeof path, "%s/gconv-modules.d", dirs[d]);
                if (!(dp = opendir(path)))
                    continue;
            }
            for (;;) {
                if (k == 1) {
                    if (!(de = readdir(dp)))
                        break;
                    size_t l = strlen(de->d_name);
                    if (l < 5 || strcmp(de->d_name + l - 5, ".conf") != 0)
                        continue;
                    snprintf(path, sizeof path, "%s/gconv-modules.d/%s", dirs[d], de->d_name);
                } else {
                    snprintf(path, sizeof path, "%s/gconv-modules", dirs[d]);
                }
                FILE *f = fopen(path, "r");
                if (f) {
                    char line[512], w[3][256];
                    while (fgets(line, sizeof line, f)) {
                        if (sscanf(line, "%255s %255s %255s", w[0], w[1], w[2]) < 3)
                            continue;
                        if (strcmp(w[0], "alias") && strcmp(w[0], "module"))
                            continue;
                        for (int j = 1; j < 3; j++) {
                            size_t l = strlen(w[j]);
                            while (l > 0 && w[j][l - 1] == '/')
                                w[j][--l] = '\0';
                            if (l && strcmp(w[j], "INTERNAL") != 0)
                                PUSH(w[j]);
                        }
                    }
                    fclose(f);
                }
                if (k == 0)
                    break;
            }
            if (dp)
                closedir(dp);
        }
    }
#undef PUSH
    qsort(names, n, sizeof *names, (int (*)(const void *, const void *))strcmp);
    for (size_t i = 0; i < n; i++) {
        /* a name with a slash would be taken for a charmap */
        if ((i > 0 && strcmp(names[i], names[i - 1]) == 0) || strchr(names[i], '/'))
            continue;
        iconv_t cd = iconv_open("UTF-8", names[i]);
        if (cd == (iconv_t)-1)
            continue;
        iconv_close(cd);
        printf("%s\n", names[i]);
    }
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    const char *from = NULL, *to = NULL;
    bool opt_l = false;
    int c;
    while ((c = getopt(argc, argv, "csf:t:l")) != -1) {
        switch (c) {
        case 'c': opt_c = true; break;
        case 's': opt_s = true; break;
        case 'f': from = optarg; break;
        case 't': to = optarg; break;
        case 'l': opt_l = true; break;
        default: usage();
        }
    }
    if (opt_l) {
        if (from || to || opt_c || opt_s || optind < argc)
            usage();
        list();
        return fflush(stdout) == 0 ? 0 : 1;
    }
    const char *loc = nl_langinfo(CODESET);
    bool fmap = from && strchr(from, '/'), tmap = to && strchr(to, '/');
    if (fmap || tmap) {
        if (!fmap || !tmap) {
            fprintf(stderr, "iconv: a charmap can only be joined with another charmap\n");
            return 1;
        }
        read_charmap(from, &from_map);
        read_charmap(to, &to_map);
        qsort(from_map.e, from_map.n, sizeof *from_map.e, cmp_bytes);
        for (size_t i = 0; i < from_map.n; i++)
            if (from_map.e[i].len > maxlen)
                maxlen = from_map.e[i].len;
        qsort(to_map.e, to_map.n, sizeof *to_map.e, cmp_name);
    } else {
        if (!from)
            from = loc;
        if (!to)
            to = loc;
        cd1 = iconv_open("UTF-32BE", from);
        cd2 = iconv_open(to, "UTF-32BE");
        two = cd1 != (iconv_t)-1 && cd2 != (iconv_t)-1;
        if (!two) {
            if (cd1 != (iconv_t)-1)
                iconv_close(cd1);
            if (cd2 != (iconv_t)-1)
                iconv_close(cd2);
            cd1 = iconv_open(to, from);
            if (cd1 == (iconv_t)-1) {
                fprintf(stderr, "iconv: conversion from %s to %s: %s\n", from, to,
                        errno == EINVAL ? "not supported" : strerror(errno));
                return 1;
            }
        }
    }

    if (optind == argc)
        argv[--optind] = "-";
    for (int i = optind; i < argc; i++) {
        int fd = 0;
        fname = argv[i];
        if (strcmp(argv[i], "-") == 0) {
            fname = "standard input";
        } else if ((fd = open(argv[i], O_RDONLY)) < 0) {
            fprintf(stderr, "iconv: %s: %s\n", argv[i], strerror(errno));
            status = 1;
            continue;
        }
        do_fd(fd);
        if (fd != 0)
            close(fd);
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "iconv: standard output: %s\n", strerror(errno));
        return 1;
    }
    return status;
}
