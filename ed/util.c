/*
 * util.c - memory, errors, output of lines (p, n, l), reading
 * commands and text, and running shell commands.
 */
#include "ed.h"
#include <termios.h>

const char *errmsg;
int help_mode, had_error;
char *last_shell;

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fputs("ed: Memory exhausted\n", stderr);
        exit(2);
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        fputs("ed: Memory exhausted\n", stderr);
        exit(2);
    }
    return p;
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
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

void buf_clear(Buf *b)
{
    b->len = 0;
    if (b->s)
        b->s[0] = 0;
}

int fail(const char *msg)
{
    errmsg = msg;
    return -1;
}

void report_error(void)
{
    fputs("?\n", stdout);
    if (help_mode && errmsg)
        printf("%s\n", errmsg);
    fflush(stdout);
    had_error = 1;
}

/* ---------- output ---------- */

#define FOLD 72

static void list_line(Line *l)
{
    int col = 0;
    size_t i = 0;
    mbstate_t st;
    memset(&st, 0, sizeof st);
    while (i < l->len) {
        char tok[64];
        int w = 0;
        size_t used = 1;
        unsigned char c = (unsigned char)l->s[i];
        const char *esc = NULL;
        switch (c) {
        case '\\': esc = "\\\\"; break;
        case '\a': esc = "\\a"; break;
        case '\b': esc = "\\b"; break;
        case '\f': esc = "\\f"; break;
        case '\r': esc = "\\r"; break;
        case '\t': esc = "\\t"; break;
        case '\v': esc = "\\v"; break;
        case '$': esc = "\\$"; break;
        }
        if (esc) {
            snprintf(tok, sizeof tok, "%s", esc);
            w = 2;
        } else {
            wchar_t wc;
            size_t n = mbrtowc(&wc, l->s + i, l->len - i, &st);
            if (n == (size_t)-1 || n == (size_t)-2 || n == 0 || !iswprint((wint_t)wc)) {
                if (n == (size_t)-1 || n == (size_t)-2)
                    memset(&st, 0, sizeof st);
                if (n != (size_t)-1 && n != (size_t)-2 && n > 1) {
                    /* a non-printable multi-byte character: every byte in octal */
                    size_t k = 0;
                    for (size_t j = 0; j < n; j++)
                        k += (size_t)snprintf(tok + k, sizeof tok - k, "\\%03o", (unsigned char)l->s[i + j]);
                    used = n;
                    w = (int)k;
                } else {
                    snprintf(tok, sizeof tok, "\\%03o", c);
                    w = 4;
                }
            } else {
                memcpy(tok, l->s + i, n);
                tok[n] = 0;
                used = n;
                w = wcwidth(wc);
                if (w < 0)
                    w = 1;
            }
        }
        if (col + w > FOLD) {
            fputs("\\\n", stdout);
            col = 0;
        }
        fputs(tok, stdout);
        col += w;
        i += used;
    }
    if (col + 1 > FOLD)
        fputs("\\\n", stdout);
    fputs("$\n", stdout);
}

void print_lines(long from, long to, int flags)
{
    for (long i = from; i <= to; i++) {
        Line *l = lines[i];
        if (interrupted)
            return;
        if (flags & PF_N)
            printf("%ld\t", i);
        if (flags & PF_L)
            list_line(l);
        else {
            fwrite(l->s, 1, l->len, stdout);
            putchar('\n');
        }
    }
}

void out_flush(void)
{
    fflush(stdout);
}

/* ---------- input ---------- */

static char ibuf[8192];
static size_t ipos, ilen;

static int stdin_next(Reader *r, Buf *b)
{
    (void)r;
    buf_clear(b);
    for (;;) {
        char *nl = ipos < ilen ? memchr(ibuf + ipos, '\n', ilen - ipos) : NULL;
        ssize_t n;
        if (nl) {
            buf_add(b, ibuf + ipos, (size_t)(nl - (ibuf + ipos)));
            ipos = (size_t)(nl - ibuf) + 1;
            if (!b->s)
                buf_adds(b, "");
            return 1;
        }
        if (ipos < ilen)
            buf_add(b, ibuf + ipos, ilen - ipos);
        ipos = ilen = 0;
        out_flush();
        n = read(0, ibuf, sizeof ibuf);
        if (n < 0) {
            if (errno == EINTR) {
                if (interrupted) {
                    buf_clear(b);
                    return -1;
                }
                continue;
            }
            return b->len ? 1 : 0;
        }
        if (n == 0)
            return b->len ? 1 : 0;
        ilen = (size_t)n;
    }
}

Reader stdin_reader = { stdin_next, NULL, 0, 0 };

/* after an error on a terminal, input typed ahead is discarded */
void flush_input(void)
{
    if (isatty(0)) {
        ipos = ilen = 0;
        tcflush(0, TCIFLUSH);
    }
}

/* input mode: lines up to "." */
int read_text(Reader *r, Buf *tmp, Line ***v, long *n)
{
    long cap = 0;
    *v = NULL;
    *n = 0;
    for (;;) {
        int k = r->next(r, tmp);
        if (k < 0) {
            for (long i = 0; i < *n; i++)
                free((*v)[i]);
            free(*v);
            *v = NULL;
            *n = 0;
            return fail("Interrupt");
        }
        if (k == 0)
            break;
        if (tmp->len == 1 && tmp->s[0] == '.')
            break;
        if (*n + 1 > cap) {
            cap = cap ? cap * 2 : 64;
            *v = xrealloc(*v, (size_t)cap * sizeof(Line *));
        }
        (*v)[(*n)++] = line_new(tmp->s ? tmp->s : "", tmp->len);
    }
    return 0;
}

/* ---------- shell ---------- */

int run_shell(const char *cmd)
{
    pid_t pid;
    int st = -1;
    struct sigaction ign, oint;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    out_flush();
    sigaction(SIGINT, &ign, &oint);
    pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    if (pid > 0)
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
            ;
    sigaction(SIGINT, &oint, NULL);
    if (pid < 0)
        return fail("Cannot create shell process");
    return st;
}
