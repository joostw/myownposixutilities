/*
 * lib.c - strings, buffers, arrays, numeric conversion, printf-style
 * formatting and regular expression support.
 */
#include "awk.h"

const char *progname = "awk";
int radix_char = '.';
int mb_mode = 0;
int collate_c = 1;

/* ------------------------------------------------------------------ */
/* errors and memory                                                  */
/* ------------------------------------------------------------------ */

extern void error_context(void);   /* run.c: prints input position */

void fatal(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    error_context();
    exit(2);
}

void warning(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s: warning: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        fatal("out of memory");
    return p;
}

void *xcalloc(size_t n, size_t m)
{
    void *p = calloc(n ? n : 1, m ? m : 1);
    if (!p)
        fatal("out of memory");
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        fatal("out of memory");
    return p;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

/* ------------------------------------------------------------------ */
/* strings                                                            */
/* ------------------------------------------------------------------ */

static struct { Str h; char z[1]; } empty_store = { { (size_t)1 << 40, 0 }, { 0 } };
Str *str_empty = &empty_store.h;

Str *str_new(const char *s, size_t len)
{
    Str *r;
    if (len == 0)
        return str_ref(str_empty);
    r = xmalloc(sizeof(Str) + len + 1);
    r->ref = 1;
    r->len = len;
    if (s)
        memcpy(r->s, s, len);
    r->s[len] = 0;
    return r;
}

Str *str_cstr(const char *s)
{
    return str_new(s, strlen(s));
}

Str *str_static(const char *s)
{
    Str *r = str_new(s, strlen(s));
    r->ref = (size_t)1 << 40;
    return r;
}

void str_free_(Str *s)
{
    if (s != str_empty)
        free(s);
}

unsigned long str_hash(const char *s, size_t len)
{
    unsigned long h = 2166136261UL;
    while (len--) {
        h ^= (unsigned char)*s++;
        h *= 16777619UL;
    }
    return h;
}

/* ------------------------------------------------------------------ */
/* buffers                                                            */
/* ------------------------------------------------------------------ */

void buf_init(Buf *b)
{
    b->p = NULL;
    b->len = b->cap = 0;
}

void buf_grow(Buf *b, size_t extra)
{
    if (b->len + extra + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 64;
        while (nc < b->len + extra + 1)
            nc *= 2;
        b->p = xrealloc(b->p, nc);
        b->cap = nc;
    }
}

void buf_add(Buf *b, const char *s, size_t n)
{
    buf_grow(b, n);
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

void buf_addc(Buf *b, int c)
{
    buf_grow(b, 1);
    b->p[b->len++] = (char)c;
    b->p[b->len] = 0;
}

void buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    int n;
    char small[128];
    va_start(ap, fmt);
    n = vsnprintf(small, sizeof small, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof small) {
        buf_add(b, small, (size_t)n);
        return;
    }
    buf_grow(b, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

Str *buf_str(Buf *b)
{
    Str *s = str_new(b->p, b->len);
    b->len = 0;
    if (b->p)
        b->p[0] = 0;
    return s;
}

void buf_free(Buf *b)
{
    free(b->p);
    buf_init(b);
}

/* ------------------------------------------------------------------ */
/* arrays: chained hash table with insertion order list               */
/* ------------------------------------------------------------------ */

Array *array_new(void)
{
    Array *a = xcalloc(1, sizeof *a);
    return a;
}

static void elem_free(Elem *e)
{
    str_unref(e->key);
    val_free(&e->v);
    free(e);
}

void array_clear(Array *a)
{
    Elem *e = a->first, *n;
    while (e) {
        n = e->next;
        elem_free(e);
        e = n;
    }
    free(a->tab);
    a->tab = NULL;
    a->size = a->count = 0;
    a->first = a->last = NULL;
}

void array_free(Array *a)
{
    array_clear(a);
    free(a);
}

static void array_rehash(Array *a)
{
    size_t ns = a->size ? a->size * 4 : 16, i;
    Elem **nt = xcalloc(ns, sizeof *nt);
    Elem *e;
    for (e = a->first; e; e = e->next) {
        i = e->hash & (ns - 1);
        e->hnext = nt[i];
        nt[i] = e;
    }
    free(a->tab);
    a->tab = nt;
    a->size = ns;
    (void)i;
}

Val *array_get(Array *a, Str *key, int create)
{
    unsigned long h = str_hash(key->s, key->len);
    Elem *e;
    if (a->size) {
        for (e = a->tab[h & (a->size - 1)]; e; e = e->hnext)
            if (e->hash == h && e->key->len == key->len &&
                memcmp(e->key->s, key->s, key->len) == 0)
                return &e->v;
    }
    if (!create)
        return NULL;
    if (a->count + 1 > a->size)
        array_rehash(a);
    e = xmalloc(sizeof *e);
    e->key = str_ref(key);
    e->hash = h;
    e->v = val_uninit();
    e->hnext = a->tab[h & (a->size - 1)];
    a->tab[h & (a->size - 1)] = e;
    e->next = NULL;
    e->prev = a->last;
    if (a->last)
        a->last->next = e;
    else
        a->first = e;
    a->last = e;
    a->count++;
    return &e->v;
}

void array_delete(Array *a, Str *key)
{
    unsigned long h;
    Elem **pp, *e;
    if (!a->size)
        return;
    h = str_hash(key->s, key->len);
    for (pp = &a->tab[h & (a->size - 1)]; (e = *pp); pp = &e->hnext) {
        if (e->hash == h && e->key->len == key->len &&
            memcmp(e->key->s, key->s, key->len) == 0) {
            *pp = e->hnext;
            if (e->prev) e->prev->next = e->next; else a->first = e->next;
            if (e->next) e->next->prev = e->prev; else a->last = e->prev;
            a->count--;
            elem_free(e);
            return;
        }
    }
}

Str **array_keys(Array *a, size_t *n)
{
    Str **k = xmalloc((a->count + 1) * sizeof *k);
    size_t i = 0;
    Elem *e;
    for (e = a->first; e; e = e->next)
        k[i++] = str_ref(e->key);
    *n = i;
    return k;
}

void array_set_cstr(Array *a, const char *key, Val v)
{
    Str *k = str_cstr(key);
    Val *c = array_get(a, k, 1);
    val_free(c);
    *c = v;
    str_unref(k);
}

/* ------------------------------------------------------------------ */
/* numbers                                                            */
/* ------------------------------------------------------------------ */

/*
 * Length of the longest prefix of s that has the form
 *   [sign] digits [radix [digits]] [exponent]  or  [sign] radix digits [exponent]
 * Returns 0 if there is no such prefix.
 */
size_t scan_number(const char *s, size_t len, int radix, int allow_sign)
{
    size_t i = 0;
    int digits = 0;
    if (allow_sign && i < len && (s[i] == '+' || s[i] == '-'))
        i++;
    while (i < len && isdigit((unsigned char)s[i]))
        i++, digits++;
    if (i < len && s[i] == radix) {
        i++;
        while (i < len && isdigit((unsigned char)s[i]))
            i++, digits++;
    }
    if (!digits)
        return 0;
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        size_t j = i + 1;
        if (j < len && (s[j] == '+' || s[j] == '-'))
            j++;
        if (j < len && isdigit((unsigned char)s[j])) {
            while (j < len && isdigit((unsigned char)s[j]))
                j++;
            i = j;
        }
    }
    return i;
}

/* Convert a validated numeric text (radix given) using strtod. */
double conv_number(const char *s, size_t n, int radix)
{
    char small[64], *b = small;
    size_t i;
    double d;
    if (n + 1 > sizeof small)
        b = xmalloc(n + 1);
    for (i = 0; i < n; i++)
        b[i] = (s[i] == radix) ? (char)radix_char : s[i];
    b[n] = 0;
    d = strtod(b, NULL);
    if (b != small)
        free(b);
    return d;
}

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

double str2num(const char *s, size_t len)
{
    size_t i = 0, n;
    while (i < len && is_space((unsigned char)s[i]))
        i++;
    n = scan_number(s + i, len - i, radix_char, 1);
    if (!n)
        return 0.0;
    return conv_number(s + i, n, radix_char);
}

int looks_numeric(const char *s, size_t len, double *out)
{
    size_t i = 0, n;
    while (i < len && is_space((unsigned char)s[i]))
        i++;
    n = scan_number(s + i, len - i, radix_char, 1);
    if (!n)
        return 0;
    {
        size_t j = i + n;
        while (j < len && (s[j] == ' ' || s[j] == '\t' || s[j] == '\n'))
            j++;
        if (j != len)
            return 0;
    }
    *out = conv_number(s + i, n, radix_char);
    return 1;
}

Val val_strnum(Str *s)
{
    Val v;
    double d;
    v.own = 0;
    v.u.a = NULL;
    v.s = s;
    if (looks_numeric(s->s, s->len, &d)) {
        v.t = T_STRNUM;
        v.n = d;
    } else {
        v.t = T_STR;
        v.n = 0;
    }
    return v;
}

/* fmt is a single floating conversion with no '*': snprintf can do it directly */
static int simple_float_fmt(Str *fmt)
{
    const char *p = fmt->s;
    int pct = 0;
    for (; *p; p++) {
        if (*p == '%') {
            if (p[1] == '%') { p++; continue; }
            if (pct++)
                return 0;
            p++;
            while (*p && strchr("-+ #0", *p)) p++;
            while (isdigit((unsigned char)*p)) p++;
            if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
            if (!*p || !strchr("eEfFgGaA", *p))
                return 0;
        }
    }
    return pct == 1 && fmt->len == strlen(fmt->s);
}

Str *num2str(double d, Str *fmt)
{
    char buf[512];
    if (d >= -9.2e18 && d <= 9.2e18 && d == (double)(long long)d) {
        int n = snprintf(buf, sizeof buf, "%lld", (long long)d);
        return str_new(buf, (size_t)n);
    }
    if (isfinite(d) && d == floor(d)) {
        /* integral value: "%d" semantics regardless of magnitude */
        Buf b;
        Str *r;
        buf_init(&b);
        buf_printf(&b, "%.0f", d);
        r = buf_str(&b);
        buf_free(&b);
        return r;
    }
    if (simple_float_fmt(fmt)) {
        int n = snprintf(buf, sizeof buf, fmt->s, d);
        if (n >= 0 && (size_t)n < sizeof buf)
            return str_new(buf, (size_t)n);
    }
    {
        Buf b;
        Val v = val_num(d);
        Str *r;
        buf_init(&b);
        format_awk(&b, fmt->s, fmt->len, &v, 1);
        r = buf_str(&b);
        buf_free(&b);
        return r;
    }
}

/* ------------------------------------------------------------------ */
/* multibyte helpers                                                  */
/* ------------------------------------------------------------------ */

size_t mb_charlen(const char *s, size_t n)
{
    mbstate_t st;
    size_t r;
    if (!mb_mode || n == 0)
        return n ? 1 : 0;
    if ((unsigned char)*s < 0x80)
        return 1;
    memset(&st, 0, sizeof st);
    r = mbrtowc(NULL, s, n, &st);
    if (r == (size_t)-1 || r == (size_t)-2 || r == 0)
        return 1;
    return r;
}

size_t mb_strlen(const char *s, size_t n)
{
    size_t i = 0, c = 0;
    if (!mb_mode)
        return n;
    while (i < n) {
        i += mb_charlen(s + i, n - i);
        c++;
    }
    return c;
}

/* byte offset after skipping nchars characters (clamped to n) */
size_t mb_offset(const char *s, size_t n, size_t nchars)
{
    size_t i = 0;
    if (!mb_mode)
        return nchars < n ? nchars : n;
    while (i < n && nchars--)
        i += mb_charlen(s + i, n - i);
    return i;
}

/* ------------------------------------------------------------------ */
/* printf engine                                                      */
/* ------------------------------------------------------------------ */

static double fmt_num(Val *v)
{
    switch (v->t) {
    case T_NUM: case T_STRNUM: return v->n;
    case T_STR: return str2num(v->s->s, v->s->len);
    default: return 0;
    }
}

extern Str *val_tostr(Val *v);     /* run.c: CONVFMT conversion */

static void pad(Buf *b, size_t n)
{
    while (n--)
        buf_addc(b, ' ');
}

void format_awk(Buf *out, const char *f, size_t flen, Val *args, int nargs)
{
    size_t i = 0;
    int ai = 0;
    Val none = val_uninit();

    while (i < flen) {
        size_t st;
        char spec[80];
        size_t sl = 0;
        int width = -1, prec = -1, minus = 0, conv;
        char flags[8];
        int nflags = 0;

        if (f[i] != '%') {
            size_t j = i;
            while (j < flen && f[j] != '%')
                j++;
            buf_add(out, f + i, j - i);
            i = j;
            continue;
        }
        st = i++;
        if (i < flen && f[i] == '%') {
            buf_addc(out, '%');
            i++;
            continue;
        }
        while (i < flen && strchr("-+ #0", f[i]) && f[i]) {
            if (f[i] == '-') minus = 1;
            if (nflags < 6 && !memchr(flags, f[i], (size_t)nflags))
                flags[nflags++] = f[i];
            i++;
        }
        if (i < flen && f[i] == '*') {
            Val *a = ai < nargs ? &args[ai++] : &none;
            double w = fmt_num(a);
            i++;
            if (w < 0) {
                w = -w;
                if (!minus) { minus = 1; if (nflags < 6) flags[nflags++] = '-'; }
            }
            width = w > 100000000 ? 100000000 : (int)w;
        } else if (i < flen && isdigit((unsigned char)f[i])) {
            width = 0;
            while (i < flen && isdigit((unsigned char)f[i])) {
                if (width < 100000000) width = width * 10 + (f[i] - '0');
                i++;
            }
        }
        if (i < flen && f[i] == '.') {
            i++;
            prec = 0;
            if (i < flen && f[i] == '*') {
                Val *a = ai < nargs ? &args[ai++] : &none;
                double p = fmt_num(a);
                i++;
                prec = p < 0 ? -1 : (p > 100000000 ? 100000000 : (int)p);
            } else {
                while (i < flen && isdigit((unsigned char)f[i])) {
                    if (prec < 100000000) prec = prec * 10 + (f[i] - '0');
                    i++;
                }
            }
        }
        /* skip C length modifiers, which are meaningless here */
        while (i < flen && strchr("hlLqjzt", f[i]) && f[i])
            i++;
        if (i >= flen) {
            buf_add(out, f + st, flen - st);
            break;
        }
        conv = (unsigned char)f[i++];

        spec[sl++] = '%';
        memcpy(spec + sl, flags, (size_t)nflags);
        sl += (size_t)nflags;
        if (width >= 0)
            sl += (size_t)snprintf(spec + sl, sizeof spec - sl, "%d", width);
        if (prec >= 0)
            sl += (size_t)snprintf(spec + sl, sizeof spec - sl, ".%d", prec);
        spec[sl] = 0;

        switch (conv) {
        case 'd': case 'i': {
            Val *a = ai < nargs ? &args[ai++] : &none;
            double d = fmt_num(a);
            if (isnan(d) || isinf(d)) {
                /* print as the floating representation */
                char sp[90];
                snprintf(sp, sizeof sp, "%s", spec);
                { char *dot = strchr(sp, '.'); if (dot) *dot = 0; }
                strcat(sp, "f");
                buf_printf(out, sp, d);
            } else if (d > -9.2e18 && d < 9.2e18) {
                strcat(spec, "lld");
                buf_printf(out, spec, (long long)d);
            } else {
                char sp[90];
                snprintf(sp, sizeof sp, "%s", spec);
                { char *dot = strchr(sp, '.'); if (dot) *dot = 0; }
                strcat(sp, ".0f");
                buf_printf(out, sp, d < 0 ? ceil(d) : floor(d));
            }
            break;
        }
        case 'o': case 'u': case 'x': case 'X': {
            Val *a = ai < nargs ? &args[ai++] : &none;
            double d = fmt_num(a);
            unsigned long long u;
            if (isnan(d) || isinf(d)) {
                char sp[90];
                snprintf(sp, sizeof sp, "%s", spec);
                { char *dot = strchr(sp, '.'); if (dot) *dot = 0; }
                strcat(sp, "f");
                buf_printf(out, sp, d);
                break;
            }
            if (d >= 0 && d < 1.8446744073709552e19)
                u = (unsigned long long)d;
            else if (d < 0 && d > -9.2e18)
                u = (unsigned long long)(long long)d;
            else {
                char sp[90];
                snprintf(sp, sizeof sp, "%s", spec);
                { char *dot = strchr(sp, '.'); if (dot) *dot = 0; }
                strcat(sp, ".0f");
                buf_printf(out, sp, d < 0 ? ceil(d) : floor(d));
                break;
            }
            sl = strlen(spec);
            spec[sl++] = 'l';
            spec[sl++] = 'l';
            spec[sl++] = (char)conv;
            spec[sl] = 0;
            buf_printf(out, spec, u);
            break;
        }
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
        case 'a': case 'A': {
            Val *a = ai < nargs ? &args[ai++] : &none;
            sl = strlen(spec);
            spec[sl++] = (char)conv;
            spec[sl] = 0;
            buf_printf(out, spec, fmt_num(a));
            break;
        }
        case 'c': {
            Val *a = ai < nargs ? &args[ai++] : &none;
            char cb[MB_LEN_MAX + 1];
            const char *cp = cb;
            size_t cl = 0;
            if (a->t == T_NUM || a->t == T_STRNUM) {
                double d = a->n;
                unsigned long code = (d >= 0 && d < 4294967296.0) ? (unsigned long)d : 0;
                if (mb_mode && code >= 128) {
                    mbstate_t ms;
                    size_t r;
                    memset(&ms, 0, sizeof ms);
                    r = wcrtomb(cb, (wchar_t)code, &ms);
                    if (r == (size_t)-1) { cb[0] = (char)code; cl = 1; }
                    else cl = r;
                } else {
                    cb[0] = (char)code;
                    cl = 1;
                }
            } else {
                Str *s = (a->t == T_UNINIT) ? str_ref(str_empty) : str_ref(a->s);
                cl = s->len ? mb_charlen(s->s, s->len) : 0;
                if (cl > MB_LEN_MAX) cl = MB_LEN_MAX;
                memcpy(cb, s->s, cl);
                str_unref(s);
            }
            if (width > 1 && !minus) pad(out, (size_t)width - 1);
            buf_add(out, cp, cl);
            if (width > 1 && minus) pad(out, (size_t)width - 1);
            break;
        }
        case 's': {
            Val *a = ai < nargs ? &args[ai++] : &none;
            Str *s = val_tostr(a);
            size_t bytes = s->len, chars;
            if (prec >= 0)
                bytes = mb_offset(s->s, s->len, (size_t)prec);
            chars = mb_strlen(s->s, bytes);
            if (width >= 0 && (size_t)width > chars && !minus)
                pad(out, (size_t)width - chars);
            buf_add(out, s->s, bytes);
            if (width >= 0 && (size_t)width > chars && minus)
                pad(out, (size_t)width - chars);
            str_unref(s);
            break;
        }
        default:
            /* not a valid conversion: copy literally */
            buf_add(out, f + st, i - st);
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* regular expressions                                                */
/* ------------------------------------------------------------------ */

/*
 * Decode an escape sequence; s[i] is the character after the backslash.
 * Returns the index after the sequence, stores the value in *ch, or -1
 * in *ch when the sequence is not one of the defined escapes.
 */
int parse_escape(const char *s, size_t i, size_t n, int *ch)
{
    int c = (unsigned char)s[i];
    switch (c) {
    case '"': *ch = '"'; return (int)i + 1;
    case '\\': *ch = '\\'; return (int)i + 1;
    case '/': *ch = '/'; return (int)i + 1;
    case 'a': *ch = '\a'; return (int)i + 1;
    case 'b': *ch = '\b'; return (int)i + 1;
    case 'f': *ch = '\f'; return (int)i + 1;
    case 'n': *ch = '\n'; return (int)i + 1;
    case 'r': *ch = '\r'; return (int)i + 1;
    case 't': *ch = '\t'; return (int)i + 1;
    case 'v': *ch = '\v'; return (int)i + 1;
    }
    if (c >= '0' && c <= '7') {
        int v = 0, k = 0;
        while (k < 3 && i < n && s[i] >= '0' && s[i] <= '7') {
            v = v * 8 + (s[i] - '0');
            i++, k++;
        }
        *ch = v & 0xff;
        return (int)i;
    }
    *ch = -1;
    return (int)i;
}

static void emit_lit(Buf *o, int ch)
{
    if (ch && strchr(".[()*+?{|^$\\", ch))
        buf_addc(o, '\\');
    buf_addc(o, ch);
}

static void emit_brack_lit(Buf *o, int ch)
{
    if (ch == ']' || ch == '[' || ch == '-' || ch == '^') {
        buf_add(o, "[.", 2);
        buf_addc(o, ch);
        buf_add(o, ".]", 2);
    } else
        buf_addc(o, ch);
}

/* Translate an awk ERE (with awk escape conventions) to a POSIX ERE. */
static void translate_ere(Buf *o, const char *s, size_t n, int brace_literal)
{
    size_t i = 0;
    int inbr = 0;
    size_t brstart = 0;
    while (i < n) {
        int c = (unsigned char)s[i];
        if (inbr) {
            if (c == '[' && i + 1 < n && (s[i+1] == ':' || s[i+1] == '.' || s[i+1] == '=')) {
                char d = s[i+1];
                size_t j = i + 2;
                while (j + 1 < n && !(s[j] == d && s[j+1] == ']'))
                    j++;
                if (j + 1 < n) {
                    buf_add(o, s + i, j + 2 - i);
                    i = j + 2;
                    continue;
                }
                buf_addc(o, '[');
                i++;
                continue;
            }
            if (c == ']' && o->len > brstart) {
                buf_addc(o, ']');
                inbr = 0;
                i++;
                continue;
            }
            if (c == '\\' && i + 1 < n) {
                int ch;
                size_t ni = (size_t)parse_escape(s, i + 1, n, &ch);
                if (ch < 0) {
                    ch = (unsigned char)s[i + 1];
                    ni = i + 2;
                }
                emit_brack_lit(o, ch);
                i = ni;
                continue;
            }
            buf_addc(o, c);
            i++;
            continue;
        }
        if (c == '[') {
            buf_addc(o, '[');
            i++;
            inbr = 1;
            if (i < n && s[i] == '^') {
                buf_addc(o, '^');
                i++;
            }
            brstart = o->len;
            continue;
        }
        if (c == '\\') {
            int d, ch;
            size_t ni;
            if (i + 1 >= n) {
                buf_add(o, "\\\\", 2);
                i++;
                continue;
            }
            d = (unsigned char)s[i + 1];
            if (strchr(".[()*+?{|^$\\", d) && d) {
                buf_addc(o, '\\');
                buf_addc(o, d);
                i += 2;
                continue;
            }
            if (d == ']' || d == '}') {
                buf_addc(o, d);
                i += 2;
                continue;
            }
            ni = (size_t)parse_escape(s, i + 1, n, &ch);
            if (ch < 0) {
                /* undefined: take the character literally */
                emit_lit(o, d);
                i += 2;
                continue;
            }
            emit_lit(o, ch);
            i = ni;
            continue;
        }
        if (brace_literal && (c == '{' || c == '}')) {
            buf_addc(o, '\\');
            buf_addc(o, c);
            i++;
            continue;
        }
        buf_addc(o, c);
        i++;
    }
}

Regex *re_compile(const char *src, size_t len, int add_newline_alt)
{
    Regex *r = xmalloc(sizeof *r);
    Buf o;
    int rc, attempt;
    char err[256];
    for (attempt = 0; attempt < 2; attempt++) {
        buf_init(&o);
        if (add_newline_alt)
            buf_addc(&o, '(');
        translate_ere(&o, src, len, attempt);
        if (add_newline_alt)
            buf_add(&o, ")|\n", 3);
        if (!o.p)
            buf_add(&o, "", 0);
        rc = regcomp(&r->re, o.p, REG_EXTENDED);
        buf_free(&o);
        if (rc == 0)
            return r;
        if (rc != REG_BADBR && rc != REG_EBRACE && rc != REG_BADRPT)
            break;
    }
    regerror(rc, &r->re, err, sizeof err);
    fatal("invalid regular expression /%.*s/: %s", (int)len, src, err);
}

/* direct-mapped cache of dynamically compiled regular expressions */
#define RECACHE 256
static struct { Str *key; int nl; Regex *re; } recache[RECACHE];

static Regex *re_cached(Str *s, int nl)
{
    unsigned long h = (str_hash(s->s, s->len) + (unsigned long)nl) & (RECACHE - 1);
    if (recache[h].re && recache[h].nl == nl && recache[h].key->len == s->len &&
        memcmp(recache[h].key->s, s->s, s->len) == 0)
        return recache[h].re;
    if (recache[h].re) {
        regfree(&recache[h].re->re);
        free(recache[h].re);
        str_unref(recache[h].key);
    }
    recache[h].re = re_compile(s->s, s->len, nl);
    recache[h].key = str_ref(s);
    recache[h].nl = nl;
    return recache[h].re;
}

Regex *re_dynamic(Str *s) { return re_cached(s, 0); }
Regex *re_dynamic_fs(Str *s, int paragraph) { return re_cached(s, paragraph); }

int re_exec(Regex *r, const char *s, int notbol, size_t *so, size_t *eo)
{
    regmatch_t m[1];
    if (regexec(&r->re, s, 1, m, notbol ? REG_NOTBOL : 0) != 0)
        return 0;
    *so = (size_t)m[0].rm_so;
    *eo = (size_t)m[0].rm_eo;
    return 1;
}
