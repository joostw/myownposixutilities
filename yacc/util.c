/*
 * util.c - memory, strings and diagnostics.
 */
#include "yacc.h"

const char *progname = "yacc";
const char *input_name = "";
int nerrors;

static void nomem(void)
{
    fprintf(stderr, "%s: out of memory\n", progname);
    exit(2);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        nomem();
    return p;
}

void *xcalloc(size_t n, size_t m)
{
    void *p = calloc(n ? n : 1, m ? m : 1);
    if (!p)
        nomem();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        nomem();
    return p;
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

void str_add(Str *b, const char *p, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->s = xrealloc(b->s, b->cap);
    }
    memcpy(b->s + b->len, p, n);
    b->len += n;
    b->s[b->len] = 0;
}

void str_adds(Str *b, const char *s)
{
    str_add(b, s, strlen(s));
}

void str_addc(Str *b, int c)
{
    char ch = (char)c;
    str_add(b, &ch, 1);
}

void str_printf(Str *b, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof tmp) {
        str_add(b, tmp, (size_t)n);
        return;
    }
    {
        char *big = xmalloc((size_t)n + 1);
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        str_add(b, big, (size_t)n);
        free(big);
    }
}

static void diag(const char *kind, int line, const char *fmt, va_list ap)
{
    fprintf(stderr, "%s: ", progname);
    if (line > 0)
        fprintf(stderr, "%s:%d: ", input_name, line);
    else if (*input_name)
        fprintf(stderr, "%s: ", input_name);
    if (kind)
        fprintf(stderr, "%s: ", kind);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void error_at(int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    diag(NULL, line, fmt, ap);
    va_end(ap);
    nerrors++;
}

void warning_at(int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    diag("warning", line, fmt, ap);
    va_end(ap);
}

void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", progname);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}
