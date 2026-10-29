/*
 * printf.c - printf: write formatted output (POSIX.1-2024, XCU printf)
 *
 * Synopsis: printf format [argument...]
 *
 * The format is reused while arguments remain; numbered conversions
 * (%n$) refer, on each reuse, to the arguments after the highest one the
 * previous use consumed.  Output goes through stdio; a write error makes
 * the exit status 1.
 *
 * Choices where the standard leaves room:
 *  - A first argument "--" is discarded (the utility has no options).
 *  - The floating conversions a A e E f F g G are supported, in long
 *    double; * is accepted for the width and the precision (*m$ with
 *    numbered conversions); the length modifiers h l L q j z t are
 *    accepted and ignored.
 *  - In the format, \" and \' are the quote characters (as in the sh
 *    built-in); a backslash before any other character is written with
 *    that character; in a %b argument too.  \c in the format is not
 *    special.  In %b, \ddd without the leading 0 is also an octal byte.
 *  - %c of an empty argument writes nothing.
 *  - The value of 'x (or "x) is that of the character x in the locale's
 *    codeset (a wide character value); if no character follows, 0.
 *  - A conversion specification that is not valid is reported, and
 *    nothing more is written; the exit status is 1.
 *  - A missing argument for a numbered conversion is reported (status 1)
 *    and taken as an empty string or zero.
 *  - Numbers out of range are reported, and the nearest value is used.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int status;
static bool stop;               /* \c in a %b argument */

static void bad(const char *arg, const char *what)
{
    fprintf(stderr, "printf: %s: %s\n", arg, what);
    status = 1;
}

static void out(const char *s, size_t n)
{
    if (n && fwrite(s, 1, n, stdout) != n)
        status = 1;
}

static void outc(int c)
{
    if (putchar(c) == EOF)
        status = 1;
}

/* the value of a character constant 'x or "x */
static intmax_t char_value(const char *s)
{
    if (!s[1])
        return 0;
    wchar_t wc;
    mbstate_t st;
    memset(&st, 0, sizeof st);
    size_t k = mbrtowc(&wc, s + 1, strlen(s + 1), &st);
    if (k == (size_t)-1 || k == (size_t)-2 || k == 0)
        return (unsigned char)s[1];
    return (intmax_t)wc;
}

/* an integer argument; unsigned_ok: for o u x X, values above INTMAX_MAX */
static intmax_t get_int(const char *s, uintmax_t *u)
{
    *u = 0;
    if (!s || !*s)
        return 0;
    if (s[0] == '\'' || s[0] == '"') {
        intmax_t v = char_value(s);
        *u = (uintmax_t)v;
        return v;
    }
    const char *q = s;
    while (isspace((unsigned char)*q))
        q++;
    char *end;
    intmax_t v;
    errno = 0;
    if (*q == '-') {
        v = strtoimax(q, &end, 0);
        *u = (uintmax_t)v;
    } else {
        *u = strtoumax(q, &end, 0);
        v = *u > (uintmax_t)INTMAX_MAX ? INTMAX_MAX : (intmax_t)*u;
    }
    if (end == q || !(isdigit((unsigned char)*q) || *q == '+' || *q == '-'))
        bad(s, "expected a numeric value");
    else if (*end)
        bad(s, "not completely converted");
    else if (errno == ERANGE)
        bad(s, "out of range");
    return v;
}

static long double get_float(const char *s)
{
    if (!s || !*s)
        return 0;
    if (s[0] == '\'' || s[0] == '"')
        return (long double)char_value(s);
    char *end;
    errno = 0;
    long double d = strtold(s, &end);
    if (end == s)
        bad(s, "expected a numeric value");
    else if (*end)
        bad(s, "not completely converted");
    else if (errno == ERANGE)
        bad(s, "out of range");
    return d;
}

/* a backslash escape of the format (p after the backslash); the number
   of characters used */
static size_t format_escape(const char *p)
{
    switch (*p) {
    case '\\': outc('\\'); return 1;
    case 'a': outc('\a'); return 1;
    case 'b': outc('\b'); return 1;
    case 'f': outc('\f'); return 1;
    case 'n': outc('\n'); return 1;
    case 'r': outc('\r'); return 1;
    case 't': outc('\t'); return 1;
    case 'v': outc('\v'); return 1;
    case '"': outc('"'); return 1;
    case '\'': outc('\''); return 1;
    case '\0': outc('\\'); return 0;
    }
    if (*p >= '0' && *p <= '7') {
        int v = 0, k;
        for (k = 0; k < 3 && p[k] >= '0' && p[k] <= '7'; k++)
            v = v * 8 + (p[k] - '0');
        outc(v & 0xff);
        return (size_t)k;
    }
    outc('\\');
    outc((unsigned char)*p);
    return 1;
}

