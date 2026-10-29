/*
 * tr.c - tr: translate characters (POSIX.1-2024, XCU tr)
 *
 * Synopsis: tr [-c|-C] [-s] string1 string2
 *           tr -s [-c|-C] string1
 *           tr -d [-c|-C] string1
 *           tr -ds [-c|-C] string1 string2
 *
 * Characters are those of the locale (LC_CTYPE), multibyte ones
 * included.  A byte of the input that does not begin a valid character
 * is a value of its own (it can be named with \octal), and is copied
 * as it is unless the strings select it.  \octal escapes in a row that
 * together encode a multibyte character name that character.
 *
 * The arrays are built as the standard describes: ranges (c-c) in
 * code-point order (that of the POSIX locale; with \octal endpoints,
 * of byte values), [:class:] for every class of the locale (wctype()),
 * [=equiv=], and [x*n] in string2 only (n with a leading zero is octal;
 * omitted or zero, it fills string2 to the length of string1).
 * [:lower:] and [:upper:] in string2 are valid at the position of the
 * opposite class in string1, and request case conversion.
 *
 * Unspecified cases, as done here:
 *   - a string2 shorter than string1 is padded with its last character
 *     (the BSD behaviour);
 *   - a character given twice in string1 takes the last mapping;
 *   - a decreasing range (z-a) is an error;
 *   - with -c the complement is ordered by code point (for UTF-8 that is
 *     the order of the encodings), the input bytes that are not
 *     characters after the characters; with -C, in a multibyte locale,
 *     by wcscoll().  Only the first elements, those string2 gives
 *     distinct characters, need ordering: the rest map to the last (or
 *     the [x*]) character of string2.
 *   - [=c=] holds c and the characters of the same primary collation
 *     weight, where the C library exposes it in wcsxfrm() (glibc);
 *     otherwise just c.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

/*
 * A value: a character of the locale (>= 0), or a byte that is not one
 * (RAW(b), < 0).  In a single-byte locale every value is a byte, 0..255.
 */
typedef int32_t Val;
#define RAW(b)    (-1 - (Val)(b))
#define IS_RAW(v) ((v) < 0)
#define RAW_BYTE(v) ((unsigned char)(-1 - (v)))

#define WMAX 0x110000              /* characters tabled: code points */
static bool mb;                    /* a multibyte locale */
static size_t nvals;               /* table size */

static size_t vidx(Val v)
{
    if (!mb)
        return (size_t)v;
    if (IS_RAW(v))
        return WMAX + RAW_BYTE(v);
    return (size_t)v < WMAX ? (size_t)v : (size_t)-1;
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        perror("tr");
        exit(1);
    }
    return p;
}
static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        perror("tr");
        exit(1);
    }
    return p;
}
static void *xcalloc(size_t n, size_t s)
{
    void *p = calloc(n ? n : 1, s);
    if (!p) {
        perror("tr");
        exit(1);
    }
    return p;
}

static void die(const char *fmt, const char *arg)
{
    fputs("tr: ", stderr);
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    exit(1);
}

/* ---- arrays ---- */

typedef struct {
    Val *v;
    size_t n, cap;
    /* [x*] / [x*0] in string2: fill with fill_val at position fill_pos */
    bool has_fill;
    size_t fill_pos;
    Val fill_val;
} Array;

static void push(Array *a, Val v)
{
    if (a->n == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 64;
        a->v = xrealloc(a->v, a->cap * sizeof *a->v);
    }
    a->v[a->n++] = v;
}

/* ---- the items of an operand ---- */

enum { I_VAL, I_RANGE, I_CLASS, I_EQUIV, I_REPEAT };

typedef struct {
    int kind;
    Val lo, hi;         /* I_VAL: lo; I_RANGE: lo-hi; I_REPEAT: lo */
    bool bytes;         /* I_RANGE of byte values (an \octal endpoint) */
    wctype_t class;
    char name[32];      /* I_CLASS */
    unsigned long count;    /* I_REPEAT: 0 = fill */
} Item;

