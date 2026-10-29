/*
 * util.c - memory, strings, diagnostics.
 */
#include "make.h"

const char *progname = "make";

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "%s: out of memory\n", progname);
        exit(2);
    }
    return p;
}

void *xcalloc(size_t n, size_t m)
{
    void *p = calloc(n ? n : 1, m ? m : 1);
    if (!p) {
        fprintf(stderr, "%s: out of memory\n", progname);
        exit(2);
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        fprintf(stderr, "%s: out of memory\n", progname);
        exit(2);
    }
    return p;
}

char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

void diag(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void fatal(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s: *** ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs(".  Stop.\n", stderr);
    exit(2);
}

void file_fatal(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s: %s:%d: *** ", progname, file, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs(".  Stop.\n", stderr);
    exit(2);
}

void file_warn(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s: %s:%d: warning: ", progname, file, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void buf_add(Buf *b, const char *p, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->s = xrealloc(b->s, b->cap);
    }
    memcpy(b->s + b->len, p, n);
    b->len += n;
    b->s[b->len] = 0;
}

void buf_adds(Buf *b, const char *s)
{
    buf_add(b, s, strlen(s));
}

void buf_addc(Buf *b, int c)
{
    char ch = (char)c;
    buf_add(b, &ch, 1);
}

void buf_reset(Buf *b)
{
    b->len = 0;
    if (b->s)
        b->s[0] = 0;
}

char *buf_take(Buf *b)
{
    char *s = b->s ? b->s : xstrdup("");
    b->s = NULL;
    b->len = b->cap = 0;
    return s;
}

void buf_free(Buf *b)
{
    free(b->s);
    b->s = NULL;
    b->len = b->cap = 0;
}

void words_add(Words *w, char *s)
{
    if (w->n + 1 >= w->cap) {
        w->cap = w->cap ? w->cap * 2 : 8;
        w->v = xrealloc(w->v, (size_t)w->cap * sizeof(char *));
    }
    w->v[w->n++] = s;
    w->v[w->n] = NULL;
}

void words_free(Words *w)
{
    for (int i = 0; i < w->n; i++)
        free(w->v[i]);
    free(w->v);
    w->v = NULL;
    w->n = w->cap = 0;
}

int is_blank(int c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

void split_words(const char *s, Words *w, int parens)
{
    while (*s) {
        const char *st;
        int depth = 0;
        while (*s && is_blank((unsigned char)*s))
            s++;
        if (!*s)
            break;
        st = s;
        while (*s && (depth > 0 || !is_blank((unsigned char)*s))) {
            if (parens) {
                if (*s == '(')
                    depth++;
                else if (*s == ')' && depth > 0)
                    depth--;
            }
            s++;
        }
        words_add(w, xstrndup(st, (size_t)(s - st)));
    }
}
