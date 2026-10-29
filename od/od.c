/*
 * od.c - od: dump files in various formats (POSIX.1-2024, XCU od)
 *
 * Synopsis:
 *   od [-v] [-A address_base] [-j skip] [-N count] [-t type_string]...
 *      [file...]
 *   od [-bcdosx] [file] [[+]offset[.][b]]                        (XSI)
 *
 * The input is formatted in blocks of 16 bytes, one line per output type.
 * The layout is that of GNU od: each type's fields are right-aligned, and
 * when types of different widths are mixed, the fields of the narrower
 * ones are padded so that the columns line up.
 *
 * Choices where the standard leaves room:
 *  - The offset is written in octal unless -A says otherwise, with at
 *    least 7 digits (6 in hexadecimal); with -A n no final offset line is
 *    written.
 *  - Byte order: that of this machine.  Without a size, d o u x take the
 *    size of int and f that of double; C S I L are the sizes of char,
 *    short, int and long; F D L those of float, double and long double.
 *  - Floating values are written in the shortest form that reads back as
 *    the same value (%g with as few digits as that takes).
 *  - -c in a locale with multibyte characters: a printable character is
 *    written in the area of its first byte, with ** for the others; a
 *    character cut by -j, -N or the end of the input, or bytes that are
 *    not a valid character, are written in octal.  Printable characters
 *    are those isprint() (iswprint()) accepts.
 *  - Named characters (-t a): \012 is written as nl.
 *  - -j and -N take decimal, octal (leading 0) and hexadecimal (0x)
 *    numbers; -j also the multipliers b, k and m.  The offset operand
 *    (XSI) is octal, decimal with a '.', and takes a 'b' (512 bytes).
 *  - A file that cannot be opened is reported, the others are dumped, and
 *    the exit status is 1.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

enum { BLOCK = 16 };

struct type {
    char kind;                  /* a c d f o u x */
    int size;                   /* bytes per item */
    int width;                  /* the width of a field, without padding */
    int pad;                    /* the padding spread over a block's fields */
};

static struct type *types;
static size_t ntypes;
static int status;

static void *xrealloc(void *p, size_t n)
{
    if (!(p = realloc(p, n))) {
        fprintf(stderr, "od: out of memory\n");
        exit(1);
    }
    return p;
}

static void usage(void)
{
    fprintf(stderr, "usage: od [-v] [-A address_base] [-j skip] [-N count] [-t type_string]... [file...]\n"
                    "       od [-bcdosx] [file] [[+]offset[.][b]]\n");
    exit(2);
}

/* ---- the output types ---- */

static int int_width(char kind, int size)
{
    static const int o[] = { 0, 3, 6, 0, 11, 0, 0, 0, 22 };
    static const int u[] = { 0, 3, 5, 0, 10, 0, 0, 0, 20 };
    static const int d[] = { 0, 4, 6, 0, 11, 0, 0, 0, 20 };
    switch (kind) {
    case 'o': return o[size];
    case 'u': return u[size];
    case 'd': return d[size];
    case 'x': return 2 * size;
    }
    return 3;
}

static void add_type(char kind, int size)
{
    types = xrealloc(types, (ntypes + 1) * sizeof *types);
    struct type *t = &types[ntypes++];
    t->kind = kind;
    t->size = size;
    if (kind == 'f')
        t->width = size == 4 ? 15 : size == 8 ? 24 : 29;
    else
        t->width = int_width(kind, size);
    t->pad = 0;
}

