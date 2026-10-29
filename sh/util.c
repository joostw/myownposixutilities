/*
 * util.c - memory, stack allocator, buffers, exceptions, output, options.
 */
#include "sh.h"

/* ------------------------------------------------------------------ */
/* memory                                                             */
/* ------------------------------------------------------------------ */

static _Noreturn void nomem(void)
{
    static const char m[] = "sh: out of memory\n";
    (void)!write(2, m, sizeof m - 1);
    _exit(2);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) nomem();
    return p;
}

void *xcalloc(size_t n, size_t m)
{
    void *p = calloc(n ? n : 1, m ? m : 1);
    if (!p) nomem();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) nomem();
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

/* stack allocator: a linked list of chunks */
typedef struct Chunk { struct Chunk *prev; size_t size, used; char data[]; } Chunk;
static Chunk *stack_top;

void *stalloc(size_t n)
{
    Chunk *c = stack_top;
    n = (n + 15) & ~(size_t)15;
    if (!c || c->used + n > c->size) {
        size_t sz = n > 16000 ? n : 16000;
        c = xmalloc(sizeof(Chunk) + sz);
        c->prev = stack_top;
        c->size = sz;
        c->used = 0;
        stack_top = c;
    }
    c->used += n;
    return c->data + c->used - n;
}

char *ststrndup(const char *s, size_t n)
{
    char *p = stalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

char *ststrdup(const char *s)
{
    return ststrndup(s, strlen(s));
}

void setstackmark(StackMark *m)
{
    m->chunk = stack_top;
    m->used = stack_top ? stack_top->used : 0;
}

void popstackmark(StackMark *m)
{
    while (stack_top && stack_top != m->chunk) {
        Chunk *p = stack_top->prev;
        free(stack_top);
        stack_top = p;
    }
    if (stack_top)
        stack_top->used = m->used;
}

/* growable buffer */
void sb_init(StrBuf *b) { b->s = NULL; b->len = b->cap = 0; }

static void sb_grow(StrBuf *b, size_t extra)
{
    if (b->len + extra + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 64;
        while (nc < b->len + extra + 1)
            nc *= 2;
        b->s = xrealloc(b->s, nc);
        b->cap = nc;
    }
}

void sb_addc(StrBuf *b, int c)
{
    sb_grow(b, 1);
    b->s[b->len++] = (char)c;
    b->s[b->len] = 0;
}

void sb_add(StrBuf *b, const char *s, size_t n)
{
    sb_grow(b, n);
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}

void sb_adds(StrBuf *b, const char *s) { sb_add(b, s, strlen(s)); }

void sb_printf(StrBuf *b, const char *fmt, ...)
{
    va_list ap;
    int n;
    char tmp[256];
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) { sb_add(b, tmp, (size_t)n); return; }
    sb_grow(b, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(b->s + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

char *sb_stack(StrBuf *b)
{
    char *p = ststrndup(b->s ? b->s : "", b->len);
    sb_free(b);
    return p;
}

char *sb_take(StrBuf *b)
{
    char *p = b->s ? b->s : xstrdup("");
    sb_init(b);
    return p;
}

void sb_free(StrBuf *b)
{
    free(b->s);
    sb_init(b);
}

/* ------------------------------------------------------------------ */
/* exceptions and errors                                              */
/* ------------------------------------------------------------------ */

struct jmploc *handler;
int exception;
const char *arg0 = "sh";
char *commandname;

void raise_exc(int e)
{
    exception = e;
    if (!handler)
        _exit(2);
    longjmp(handler->loc, 1);
}

static void vwarn(const char *fmt, va_list ap)
{
    char buf[1024];
    int n;
    outflush(&out1);
    if (commandname)
        n = snprintf(buf, sizeof buf, "%s: %s: ", arg0, commandname);
    else if (!interactive && cmd_lineno > 0)
        n = snprintf(buf, sizeof buf, "%s: %d: ", arg0, cmd_lineno);
    else
        n = snprintf(buf, sizeof buf, "%s: ", arg0);
    if (n < 0) n = 0;
    if ((size_t)n < sizeof buf) {
        int m = vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, ap);
        if (m > 0) n += m;
    }
    if ((size_t)n >= sizeof buf - 1)
        n = sizeof buf - 2;
    buf[n++] = '\n';
    outn(&out2, buf, (size_t)n);
    outflush(&out2);
}

void sh_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vwarn(fmt, ap);
    va_end(ap);
}

void sh_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vwarn(fmt, ap);
    va_end(ap);
    exitstatus = 2;
    raise_exc(EX_ERROR);
}

void syntax_error_at(int line, const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    outflush(&out1);
    if (!interactive && line > 0)
        outfmt(&out2, "%s: %d: syntax error: %s\n", arg0, line, buf);
    else
        outfmt(&out2, "%s: syntax error: %s\n", arg0, buf);
    outflush(&out2);
    exitstatus = 2;
    raise_exc(EX_ERROR);
}

/* ------------------------------------------------------------------ */
/* output                                                             */
/* ------------------------------------------------------------------ */

