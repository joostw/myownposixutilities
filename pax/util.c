/*
 * util.c - memory, messages, buffers and keyword lists.
 */
#include "pax.h"

int exit_status;
const char *progname = "pax";

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        errx_("out of memory");
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        errx_("out of memory");
    return p;
}

char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static void vmsg(const char *fmt, va_list ap, int with_errno)
{
    int e = errno;
    fflush(stdout);
    fprintf(stderr, "%s: ", progname);
    vfprintf(stderr, fmt, ap);
    if (with_errno)
        fprintf(stderr, ": %s", strerror(e));
    fputc('\n', stderr);
    fflush(stderr);
}

void warnx_(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmsg(fmt, ap, 0);
    va_end(ap);
    exit_status = 1;
}

void warn_(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmsg(fmt, ap, 1);
    va_end(ap);
    exit_status = 1;
}

void errx_(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmsg(fmt, ap, 0);
    va_end(ap);
    exit(1);
}

void buf_add(Buf *b, const void *p, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 64;
        while (nc < b->len + n + 1)
            nc *= 2;
        b->s = xrealloc(b->s, nc);
        b->cap = nc;
    }
    memcpy(b->s + b->len, p, n);
    b->len += n;
    b->s[b->len] = 0;
}

void buf_adds(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }

void buf_addc(Buf *b, int c)
{
    char ch = (char)c;
    buf_add(b, &ch, 1);
}

void buf_fmt(Buf *b, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof tmp) {
        char *big = xmalloc((size_t)n + 1);
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        buf_add(b, big, (size_t)n);
        free(big);
    } else if (n > 0)
        buf_add(b, tmp, (size_t)n);
}

void buf_free(Buf *b)
{
    free(b->s);
    b->s = NULL;
    b->len = b->cap = 0;
}

void kv_set(Kv **list, const char *key, const char *val, size_t vlen)
{
    Kv *k;
    for (k = *list; k; k = k->next)
        if (strcmp(k->key, key) == 0)
            break;
    if (!k) {
        Kv **pp = list;
        while (*pp)
            pp = &(*pp)->next;
        k = xmalloc(sizeof *k);
        k->key = xstrdup(key);
        k->next = NULL;
        *pp = k;
    } else
        free(k->val);
    k->val = xstrndup(val, vlen);
    k->vlen = vlen;
}

Kv *kv_get(Kv *list, const char *key)
{
    for (; list; list = list->next)
        if (strcmp(list->key, key) == 0)
            return list;
    return NULL;
}

void kv_free(Kv **list)
{
    Kv *k = *list, *n;
    for (; k; k = n) {
        n = k->next;
        free(k->key);
        free(k->val);
        free(k);
    }
    *list = NULL;
}

Kv *kv_copy(Kv *list)
{
    Kv *r = NULL;
    for (; list; list = list->next)
        kv_set(&r, list->key, list->val, list->vlen);
    return r;
}

void entry_init(Entry *e)
{
    memset(e, 0, sizeof *e);
    e->nlink = 1;
}

void entry_free(Entry *e)
{
    free(e->path);
    free(e->link);
    free(e->uname);
    free(e->gname);
    free(e->src);
    free(e->raw_path);
    free(e->raw_link);
    kv_free(&e->xrec);
    entry_init(e);
}