/* a growing byte buffer */
struct buf {
    char *s;
    size_t len, cap;
};

static void bput(struct buf *b, int c)
{
    if (b->len + 1 >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        if (!(b->s = realloc(b->s, b->cap))) {
            fprintf(stderr, "printf: out of memory\n");
            exit(1);
        }
    }
    b->s[b->len++] = (char)c;
}

/* the argument of %b, with its escapes */
static void b_expand(const char *p, struct buf *o)
{
    while (*p) {
        if (*p != '\\') {
            bput(o, (unsigned char)*p++);
            continue;
        }
        p++;
        if (*p == 'c') {
            stop = true;
            return;
        }
        if (*p >= '0' && *p <= '7') {
            int v = 0, k, max = *p == '0' ? 4 : 3;
            for (k = 0; k < max && *p >= '0' && *p <= '7'; k++)
                v = v * 8 + (*p++ - '0');
            bput(o, v & 0xff);
            continue;
        }
        switch (*p) {
        case '\\': bput(o, '\\'); p++; continue;
        case 'a': bput(o, '\a'); p++; continue;
        case 'b': bput(o, '\b'); p++; continue;
        case 'f': bput(o, '\f'); p++; continue;
        case 'n': bput(o, '\n'); p++; continue;
        case 'r': bput(o, '\r'); p++; continue;
        case 't': bput(o, '\t'); p++; continue;
        case 'v': bput(o, '\v'); p++; continue;
        }
        bput(o, '\\');
        if (*p)
            bput(o, (unsigned char)*p++);
    }
}