static void type_string(const char *s)
{
    const char *p = s;
    if (!*p) {
        fprintf(stderr, "od: an empty type string\n");
        exit(2);
    }
    while (*p) {
        char k = *p++;
        int size;
        switch (k) {
        case 'a': case 'c':
            add_type(k, 1);
            continue;
        case 'd': case 'o': case 'u': case 'x':
            size = (int)sizeof(int);
            if (*p == 'C') size = 1, p++;
            else if (*p == 'S') size = (int)sizeof(short), p++;
            else if (*p == 'I') size = (int)sizeof(int), p++;
            else if (*p == 'L') size = (int)sizeof(long), p++;
            else if (isdigit((unsigned char)*p)) {
                size = 0;
                while (isdigit((unsigned char)*p) && size < 100)
                    size = size * 10 + (*p++ - '0');
                if (size != 1 && size != 2 && size != 4 && size != 8) {
                    fprintf(stderr, "od: %s: no integer type of %d bytes\n", s, size);
                    exit(2);
                }
            }
            add_type(k, size);
            continue;
        case 'f':
            size = (int)sizeof(double);
            if (*p == 'F') size = (int)sizeof(float), p++;
            else if (*p == 'D') size = (int)sizeof(double), p++;
            else if (*p == 'L') size = (int)sizeof(long double), p++;
            else if (isdigit((unsigned char)*p)) {
                size = 0;
                while (isdigit((unsigned char)*p) && size < 100)
                    size = size * 10 + (*p++ - '0');
                if (size != (int)sizeof(float) && size != (int)sizeof(double) &&
                    size != (int)sizeof(long double)) {
                    fprintf(stderr, "od: %s: no floating type of %d bytes\n", s, size);
                    exit(2);
                }
            }
            add_type(k, size);
            continue;
        default:
            fprintf(stderr, "od: %s: '%c' is not a type\n", s, k);
            exit(2);
        }
    }
}

/* the padding of each type, so that the blocks of all types are as wide */
static void layout(void)
{
    int widest = 0;
    for (size_t i = 0; i < ntypes; i++) {
        int w = (types[i].width + 1) * (BLOCK / types[i].size);
        if (w > widest)
            widest = w;
    }
    for (size_t i = 0; i < ntypes; i++)
        types[i].pad = widest - types[i].width * (BLOCK / types[i].size);
}

/* ---- formatting ---- */

static void put(const char *s, int width)
{
    if (printf("%*s", width, s) < 0)
        status = 1;
}

static uintmax_t get_unsigned(const unsigned char *p, int size)
{
    switch (size) {
    case 1: return *p;
    case 2: { uint16_t v; memcpy(&v, p, 2); return v; }
    case 4: { uint32_t v; memcpy(&v, p, 4); return v; }
    default: { uint64_t v; memcpy(&v, p, 8); return v; }
    }
}

static intmax_t get_signed(const unsigned char *p, int size)
{
    switch (size) {
    case 1: return (int8_t)*p;
    case 2: { int16_t v; memcpy(&v, p, 2); return v; }
    case 4: { int32_t v; memcpy(&v, p, 4); return v; }
    default: { int64_t v; memcpy(&v, p, 8); return v; }
    }
}

/* the shortest %g form of a floating value that reads back as it */
static void float_text(char *buf, size_t n, const unsigned char *p, int size)
{
    if (size == (int)sizeof(float)) {
        float v;
        memcpy(&v, p, sizeof v);
        for (int prec = 1; prec <= FLT_DECIMAL_DIG; prec++) {
            snprintf(buf, n, "%.*g", prec, (double)v);
            if (v != v || strtof(buf, NULL) == v)
                return;
        }
    } else if (size == (int)sizeof(double)) {
        double v;
        memcpy(&v, p, sizeof v);
        for (int prec = 1; prec <= DBL_DECIMAL_DIG; prec++) {
            snprintf(buf, n, "%.*g", prec, v);
            if (v != v || strtod(buf, NULL) == v)
                return;
        }
    } else {
        long double v;
        memcpy(&v, p, sizeof v);
        for (int prec = 1; prec <= LDBL_DECIMAL_DIG; prec++) {
            snprintf(buf, n, "%.*Lg", prec, v);
            if (v != v || strtold(buf, NULL) == v)
                return;
        }
    }
}

static const char *named[] = {
    "nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel", "bs", "ht", "nl", "vt", "ff", "cr",
    "so", "si", "dle", "dc1", "dc2", "dc3", "dc4", "nak", "syn", "etb", "can", "em", "sub",
    "esc", "fs", "gs", "rs", "us", "sp"
};