Out out1 = { 1, {0}, 0, 0 }, out2 = { 2, {0}, 0, 0 };

int outflush(Out *o)
{
    size_t off = 0;
    while (off < o->len) {
        ssize_t n = write(o->fd, o->buf + off, o->len - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            o->err = 1;
            break;
        }
        off += (size_t)n;
    }
    o->len = 0;
    return o->err ? -1 : 0;
}

void outn(Out *o, const char *s, size_t n)
{
    while (n) {
        size_t k = sizeof o->buf - o->len;
        if (k == 0) {
            outflush(o);
            continue;
        }
        if (k > n) k = n;
        memcpy(o->buf + o->len, s, k);
        o->len += k;
        s += k;
        n -= k;
    }
}

void outc(Out *o, int c)
{
    char ch = (char)c;
    outn(o, &ch, 1);
}

void outs(Out *o, const char *s) { outn(o, s, strlen(s)); }

void outfmt(Out *o, const char *fmt, ...)
{
    va_list ap;
    char tmp[1024];
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) {
        outn(o, tmp, (size_t)n);
    } else {
        char *p = xmalloc((size_t)n + 1);
        va_start(ap, fmt);
        vsnprintf(p, (size_t)n + 1, fmt, ap);
        va_end(ap);
        outn(o, p, (size_t)n);
        free(p);
    }
}

void flushall(void)
{
    outflush(&out1);
    outflush(&out2);
}

/* ------------------------------------------------------------------ */
/* options                                                            */
/* ------------------------------------------------------------------ */

char opts[NOPTS];
const struct OptDesc optdesc[NOPTS] = {
    { "allexport", 'a' }, { "notify", 'b' }, { "noclobber", 'C' },
    { "errexit", 'e' }, { "noglob", 'f' }, { "hashall", 'h' },
    { "interactive", 'i' }, { "monitor", 'm' }, { "noexec", 'n' },
    { "nounset", 'u' }, { "verbose", 'v' }, { "xtrace", 'x' },
    { "ignoreeof", 0 }, { "nolog", 0 }, { "pipefail", 0 }, { "vi", 0 },
    { "stdin", 's' }, { "cmdstr", 'c' }
};

static void option_changed(int i)
{
    if (i == O_MONITOR)
        set_job_control(opts[i]);
    else if (i == O_NOTIFY)
        set_notify(opts[i]);
}

int set_option_letter(int c, int on)
{
    int i;
    for (i = 0; i < NOPTS; i++)
        if (optdesc[i].letter == c && c) {
            if (i == O_INTERACTIVE || i == O_STDIN || i == O_CMDSTR)
                return -1;  /* only at invocation */
            opts[i] = (char)on;
            option_changed(i);
            return 0;
        }
    return -1;
}

int set_option_name(const char *name, int on)
{
    int i;
    for (i = 0; i < NOPTS; i++)
        if (strcmp(optdesc[i].name, name) == 0 && i != O_INTERACTIVE &&
            i != O_STDIN && i != O_CMDSTR) {
            opts[i] = (char)on;
            option_changed(i);
            return 0;
        }
    return -1;
}

char *optflags_string(void)
{
    char buf[NOPTS + 1];
    int i, n = 0;
    for (i = 0; i < NOPTS; i++)
        if (opts[i] && optdesc[i].letter)
            buf[n++] = optdesc[i].letter;
    buf[n] = 0;
    return ststrdup(buf);
}

/* ------------------------------------------------------------------ */
/* misc                                                               */
/* ------------------------------------------------------------------ */

long parse_long(const char *s, int *ok)
{
    char *end;
    long v;
    errno = 0;
    if (!*s || isspace((unsigned char)*s)) { *ok = 0; return 0; }
    v = strtol(s, &end, 10);
    *ok = (*end == 0 && errno == 0);
    return v;
}

int is_number(const char *s)
{
    if (!*s) return 0;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s))
            return 0;
    return 1;
}

/* quote a string so that the shell reads it back as the same word */
char *quote_for_reinput(const char *s)
{
    StrBuf b;
    const char *p;
    int safe = *s != 0;
    for (p = s; *p; p++)
        if (!(isalnum((unsigned char)*p) || strchr("_/.,:@%+-=", *p)))
            safe = 0;
    if (safe)
        return ststrdup(s);
    sb_init(&b);
    sb_addc(&b, '\'');
    for (p = s; *p; p++) {
        if (*p == '\'')
            sb_adds(&b, "'\\''");
        else
            sb_addc(&b, *p);
    }
    sb_addc(&b, '\'');
    return sb_stack(&b);
}

int is_name_n(const char *s, size_t n)
{
    size_t i;
    if (n == 0 || !(isalpha((unsigned char)s[0]) || s[0] == '_') || (unsigned char)s[0] > 127)
        return 0;
    for (i = 1; i < n; i++)
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_') || (unsigned char)s[i] > 127)
            return 0;
    return 1;
}

int is_name(const char *s) { return is_name_n(s, strlen(s)); }