typedef struct {
    Item *it;
    size_t n;
} Items;

static void add_item(Items *L, Item it)
{
    L->it = xrealloc(L->it, (L->n + 1) * sizeof *L->it);
    L->it[L->n++] = it;
}

/* a byte value as a value: the character it is, if it is one */
static Val byte_val(unsigned char b)
{
    if (!mb)
        return b;
    if (b == 0)
        return 0;
    wchar_t w;
    mbstate_t st;
    memset(&st, 0, sizeof st);
    char c = (char)b;
    if (mbrtowc(&w, &c, 1, &st) == 1)
        return (Val)w;
    return RAW(b);
}

/* the value of the bytes p[0..n) as one character, or -1 if they are not exactly one */
static int decode(const char *p, size_t n, Val *out)
{
    if (!mb) {
        if (n != 1)
            return -1;
        *out = (unsigned char)*p;
        return 0;
    }
    wchar_t w;
    mbstate_t st;
    memset(&st, 0, sizeof st);
    size_t k = mbrtowc(&w, p, n, &st);
    if (k == 0 && n == 1) {
        *out = 0;
        return 0;
    }
    if (k != n)
        return -1;
    *out = (Val)w;
    return 0;
}

static bool is_odigit(char c) { return c >= '0' && c <= '7'; }

/* one \octal escape at s (s[0] == '\\', s[1] an octal digit) */
static unsigned char octal(const char **sp)
{
    const char *s = *sp + 1;
    unsigned v = 0;
    for (int d = 0; d < 3 && is_odigit(*s); d++)
        v = v * 8 + (unsigned)(*s++ - '0');
    *sp = s;
    return (unsigned char)v;
}

/*
 * The next character of an operand: an escape sequence or a character.
 * *was_octal is set for an \octal escape, which is a byte value when it
 * stands alone (*single), or a character with the escapes after it.
 */
static Val next_char(const char **sp, bool *was_octal, bool range_endpoint_ok)
{
    const char *s = *sp;
    *was_octal = false;
    if (*s == '\\' && is_odigit(s[1])) {
        unsigned char b = octal(&s);
        *was_octal = true;
        /* a lone byte value: before a '-', as a range endpoint */
        if (!mb || (range_endpoint_ok && *s == '-' && s[1])) {
            *sp = s;
            return byte_val(b);
        }
        /* concatenated escapes that encode one multibyte character */
        char buf[MB_LEN_MAX];
        size_t n = 0;
        const char *t = s;
        buf[n++] = (char)b;
        for (;;) {
            Val v;
            if (decode(buf, n, &v) == 0) {
                if (n > 1) {
                    *sp = t;
                    *was_octal = false;
                    return v;
                }
                break;
            }
            if (n == MB_LEN_MAX || t[0] != '\\' || !is_odigit(t[1]))
                break;
            buf[n++] = (char)octal(&t);
        }
        *sp = s;
        return byte_val(b);
    }
    if (*s == '\\' && s[1]) {
        s++;
        Val v;
        switch (*s) {
        case '\\': v = '\\'; break;
        case 'a': v = '\a'; break;
        case 'b': v = '\b'; break;
        case 'f': v = '\f'; break;
        case 'n': v = '\n'; break;
        case 'r': v = '\r'; break;
        case 't': v = '\t'; break;
        case 'v': v = '\v'; break;
        default:
            goto plain;         /* unspecified: the character itself */
        }
        *sp = s + 1;
        return v;
    }
plain:
    if (!mb) {
        *sp = s + 1;
        return (unsigned char)*s;
    }
    {
        wchar_t w;
        mbstate_t st;
        memset(&st, 0, sizeof st);
        size_t k = mbrtowc(&w, s, strlen(s), &st);
        if (k == (size_t)-1 || k == (size_t)-2 || k == 0) {
            *sp = s + 1;
            return RAW((unsigned char)*s);
        }
        *sp = s + k;
        return (Val)w;
    }
}