static bool multibyte;          /* the locale has characters of more than one byte */

/* -c: the text for the byte at p (n bytes of input from there on); the
   number of bytes the character takes */
static int char_text(char *buf, const unsigned char *p, size_t n)
{
    unsigned char c = *p;
    switch (c) {
    case '\0': strcpy(buf, "\\0"); return 1;
    case '\a': strcpy(buf, "\\a"); return 1;
    case '\b': strcpy(buf, "\\b"); return 1;
    case '\f': strcpy(buf, "\\f"); return 1;
    case '\n': strcpy(buf, "\\n"); return 1;
    case '\r': strcpy(buf, "\\r"); return 1;
    case '\t': strcpy(buf, "\\t"); return 1;
    case '\v': strcpy(buf, "\\v"); return 1;
    }
    if (multibyte && c >= 0x80) {
        wchar_t wc;
        mbstate_t st;
        memset(&st, 0, sizeof st);
        size_t k = mbrtowc(&wc, (const char *)p, n, &st);
        if (k != (size_t)-1 && k != (size_t)-2 && k > 0 && iswprint((wint_t)wc)) {
            memcpy(buf, p, k);
            buf[k] = '\0';
            return (int)k;
        }
    } else if (c < 0x80 && isprint(c)) {
        buf[0] = (char)c;
        buf[1] = '\0';
        return 1;
    } else if (!multibyte && isprint(c)) {
        buf[0] = (char)c;
        buf[1] = '\0';
        return 1;
    }
    snprintf(buf, 8, "%03o", c);
    return 1;
}

/* write one block (len bytes of data, len <= BLOCK, the rest zero) as type t;
   avail: the bytes of input from here on that are in data, for -c */
static void write_type(const struct type *t, const unsigned char *data, size_t len,
                       size_t avail, int *cont_state)
{
    int fields = BLOCK / t->size;
    int used = (int)((len + (size_t)t->size - 1) / (size_t)t->size);
    int pad_left = t->pad;
    int cont = *cont_state;
    for (int i = fields, f = 0; i > fields - used; i--, f++) {
        int next_pad = t->pad * (i - 1) / fields;
        int w = pad_left - next_pad + t->width;
        pad_left = next_pad;
        const unsigned char *p = data + (size_t)f * (size_t)t->size;
        char buf[64];
        switch (t->kind) {
        case 'o':
            snprintf(buf, sizeof buf, "%0*jo", t->width, get_unsigned(p, t->size));
            break;
        case 'x':
            snprintf(buf, sizeof buf, "%0*jx", t->width, get_unsigned(p, t->size));
            break;
        case 'u':
            snprintf(buf, sizeof buf, "%ju", get_unsigned(p, t->size));
            break;
        case 'd':
            snprintf(buf, sizeof buf, "%jd", get_signed(p, t->size));
            break;
        case 'f':
            float_text(buf, sizeof buf, p, t->size);
            break;
        case 'a': {
            unsigned c = *p & 0x7f;
            if (c <= 0x20)
                strcpy(buf, named[c]);
            else if (c == 0x7f)
                strcpy(buf, "del");
            else
                buf[0] = (char)c, buf[1] = '\0';
            break;
        }
        case 'c':
            if (cont > 0) {
                strcpy(buf, "**");
                cont--;
            } else {
                size_t left = avail - (size_t)f;
                int k = char_text(buf, p, left);
                cont = k - 1;
                /* a character's other bytes are one column narrower than text */
                if (k > 1) {
                    int cols = 1;
                    wchar_t wc;
                    mbstate_t st;
                    memset(&st, 0, sizeof st);
                    if (mbrtowc(&wc, buf, (size_t)k, &st) == (size_t)k) {
                        int wd = wcwidth(wc);
                        cols = wd > 0 ? wd : 1;
                    }
                    w += k - cols;      /* printf counts bytes */
                }
            }
            break;
        }
        put(buf, w);
    }
    *cont_state = cont;
    if (putchar('\n') == EOF)
        status = 1;
}

