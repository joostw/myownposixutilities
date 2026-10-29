/*
 * util.c - memory, strings, errors, output and character helpers.
 */
#include "ex.h"

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        diag("out of memory");
        ex_exit(2);
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        diag("out of memory");
        ex_exit(2);
    }
    return p;
}

void *xcalloc(size_t a, size_t b)
{
    void *p = calloc(a ? a : 1, b ? b : 1);
    if (!p) {
        diag("out of memory");
        ex_exit(2);
    }
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

void str_init(Str *b) { b->s = NULL; b->len = b->cap = 0; }

static void str_grow(Str *b, size_t need)
{
    if (b->len + need + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 64;
        while (nc < b->len + need + 1)
            nc *= 2;
        b->s = xrealloc(b->s, nc);
        b->cap = nc;
    }
}

void str_addc(Str *b, int c)
{
    str_grow(b, 1);
    b->s[b->len++] = (char)c;
    b->s[b->len] = 0;
}

void str_addn(Str *b, const char *s, size_t n)
{
    str_grow(b, n);
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}

void str_adds(Str *b, const char *s) { str_addn(b, s, strlen(s)); }

void str_fmt(Str *b, const char *fmt, ...)
{
    va_list ap;
    char tmp[512];
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof tmp) {
        str_addn(b, tmp, (size_t)n);
        return;
    }
    str_grow(b, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(b->s + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

void str_clear(Str *b)
{
    b->len = 0;
    if (b->s)
        b->s[0] = 0;
}

void str_free(Str *b)
{
    free(b->s);
    str_init(b);
}

char *str_detach(Str *b)
{
    char *s = b->s ? b->s : xstrdup("");
    str_init(b);
    return s;
}

/* ---------- output ---------- */

static char obuf[8192];
static size_t olen;

void out_flush(void)
{
    size_t off = 0;
    while (off < olen) {
        ssize_t n = write(1, obuf + off, olen - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        off += (size_t)n;
    }
    olen = 0;
}

void out_write(const char *s, size_t n)
{
    if (vi_active) {
        vs_ex_output(s, n);
        return;
    }
    while (n) {
        size_t k = sizeof obuf - olen;
        if (k > n)
            k = n;
        memcpy(obuf + olen, s, k);
        olen += k;
        s += k;
        n -= k;
        if (olen == sizeof obuf)
            out_flush();
    }
}

void out_str(const char *s) { out_write(s, strlen(s)); }

void diag(const char *fmt, ...)
{
    va_list ap;
    char buf[1024];
    int n;
    out_flush();
    n = snprintf(buf, sizeof buf, "ex: ");
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2)
        n = (int)sizeof buf - 2;
    buf[n++] = '\n';
    if (write(2, buf, (size_t)n) < 0) {
        /* nothing useful to do */
    }
}

void bell(void)
{
    if (vi_active) {
        tputs_("\a");
        tflush();
    } else if (interactive) {
        out_str("\a");
    }
}

void msg(const char *fmt, ...)
{
    va_list ap;
    Str b;
    if (silent && !vi_active)
        return;
    str_init(&b);
    if (mode != MODE_EX && !vi_active) {
        char tmp[2048];
        va_start(ap, fmt);
        vsnprintf(tmp, sizeof tmp, fmt, ap);
        va_end(ap);
        free(pending_msg);
        pending_msg = xstrdup(tmp);
        return;
    }
    va_start(ap, fmt);
    {
        char tmp[2048];
        vsnprintf(tmp, sizeof tmp, fmt, ap);
        str_adds(&b, tmp);
    }
    va_end(ap);
    if (vi_active)
        vs_message(b.s, 0);
    else {
        str_addc(&b, '\n');
        out_write(b.s, b.len);
    }
    str_free(&b);
}

/* write an error message in the manner appropriate to the mode */
void errmsg_show(const char *text)
{
    if (mode != MODE_EX && !vi_active && !batch_stdin) {
        free(pending_msg);
        pending_msg = xstrdup(text);
        return;
    }
    if (vi_active) {
        vs_message(text, 1);
        return;
    }
    if (batch_stdin || !interactive) {
        out_flush();
        diag("%s", text);
        return;
    }
    if (term_capable_standout()) {
        out_str("\033[7m");
        out_str(text);
        out_str("\033[m\n");
    } else {
        if (OV(OP_ERRORBELLS))
            out_str("\a");
        out_str(text);
        out_str("\n");
    }
}

/* ---------- errors ---------- */

jmp_buf *errjmp;

void ex_err_silent(void)
{
    if (errjmp)
        longjmp(*errjmp, 1);
    ex_exit(1);
}

void ex_err(const char *fmt, ...)
{
    va_list ap;
    char buf[2048];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    errmsg_show(buf);
    ex_err_silent();
}

int protect(void (*fn)(void *), void *arg)
{
    jmp_buf jb, *saved = errjmp;
    errjmp = &jb;
    if (setjmp(jb)) {
        errjmp = saved;
        return -1;
    }
    fn(arg);
    errjmp = saved;
    return 0;
}

/* ---------- characters ---------- */

int mb;
static int utf8;

void chars_init(void)
{
    const char *cs;
    mb = MB_CUR_MAX > 1;
    cs = setlocale(LC_CTYPE, NULL);
    utf8 = 0;
    if (mb && cs && (strstr(cs, "UTF-8") || strstr(cs, "utf8") || strstr(cs, "UTF8") ||
                     strstr(cs, "utf-8")))
        utf8 = 1;
}

int ch_len(const char *s, int len, int pos)
{
    mbstate_t st;
    size_t n;
    if (!mb || pos >= len || (unsigned char)s[pos] < 0x80)
        return 1;
    memset(&st, 0, sizeof st);
    n = mbrtowc(NULL, s + pos, (size_t)(len - pos), &st);
    if (n == (size_t)-1 || n == (size_t)-2 || n == 0)
        return 1;
    return (int)n;
}

int ch_prev(const char *s, int pos)
{
    int p, q;
    if (pos <= 0)
        return 0;
    if (!mb)
        return pos - 1;
    if (utf8) {
        p = pos - 1;
        while (p > 0 && pos - p < 4 && ((unsigned char)s[p] & 0xC0) == 0x80)
            p--;
        if (p + ch_len(s, pos, p) == pos)
            return p;
        return pos - 1;
    }
    /* generic: scan from the start */
    p = 0;
    for (;;) {
        q = p + ch_len(s, pos, p);
        if (q >= pos)
            return p;
        p = q;
    }
}

wint_t ch_wc(const char *s, int len, int pos)
{
    mbstate_t st;
    wchar_t wc;
    size_t n;
    if (pos >= len)
        return WEOF;
    if (!mb || (unsigned char)s[pos] < 0x80)
        return (unsigned char)s[pos];
    memset(&st, 0, sizeof st);
    n = mbrtowc(&wc, s + pos, (size_t)(len - pos), &st);
    if (n == (size_t)-1 || n == (size_t)-2 || n == 0)
        return (unsigned char)s[pos] | 0x40000000u;  /* invalid byte marker */
    return (wint_t)wc;
}

int is_wordc(wint_t c)
{
    if (c == WEOF || (c & 0x40000000u))
        return 0;
    if (c < 0x80)
        return isalnum((int)c) || c == '_';
    return mb ? (iswalnum(c) || c == L'_') : (isalnum((int)c) || c == '_');
}

int is_blankc(int c) { return c == ' ' || c == '\t'; }

/*
 * Render the character at s[pos] (display column col, 0-based) into out
 * (if non-NULL); return its display width.  In list mode the characters
 * are shown as specified for the ex l command.
 */
int char_disp(Str *out, const char *s, int len, int pos, int col, int list)
{
    unsigned char c = (unsigned char)s[pos];
    static const char esc_from[] = "\\\a\b\f\n\r\t\v";
    static const char esc_to[] = "\\abfnrtv";
    char tmp[16];
    int n;
    if (list) {
        const char *e = c ? strchr(esc_from, c) : NULL;
        if (e) {
            if (out) {
                str_addc(out, '\\');
                str_addc(out, esc_to[e - esc_from]);
            }
            return 2;
        }
        if (c == '$') {
            if (out)
                str_adds(out, "\\$");
            return 2;
        }
    } else if (c == '\t') {
        int ts = (int)OV(OP_TABSTOP);
        if (ts <= 0)
            ts = 8;
        n = ts - col % ts;
        if (out)
            for (int i = 0; i < n; i++)
                str_addc(out, ' ');
        return n;
    }
    if (c < 0x80) {
        if (c >= 0x20 && c < 0x7f) {
            if (out)
                str_addc(out, (char)c);
            return 1;
        }
        if (list) {
            snprintf(tmp, sizeof tmp, "\\%03o", c);
            if (out)
                str_adds(out, tmp);
            return 4;
        }
        if (out) {
            str_addc(out, '^');
            str_addc(out, c == 0x7f ? '?' : (char)(c + '@'));
        }
        return 2;
    }
    if (mb) {
        wint_t wc = ch_wc(s, len, pos);
        if (!(wc & 0x40000000u) && iswprint(wc)) {
            int w = wcwidth((wchar_t)wc);
            int k = ch_len(s, len, pos);
            if (w < 0)
                w = 1;
            if (out)
                str_addn(out, s + pos, (size_t)k);
            return w;
        }
        if (!(wc & 0x40000000u)) {
            /* unprintable valid character: octal for each byte */
            int k = ch_len(s, len, pos);
            for (int i = 0; i < k; i++) {
                snprintf(tmp, sizeof tmp, "\\%03o", (unsigned char)s[pos + i]);
                if (out)
                    str_adds(out, tmp);
            }
            return 4 * k;
        }
    } else if (isprint(c)) {
        if (out)
            str_addc(out, (char)c);
        return 1;
    }
    snprintf(tmp, sizeof tmp, "\\%03o", c);
    if (out)
        str_adds(out, tmp);
    return 4;
}

int ch_width(const char *s, int len, int pos, int col)
{
    return char_disp(NULL, s, len, pos, col, (int)OV(OP_LIST));
}

int col_of(const char *s, int len, int off)
{
    int col = 0, p = 0;
    while (p < off && p < len) {
        col += ch_width(s, len, p, col);
        p += ch_len(s, len, p);
    }
    return col;
}

int off_of_col(const char *s, int len, int col)
{
    int c = 0, p = 0, last = 0;
    if (len == 0)
        return 0;
    while (p < len) {
        int w = ch_width(s, len, p, c);
        if (col < c + w)
            return p;
        c += w;
        last = p;
        p += ch_len(s, len, p);
    }
    return last;
}