/* [x*n]: a repeat item at s, or false */
static bool parse_repeat(const char **sp, Item *it)
{
    const char *s = *sp + 1;
    bool oct;
    if (!*s)
        return false;
    Val x = next_char(&s, &oct, false);
    if (*s != '*')
        return false;
    s++;
    const char *digits = s;
    while (*s >= '0' && *s <= '9')
        s++;
    if (*s != ']')
        return false;
    unsigned long n = 0;
    int base = digits[0] == '0' ? 8 : 10;
    for (const char *d = digits; d < s; d++) {
        if (base == 8 && *d > '7')
            return false;
        unsigned long nn = n * (unsigned long)base + (unsigned long)(*d - '0');
        n = nn / (unsigned long)base == n ? nn : ULONG_MAX;
        if (n > (1UL << 30))
            die("repeat count too large in '%s'", *sp);
    }
    it->kind = I_REPEAT;
    it->lo = x;
    it->count = n;
    *sp = s + 1;
    return true;
}

static Items parse_operand(const char *op, bool is_string2)
{
    Items L = {0};
    const char *s = op;
    while (*s) {
        Item it;
        memset(&it, 0, sizeof it);
        if (s[0] == '[' && (s[1] == ':' || s[1] == '=')) {
            char delim = s[1];
            const char *end = s + 2;
            /* [=x=]: x is one character; [:name:] */
            while (*end && !(end[0] == delim && end[1] == ']'))
                end++;
            if (*end && end > s + 2) {
                size_t len = (size_t)(end - (s + 2));
                if (delim == ':') {
                    if (len >= sizeof it.name)
                        die("invalid character class in '%s'", op);
                    memcpy(it.name, s + 2, len);
                    it.name[len] = 0;
                    it.class = wctype(it.name);
                    if (!it.class)
                        die("invalid character class '%s'", it.name);
                    it.kind = I_CLASS;
                } else {
                    char tmp[64];
                    if (len >= sizeof tmp)
                        die("invalid equivalence class in '%s'", op);
                    memcpy(tmp, s + 2, len);
                    tmp[len] = 0;
                    const char *t = tmp;
                    bool oct;
                    it.lo = next_char(&t, &oct, false);
                    if (*t)
                        die("invalid equivalence class in '%s'", op);
                    it.kind = I_EQUIV;
                }
                add_item(&L, it);
                s = end + 2;
                continue;
            }
        }
        if (s[0] == '[') {
            const char *t = s;
            if (parse_repeat(&t, &it)) {
                if (!is_string2)
                    die("[x*n] is only valid in string2: '%s'", op);
                add_item(&L, it);
                s = t;
                continue;
            }
        }
        bool oct_lo;
        Val lo = next_char(&s, &oct_lo, true);
        if (s[0] == '-' && s[1]) {
            const char *t = s + 1;
            bool oct_hi;
            Val hi = next_char(&t, &oct_hi, false);
            it.kind = I_RANGE;
            it.bytes = oct_lo || oct_hi;
            if (it.bytes) {
                /* the coded values between the endpoints */
                int blo = IS_RAW(lo) ? RAW_BYTE(lo) : (int)lo;
                int bhi = IS_RAW(hi) ? RAW_BYTE(hi) : (int)hi;
                if (blo > 255 || bhi > 255)
                    die("invalid range in '%s'", op);
                it.lo = blo;
                it.hi = bhi;
            } else {
                if (IS_RAW(lo) || IS_RAW(hi))
                    die("invalid range in '%s'", op);
                it.lo = lo;
                it.hi = hi;
            }
            if (it.hi < it.lo)
                die("range endpoints out of order in '%s'", op);
            add_item(&L, it);
            s = t;
            continue;
        }
        it.kind = I_VAL;
        it.lo = lo;
        add_item(&L, it);
    }
    return L;
}

/* ---- expanding items ---- */

static bool valid_char(Val w)
{
    return !(w >= 0xD800 && w <= 0xDFFF);
}