/* ---- input ---- */

static char **files;
static int nfiles, curfile = -1;
static FILE *cur;
static const char *curname;

/* the next input file; false when there are no more */
static bool open_next(void)
{
    for (;;) {
        if (cur && cur != stdin)
            fclose(cur);
        cur = NULL;
        if (++curfile >= (nfiles ? nfiles : 1))
            return false;
        curname = nfiles ? files[curfile] : "-";
        if (strcmp(curname, "-") == 0) {
            cur = stdin;
            return true;
        }
        if ((cur = fopen(curname, "rb")))
            return true;
        fprintf(stderr, "od: %s: %s\n", curname, strerror(errno));
        status = 1;
    }
}

/* read up to n bytes of the concatenated input */
static size_t input(unsigned char *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        if (!cur && !open_next())
            break;
        size_t k = fread(buf + got, 1, n - got, cur);
        got += k;
        if (k == 0 || feof(cur) || ferror(cur)) {
            if (ferror(cur)) {
                fprintf(stderr, "od: %s: %s\n", curname, strerror(errno));
                status = 1;
            }
            if (cur != stdin)
                fclose(cur);
            else
                clearerr(stdin);
            cur = NULL;
            if (!open_next())
                break;
        }
    }
    return got;
}

/* skip n bytes; false if the input is shorter */
static bool skip(uintmax_t n)
{
    while (n > 0) {
        if (!cur && !open_next())
            return false;
        struct stat st;
        if (cur != stdin && fstat(fileno(cur), &st) == 0 && S_ISREG(st.st_mode)) {
            if ((uintmax_t)st.st_size <= n) {
                n -= (uintmax_t)st.st_size;
                fclose(cur);
                cur = NULL;
                if (!open_next())
                    return n == 0;
                continue;
            }
            if (fseeko(cur, (off_t)n, SEEK_SET) == 0)
                return true;
        }
        unsigned char buf[4096];
        size_t k = fread(buf, 1, n < sizeof buf ? (size_t)n : sizeof buf, cur);
        n -= k;
        if (k == 0) {
            if (cur != stdin)
                fclose(cur);
            cur = NULL;
            if (!open_next())
                return false;
        }
    }
    return true;
}

/* ---- numbers in options ---- */

static uintmax_t number(const char *s, bool multipliers, const char *what)
{
    const char *p = s;
    int base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        base = 16, p += 2;
    else if (p[0] == '0' && p[1])
        base = 8;
    if (!*p || !isxdigit((unsigned char)*p))
        goto bad;
    char *end;
    errno = 0;
    uintmax_t v = strtoumax(p, &end, base);
    if (errno == ERANGE)
        goto bad;
    if (*end && multipliers && base != 16 && !end[1]) {
        uintmax_t m = *end == 'b' ? 512 : *end == 'k' ? 1024 : *end == 'm' ? 1048576 : 0;
        if (!m || v > UINTMAX_MAX / m)
            goto bad;
        return v * m;
    }
    if (*end)
        goto bad;
    return v;
bad:
    fprintf(stderr, "od: %s: not a valid %s\n", s, what);
    exit(2);
}