/* write a conversion with the C printf; spec is the specification */
static void cformat(const char *spec, ...)
{
    va_list ap, aq;
    va_start(ap, spec);
    va_copy(aq, ap);
    char small[256];
    int n = vsnprintf(small, sizeof small, spec, ap);
    va_end(ap);
    if (n < 0) {
        va_end(aq);
        fprintf(stderr, "printf: %s\n", strerror(errno));
        status = 1;
        return;
    }
    if ((size_t)n < sizeof small) {
        out(small, (size_t)n);
    } else {
        char *big = malloc((size_t)n + 1);
        if (!big) {
            fprintf(stderr, "printf: out of memory\n");
            exit(1);
        }
        vsnprintf(big, (size_t)n + 1, spec, aq);
        out(big, (size_t)n);
        free(big);
    }
    va_end(aq);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int i = 1;
    if (i < argc && strcmp(argv[i], "--") == 0)
        i++;
    if (i >= argc) {
        fprintf(stderr, "usage: printf format [argument...]\n");
        return 2;
    }
    const char *fmt = argv[i++];
    char **args = argv + i;
    int nargs = argc - i, ai = 0, maxused = 0;
    bool numbered = false;
    for (;;) {
        const char *p = fmt;
        bool consumed = false;
        int base = ai;
        while (*p && !stop) {
            if (*p == '\\') {
                p++;
                p += format_escape(p);
                continue;
            }
            if (*p != '%') {
                outc((unsigned char)*p++);
                continue;
            }
            if (p[1] == '%') {
                outc('%');
                p += 2;
                continue;
            }
            const char *start = p;
            int argn = -1;
            {
                const char *q = p + 1;
                int n = 0;
                while (isdigit((unsigned char)*q) && n < 100000)
                    n = n * 10 + (*q++ - '0');
                if (*q == '$' && n > 0) {
                    argn = n;
                    numbered = true;
                    p = q;
                }
            }
            char spec[80];
            size_t sl = 0;
            spec[sl++] = '%';
            p++;
            while (*p && strchr("-+ #0", *p)) {
                if (sl < 20)
                    spec[sl++] = *p;
                p++;
            }
            int width = 0, prec = 0;
            bool havewidth = false, haveprec = false;
            /* an argument for *, or *m$ */
            #define STAR_ARG(dst)                                                   \
                do {                                                                \
                    const char *sa_;                                                \
                    uintmax_t su_;                                                  \
                    if (argn > 0) {                                                 \
                        int m_ = 0;                                                 \
                        while (isdigit((unsigned char)*p) && m_ < 100000)           \
                            m_ = m_ * 10 + (*p++ - '0');                            \
                        if (*p == '$')                                              \
                            p++;                                                    \
                        sa_ = base + m_ - 1 < nargs && m_ > 0 ? args[base + m_ - 1] : NULL; \
                        if (base + m_ > maxused)                                    \
                            maxused = base + m_;                                    \
                    } else {                                                        \
                        sa_ = ai < nargs ? args[ai++] : NULL;                       \
                        consumed = true;                                            \
                        if (ai > maxused)                                           \
                            maxused = ai;                                           \
                    }                                                               \
                    intmax_t v_ = get_int(sa_, &su_);                               \
                    dst = v_ > 1000000 ? 1000000 : v_ < -1000000 ? -1000000 : (int)v_; \
                } while (0)
            if (*p == '*') {
                p++;
                STAR_ARG(width);
                havewidth = true;
                if (width < 0) {
                    spec[sl++] = '-';
                    width = -width;
                }
            } else {
                while (isdigit((unsigned char)*p)) {
                    if (width < 1000000)
                        width = width * 10 + (*p - '0');
                    havewidth = true;
                    p++;
                }
            }
            if (*p == '.') {
                p++;
                haveprec = true;
                if (*p == '*') {
                    p++;
                    STAR_ARG(prec);
                    if (prec < 0)
                        haveprec = false;
                } else {
                    while (isdigit((unsigned char)*p)) {
                        if (prec < 1000000)
                            prec = prec * 10 + (*p - '0');
                        p++;
                    }
                }
            }
            while (*p && strchr("hlLqjzt", *p))
                p++;
            int conv = (unsigned char)*p;
            if (!conv || !strchr("diouxXeEfFgGaAcsb", conv)) {
                if (!conv)
                    fprintf(stderr, "printf: %s: a conversion specifier is missing\n", start);
                else
                    fprintf(stderr, "printf: %%%c: not a valid conversion\n", conv);
                status = 1;
                fflush(stdout);
                return 1;
            }
            p++;
            const char *arg;
            if (argn > 0) {
                int idx = base + argn - 1;
                arg = idx < nargs ? args[idx] : NULL;
                if (base + argn > maxused)
                    maxused = base + argn;
                if (!arg) {
                    fprintf(stderr, "printf: %%%d$: no such argument\n", argn);
                    status = 1;
                }
            } else {
                arg = ai < nargs ? args[ai++] : NULL;
                consumed = true;
                if (ai > maxused)
                    maxused = ai;
            }
            if (havewidth)
                sl += (size_t)snprintf(spec + sl, sizeof spec - sl, "%d", width);
            if (haveprec)
                sl += (size_t)snprintf(spec + sl, sizeof spec - sl, ".%d", prec);
            spec[sl] = '\0';
            uintmax_t u;
            switch (conv) {
            case 'd': case 'i': {
                intmax_t v = get_int(arg, &u);
                strcat(spec, "jd");
                cformat(spec, v);
                break;
            }
            case 'o': case 'u': case 'x': case 'X': {
                get_int(arg, &u);
                size_t k = strlen(spec);
                spec[k] = 'j';
                spec[k + 1] = (char)conv;
                spec[k + 2] = '\0';
                cformat(spec, u);
                break;
            }
            case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
                long double d = get_float(arg);
                size_t k = strlen(spec);
                spec[k] = 'L';
                spec[k + 1] = (char)conv;
                spec[k + 2] = '\0';
                cformat(spec, d);
                break;
            }
            case 'c': {
                char cs[2] = { arg && *arg ? arg[0] : 0, 0 };
                strcat(spec, "s");
                cformat(spec, cs);
                break;
            }
            case 's':
                strcat(spec, "s");
                cformat(spec, arg ? arg : "");
                break;
            case 'b': {
                struct buf t = { 0 };
                b_expand(arg ? arg : "", &t);
                /* width and precision count bytes; the result may hold NUL */
                size_t len = t.len;
                size_t n = haveprec && (size_t)prec < len ? (size_t)prec : len;
                size_t pad = havewidth && (size_t)width > n ? (size_t)width - n : 0;
                bool left = strchr(spec, '-') != NULL;
                if (!left)
                    while (pad-- > 0)
                        outc(' ');
                out(t.s, n);
                if (left)
                    while (pad-- > 0)
                        outc(' ');
                free(t.s);
                break;
            }
            }
        }
        if (stop)
            break;
        if (numbered) {
            if (maxused <= base || maxused >= nargs)
                break;
            ai = maxused;
            continue;
        }
        if (!consumed || ai >= nargs)
            break;
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "printf: write error: %s\n", strerror(errno));
        status = 1;
    }
    return status;
}