static void expand_class(Array *a, wctype_t cl)
{
    if (!mb) {
        for (int b = 0; b < 256; b++) {
            /* in a single-byte locale btowc() gives the wide character */
            wint_t w = btowc(b);
            if (w != WEOF && iswctype(w, cl))
                push(a, b);
        }
        return;
    }
    for (Val w = 0; w < WMAX; w++)
        if (valid_char(w) && iswctype((wint_t)w, cl))
            push(a, w);
}

/* the primary collation weight of a character, where wcsxfrm() shows it */
static size_t primary(Val v, wchar_t *out, size_t cap)
{
    wchar_t s[2] = { mb ? (wchar_t)v : (wchar_t)btowc((int)v), 0 };
    size_t n = wcsxfrm(out, s, cap);
    if (n >= cap)
        return 0;
    /* glibc separates the levels with L'\1' */
    for (size_t i = 0; i < n; i++)
        if (out[i] == 1)
            return i;
    return n;
}

static void expand_equiv(Array *a, Val x)
{
    wchar_t px[64], pw[64];
    size_t nx = IS_RAW(x) ? 0 : primary(x, px, 64);
    size_t start = a->n;
    if (nx && !(mb ? false : btowc((int)x) == WEOF)) {
        Val end = mb ? WMAX : 256;
        for (Val w = 0; w < end; w++) {
            if (mb && !valid_char(w))
                continue;
            if (!mb && btowc((int)w) == WEOF)
                continue;
            size_t nw = primary(w, pw, 64);
            if (nw == nx && wmemcmp(px, pw, nx) == 0)
                push(a, w);
        }
    }
    /* nothing sensible found: the character itself */
    bool has_x = false;
    for (size_t i = start; i < a->n; i++)
        has_x |= a->v[i] == x;
    if (!has_x || a->n - start > 256) {
        a->n = start;
        push(a, x);
    }
}

static void expand(Array *a, const Item *it, const char *op)
{
    switch (it->kind) {
    case I_VAL:
        push(a, it->lo);
        break;
    case I_RANGE:
        for (Val v = it->lo; v <= it->hi; v++) {
            if (it->bytes)
                push(a, byte_val((unsigned char)v));
            else if (!mb || valid_char(v))
                push(a, v);
        }
        break;
    case I_CLASS:
        expand_class(a, it->class);
        break;
    case I_EQUIV:
        expand_equiv(a, it->lo);
        break;
    case I_REPEAT:
        if (it->count == 0) {
            if (a->has_fill)
                die("only one [x*] may be given in '%s'", op);
            a->has_fill = true;
            a->fill_pos = a->n;
            a->fill_val = it->lo;
        } else {
            for (unsigned long i = 0; i < it->count; i++)
                push(a, it->lo);
        }
        break;
    }
}

static bool is_case(const Item *it, const char *name)
{
    return it->kind == I_CLASS && strcmp(it->name, name) == 0;
}

/*
 * Build both arrays for translation: case classes at the same position
 * become the pairs of the toupper or tolower mapping.
 */
static void build_pair(const Items *A, const Items *B, Array *a, Array *b,
                       const char *op1, const char *op2, bool pairing)
{
    size_t i = 0, j = 0;
    while (i < A->n || j < B->n) {
        const Item *x = i < A->n ? &A->it[i] : NULL;
        const Item *y = j < B->n ? &B->it[j] : NULL;
        if (y && y->kind == I_REPEAT && y->count == 0)
            pairing = false;    /* positions after a fill are not known yet */
        if (pairing && x && y && a->n == b->n &&
            ((is_case(x, "lower") && is_case(y, "upper")) ||
             (is_case(x, "upper") && is_case(y, "lower"))))
        {
            bool up = is_case(x, "lower");
            Val end = mb ? WMAX : 256;
            for (Val c = 0; c < end; c++) {
                wint_t w = mb ? (wint_t)c : btowc((int)c);
                if (w == WEOF || (mb && !valid_char(c)))
                    continue;
                if (up ? !iswlower(w) : !iswupper(w))
                    continue;
                wint_t m = up ? towupper(w) : towlower(w);
                if (m == w)
                    continue;
                push(a, c);
                push(b, mb ? (Val)m : (Val)wctob(m));
            }
            i++;
            j++;
            continue;
        }
        bool take_b;
        if (!y)
            take_b = false;
        else if (!x)
            take_b = true;
        else if (!pairing)
            take_b = false;         /* all of string1 first */
        else if (b->n != a->n)
            take_b = b->n < a->n;
        else
            take_b = is_case(x, "lower") || is_case(x, "upper");
        if (take_b) {
            if (y->kind == I_CLASS && (is_case(y, "lower") || is_case(y, "upper")))
                die("[:%s:] in string2 needs the opposite class at the same position in string1", y->name);
            if (y->kind == I_CLASS)
                die("character class [:%s:] is not valid in string2", y->name);
            if (y->kind == I_EQUIV)
                die("an equivalence class is not valid in string2: '%s'", op2);
            expand(b, y, op2);
            j++;
        } else {
            expand(a, x, op1);
            i++;
        }
    }
}