/* the XSI offset operand: [+]offset[.][b]; false if it is not one */
static bool offset_operand(const char *s, uintmax_t *v)
{
    const char *p = s;
    if (*p == '+')
        p++;
    if (!isdigit((unsigned char)*p))
        return false;
    size_t n = strspn(p, "0123456789");
    int base = 8;
    const char *q = p + n;
    if (*q == '.') {
        base = 10;
        q++;
    }
    uintmax_t mult = 1;
    if (*q == 'b') {
        mult = 512;
        q++;
    }
    if (*q)
        return false;
    uintmax_t r = 0;
    for (size_t i = 0; i < n; i++) {
        int d = p[i] - '0';
        if (d >= base)
            return false;
        r = r * (uintmax_t)base + (uintmax_t)d;
    }
    *v = r * mult;
    return true;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    multibyte = MB_CUR_MAX > 1;
    char base = 'o';
    bool opt_v = false, posix_opts = false, have_N = false;
    uintmax_t skip_n = 0, count = 0;
    int c;
    while ((c = getopt(argc, argv, "A:bcdj:N:ost:vx")) != -1) {
        switch (c) {
        case 'A':
            if (strlen(optarg) != 1 || !strchr("doxn", optarg[0])) {
                fprintf(stderr, "od: -A %s: the base is d, o, x or n\n", optarg);
                return 2;
            }
            base = optarg[0];
            posix_opts = true;
            break;
        case 'j': skip_n = number(optarg, true, "skip"); posix_opts = true; break;
        case 'N': count = number(optarg, false, "count"); have_N = true; posix_opts = true; break;
        case 't': type_string(optarg); posix_opts = true; break;
        case 'v': opt_v = true; posix_opts = true; break;
        case 'b': add_type('o', 1); break;
        case 'c': add_type('c', 1); break;
        case 'd': add_type('u', 2); break;
        case 'o': add_type('o', 2); break;
        case 's': add_type('d', 2); break;
        case 'x': add_type('x', 2); break;
        default: usage();
        }
    }
    files = argv + optind;
    nfiles = argc - optind;
    /* the XSI offset operand */
    if (!posix_opts && nfiles >= 1 && nfiles <= 2) {
        const char *last = files[nfiles - 1];
        uintmax_t off;
        if ((last[0] == '+' || (nfiles == 2 && isdigit((unsigned char)last[0]))) &&
            offset_operand(last, &off)) {
            skip_n = off;
            nfiles--;
        }
    }
    if (ntypes == 0)
        add_type('o', 2);
    layout();

    if (skip_n && !skip(skip_n)) {
        fprintf(stderr, "od: cannot skip past the end of the input\n");
        return 1;
    }
    uintmax_t offset = skip_n;
    unsigned char block[BLOCK + 8], prev[BLOCK];
    bool have_prev = false, starred = false;
    int *cont = calloc(ntypes, sizeof *cont);
    /* a little more input than the block, for -c characters across blocks */
    unsigned char look[BLOCK + 8];
    size_t looklen = 0;
    bool done = false;
    for (;;) {
        size_t want = BLOCK + 8;
        if (have_N) {
            uintmax_t left = count - (offset - skip_n);
            if (left < want)
                want = (size_t)left;
        }
        if (looklen < want && !done) {
            size_t k = input(look + looklen, want - looklen);
            if (looklen + k < want)
                done = true;
            looklen += k;
        }
        if (looklen > want)
            looklen = want;         /* -N reached */
        size_t len = looklen < BLOCK ? looklen : BLOCK;
        if (len == 0)
            break;
        memset(block, 0, sizeof block);
        memcpy(block, look, looklen);
        if (!opt_v && have_prev && len == BLOCK && memcmp(block, prev, BLOCK) == 0) {
            if (!starred && puts("*") == EOF)
                status = 1;
            starred = true;
        } else {
            starred = false;
            for (size_t i = 0; i < ntypes; i++) {
                if (i == 0 && base != 'n') {
                    if (printf(base == 'o' ? "%07jo" : base == 'd' ? "%07ju" : "%06jx", offset) < 0)
                        status = 1;
                } else if (base != 'n') {
                    if (printf("%*s", base == 'x' ? 6 : 7, "") < 0)
                        status = 1;
                }
                write_type(&types[i], block, len, looklen, &cont[i]);
            }
        }
        memcpy(prev, block, BLOCK);
        have_prev = true;
        offset += len;
        memmove(look, look + len, looklen - len);
        looklen -= len;
        if (len < BLOCK)
            break;
    }
    if (base != 'n' && printf(base == 'o' ? "%07jo\n" : base == 'd' ? "%07ju\n" : "%06jx\n", offset) < 0)
        status = 1;
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "od: write error: %s\n", strerror(errno));
        return 1;
    }
    free(cont);
    return status;
}