static Array build(const Items *L, const char *op)
{
    Array a = {0};
    for (size_t i = 0; i < L->n; i++)
        expand(&a, &L->it[i], op);
    return a;
}

/* ---- complements ---- */

/* the key of a value for -c: code point order, bytes that are not characters last */
static int cmp_coll(const void *p, const void *q)
{
    Val a = *(const Val *)p, b = *(const Val *)q;
    if (IS_RAW(a) || IS_RAW(b)) {
        if (IS_RAW(a) && IS_RAW(b))
            return RAW_BYTE(a) - RAW_BYTE(b);
        return IS_RAW(a) ? 1 : -1;
    }
    wchar_t x[2] = { (wchar_t)a, 0 }, y[2] = { (wchar_t)b, 0 };
    int r = wcscoll(x, y);
    return r ? r : (a > b) - (a < b);
}
static int cmp_coll_sb(const void *p, const void *q)
{
    Val a = *(const Val *)p, b = *(const Val *)q;
    char x[2] = { (char)a, 0 }, y[2] = { (char)b, 0 };
    int r;
    if (a == 0 || b == 0)
        r = (a > b) - (a < b);
    else
        r = strcoll(x, y);
    return r ? r : (a > b) - (a < b);
}

/*
 * The first k elements of the complement of the values marked in in1:
 * in binary order (-c) or collation order (-C).
 */
static Val *complement_prefix(const unsigned char *in1, size_t k, bool collate, size_t *got)
{
    Val *all = NULL;
    size_t n = 0;
    if (k == 0) {
        *got = 0;
        return NULL;
    }
    if (!collate) {
        all = xmalloc(k * sizeof *all);
        Val end = mb ? WMAX : 256;
        for (Val v = 0; v < end && n < k; v++)
            if ((!mb || valid_char(v)) && !in1[vidx(v)])
                all[n++] = v;
        for (int b = 0x80; mb && b < 256 && n < k; b++)
            if (!in1[vidx(RAW(b))])
                all[n++] = RAW(b);
        *got = n;
        return all;
    }
    size_t cap = mb ? WMAX + 128 : 256;
    all = xmalloc(cap * sizeof *all);
    if (mb) {
        for (Val v = 0; v < WMAX; v++)
            if (valid_char(v) && !in1[vidx(v)])
                all[n++] = v;
        for (int b = 0x80; b < 256; b++)
            if (!in1[vidx(RAW(b))])
                all[n++] = RAW(b);
    } else {
        for (Val v = 0; v < 256; v++)
            if (!in1[v])
                all[n++] = v;
    }
    qsort(all, n, sizeof *all, mb ? cmp_coll : cmp_coll_sb);
    *got = n < k ? n : k;
    return all;
}

/* ---- the input ---- */

enum { F_S1 = 1, F_SQ = 2, F_MAP = 4 };

static unsigned char *flag;     /* per value */
static Val *map;                /* per value, where F_MAP */
static bool opt_c, opt_C, opt_d, opt_s, complement;
static Val cmap_default;
static bool squeeze_s1;        /* -s with string1 only: squeeze its array */        /* complement translation: values past the prefix */

static unsigned char obuf[65536];
static size_t olen;
static void oflush(void)
{
    if (olen && fwrite(obuf, 1, olen, stdout) != olen) {
        perror("tr: write error");
        exit(1);
    }
    olen = 0;
}
static void oput(const char *p, size_t n)
{
    if (olen + n > sizeof obuf)
        oflush();
    memcpy(obuf + olen, p, n);
    olen += n;
}
static void oval(Val v)
{
    if (!mb) {
        char c = (char)v;
        oput(&c, 1);
    } else if (IS_RAW(v)) {
        char c = (char)RAW_BYTE(v);
        oput(&c, 1);
    } else {
        char buf[MB_LEN_MAX];
        mbstate_t st;
        memset(&st, 0, sizeof st);
        size_t k = wcrtomb(buf, (wchar_t)v, &st);
        if (k == (size_t)-1)
            return;
        oput(buf, k);
    }
}

static bool selected(Val v)
{
    size_t i = vidx(v);
    bool in = i != (size_t)-1 && (flag[i] & F_S1);
    return complement ? !in : in;
}

static Val last_out;
static bool have_last;

static void process(Val v, const char *bytes, size_t n)
{
    size_t i = vidx(v);
    bool sel = selected(v);
    if (opt_d && sel)
        return;
    Val o = v;
    bool changed = false;
    if (!opt_d && sel && (complement || (i != (size_t)-1 && (flag[i] & F_MAP)))) {
        if (complement) {
            o = i != (size_t)-1 && (flag[i] & F_MAP) ? map[i] : cmap_default;
        } else {
            o = map[i];
        }
        changed = o != v;
    }
    if (opt_s) {
        size_t oi = vidx(o);
        bool sq;
        if (squeeze_s1)
            sq = selected(o);       /* -s alone: the last operand is string1 */
        else
            sq = oi != (size_t)-1 && (flag[oi] & F_SQ);
        if (sq && have_last && last_out == o)
            return;
    }
    last_out = o;
    have_last = true;
    if (changed)
        oval(o);
    else
        oput(bytes, n);
}

static void run(void)
{
    static char buf[65536 + MB_LEN_MAX];
    size_t have = 0;
    bool eof = false;
    while (!eof || have) {
        if (!eof) {
            size_t n = fread(buf + have, 1, sizeof buf - have, stdin);
            if (n == 0) {
                if (ferror(stdin)) {
                    perror("tr: read error");
                    oflush();
                    exit(1);
                }
                eof = true;
            }
            have += n;
        }
        size_t i = 0;
        while (i < have) {
            if (!mb) {
                process((unsigned char)buf[i], buf + i, 1);
                i++;
                continue;
            }
            unsigned char c = (unsigned char)buf[i];
            if (c < 0x80 && c != 0) {
                /* ASCII: itself in every locale in practice (checked in main) */
                process(c, buf + i, 1);
                i++;
                continue;
            }
            wchar_t w;
            mbstate_t st;
            memset(&st, 0, sizeof st);
            size_t k = mbrtowc(&w, buf + i, have - i, &st);
            if (k == (size_t)-2 && !eof)
                break;              /* the rest of it is still to be read */
            if (k == 0) {
                process(0, buf + i, 1);
                i++;
            } else if (k == (size_t)-1 || k == (size_t)-2) {
                process(RAW(c), buf + i, 1);
                i++;
            } else {
                process((Val)w, buf + i, k);
                i += k;
            }
        }
        memmove(buf, buf + i, have - i);
        have -= i;
    }
}

static void usage(void)
{
    fputs("usage: tr [-c|-C] [-s] string1 string2\n"
          "       tr -s [-c|-C] string1\n"
          "       tr -d [-c|-C] string1\n"
          "       tr -ds [-c|-C] string1 string2\n", stderr);
    exit(1);
}

int main(int argc, char *argv[])
{
    int opt;
    setlocale(LC_ALL, "");
    mb = MB_CUR_MAX > 1;
    if (mb) {
        /* the ASCII fast path of run() needs ASCII to be itself */
        for (int c = 1; c < 0x80; c++)
            if (btowc(c) != (wint_t)c) {
                fputs("tr: unsupported locale encoding\n", stderr);
                return 1;
            }
    }
    nvals = mb ? WMAX + 256 : 256;

    while ((opt = getopt(argc, argv, "cCds")) != -1) {
        switch (opt) {
        case 'c': opt_c = true; break;
        case 'C': opt_C = true; break;
        case 'd': opt_d = true; break;
        case 's': opt_s = true; break;
        default: usage();
        }
    }
    argc -= optind;
    argv += optind;
    complement = opt_c || opt_C;

    bool translate = !opt_d && argc >= 2;
    if (argc < 1 || argc > 2)
        usage();
    if (opt_d && opt_s && argc != 2) {
        fputs("tr: -d and -s need both string1 and string2\n", stderr);
        usage();
    }
    if (!opt_d && !opt_s && argc != 2)
        usage();
    if (opt_c && opt_C)
        usage();

    flag = xcalloc(nvals, 1);
    Items I1 = parse_operand(argv[0], false);
    Array a1, a2 = {0};

    if (translate) {
        Items I2 = parse_operand(argv[1], true);
        a1 = (Array){0};
        build_pair(&I1, &I2, &a1, &a2, argv[0], argv[1], !complement);
        if (a2.n == 0 && !a2.has_fill)
            die("string2 must not be empty when translating: '%s'", argv[1]);
        if (a1.has_fill)
            die("[x*n] is only valid in string2: '%s'", argv[0]);
    } else {
        a1 = build(&I1, argv[0]);
        if (opt_d && opt_s) {
            Items I2 = parse_operand(argv[1], true);
            a2 = build(&I2, argv[1]);
            if (a2.has_fill)
                push(&a2, a2.fill_val);
        }
    }
    for (size_t i = 0; i < a1.n; i++) {
        size_t x = vidx(a1.v[i]);
        if (x != (size_t)-1)
            flag[x] |= F_S1;
    }

    if (translate) {
        map = xcalloc(nvals, sizeof *map);
        if (!complement) {
            /* string2 filled to the length of string1, or padded */
            size_t fill = 0;
            if (a2.has_fill && a1.n > a2.n)
                fill = a1.n - a2.n;
            for (size_t i = 0; i < a1.n; i++) {
                Val to;
                if (a2.has_fill && i >= a2.fill_pos)
                    to = i < a2.fill_pos + fill ? a2.fill_val
                       : a2.v[i - fill];
                else if (i < a2.n)
                    to = a2.v[i];
                else
                    to = a2.has_fill ? a2.fill_val : a2.v[a2.n - 1];
                size_t x = vidx(a1.v[i]);
                if (x == (size_t)-1)
                    continue;
                flag[x] |= F_MAP;
                map[x] = to;
            }
        } else {
            /* the complement: only its first elements map to distinct ones */
            size_t k = a2.has_fill ? a2.fill_pos : a2.n;
            size_t got;
            Val *pre = complement_prefix(flag, k, opt_C && true, &got);
            for (size_t i = 0; i < got; i++) {
                size_t x = vidx(pre[i]);
                flag[x] |= F_MAP;
                map[x] = a2.v[i];
            }
            free(pre);
            cmap_default = a2.has_fill ? a2.fill_val : a2.v[a2.n - 1];
        }
    }

    if (opt_s) {
        if (translate || opt_d) {
            /* the last operand: string2 as it maps */
            for (size_t i = 0; i < a2.n; i++) {
                size_t x = vidx(a2.v[i]);
                if (x != (size_t)-1)
                    flag[x] |= F_SQ;
            }
            if (a2.has_fill) {
                size_t x = vidx(a2.fill_val);
                if (x != (size_t)-1)
                    flag[x] |= F_SQ;
            }
        } else {
            squeeze_s1 = true;
        }
    }

    run();
    oflush();
    if (fflush(stdout) == EOF || ferror(stdout)) {
        perror("tr: write error");
        return 1;
    }
    return 0;
}
