/*
 * input.c - the stack of input sources the lexer reads from.
 *
 * Sources are file descriptors (scripts, standard input, terminals),
 * strings (-c, eval, command substitutions, traps) and alias values.
 * When reading commands from standard input the shell never consumes
 * bytes beyond the current command: it reads one byte at a time unless
 * the input is seekable, in which case input_sync() seeks back.
 */
#include "sh.h"

enum { S_FD, S_STR, S_ALIAS, S_CONT };

typedef struct Src {
    int type;
    int fd;
    int tty;            /* interactive terminal source */
    int seekable;
    char *buf;
    size_t pos, len, cap;
    int heap;
    Alias *alias;
    int eof;
    int pushback[16];
    int npush;
    struct Src *prev;
} Src;

static Src *src;
int alias_blank_popped;     /* an alias whose value ends in a blank ended */
static Src base_src;    /* pushback holder when no source is active */
#define PB (src ? src : &base_src)
static int prompt_which = 1;

int input_depth(void)
{
    int n = 0;
    Src *s;
    for (s = src; s; s = s->prev) n++;
    return n;
}

void push_input_fd(int fd, int tty)
{
    Src *s = xcalloc(1, sizeof *s);
    s->type = S_FD;
    s->fd = fd;
    s->tty = tty;
    s->seekable = lseek(fd, 0, SEEK_CUR) >= 0;
    s->prev = src;
    src = s;
}

void push_input_string(const char *str, int heap)
{
    Src *s = xcalloc(1, sizeof *s);
    s->type = S_STR;
    s->buf = (char *)str;
    s->len = strlen(str);
    s->heap = heap;
    s->prev = src;
    src = s;
}

void push_alias(const char *val, Alias *a)
{
    Src *s = xcalloc(1, sizeof *s);
    s->type = S_ALIAS;
    s->buf = (char *)val;
    s->len = strlen(val);
    s->alias = a;
    a->inuse = 1;
    s->prev = src;
    src = s;
}

void pop_input(void)
{
    Src *s = src;
    if (!s)
        return;
    src = s->prev;
    if (s->type == S_ALIAS && s->alias) {
        s->alias->inuse = 0;
        if (s->len && (s->buf[s->len - 1] == ' ' || s->buf[s->len - 1] == '\t'))
            alias_blank_popped = 1;
    } else if ((s->type == S_STR || s->type == S_CONT) && s->heap)
        free(s->buf);
    else if (s->type == S_FD)
        free(s->buf);
    free(s);
}

void set_prompt(int which) { prompt_which = which; }

int input_is_interactive(void) { return src && src->type == S_FD && src->tty; }

static void echo_verbose(const char *p, size_t n)
{
    if (opts[O_VERBOSE]) {
        outn(&out2, p, n);
        outflush(&out2);
    }
}

/* refill an fd source; returns 0 at EOF */
static int fill_fd(Src *s)
{
    ssize_t n;
    if (s->eof)
        return 0;
    if (s->tty) {
        char *line;
        const char *ps = NULL;
        StackMark m;
        flushall();
        jobs_notify();
        if (prompt_which == 1) {
            history_new_entry();
            mail_check();
        }
        setstackmark(&m);
        if (prompt_which == 1)
            ps = expand_prompt(var_get("PS1") ? var_get("PS1") : "");
        else if (prompt_which == 2)
            ps = expand_prompt(var_get("PS2") ? var_get("PS2") : "");
        line = read_line_interactive(ps ? ps : "");
        popstackmark(&m);
        prompt_which = 2;
        if (!line) {
            s->eof = 1;
            return 0;
        }
        free(s->buf);
        s->buf = line;
        s->len = strlen(line);
        s->pos = 0;
        echo_verbose(line, s->len);
        return s->len > 0;
    }
    if (!s->buf) {
        s->cap = 8192;
        s->buf = xmalloc(s->cap);
    }
    for (;;) {
        size_t want = s->cap;
        if (s->fd == 0 && !s->seekable)
            want = 1;
        n = read(s->fd, s->buf, want);
        if (n < 0 && errno == EINTR) {
            if (gotsig)
                run_pending_traps();
            continue;
        }
        break;
    }
    if (n <= 0) {
        if (n < 0 && errno != EIO) {
            sh_warn("read error: %s", strerror(errno));
            exitstatus = 2;
        }
        s->eof = 1;
        return 0;
    }
    s->pos = 0;
    s->len = (size_t)n;
    echo_verbose(s->buf, s->len);
    return 1;
}

int pgetc(void)
{
    for (;;) {
        Src *s = src;
        if (!s)
            return EOF;
        if (s->npush)
            return s->pushback[--s->npush];
        if (s->pos < s->len)
            return (unsigned char)s->buf[s->pos++];
        if (s->type == S_FD) {
            if (!fill_fd(s))
                return EOF;
            continue;
        }
        if (s->type == S_CONT) {
            pop_input();
            continue;
        }
        if (s->type == S_ALIAS) {
            /* end of an alias value: continue with the enclosing input,
             * behaving as if a <space> had been read */
            pop_input();
            if (src && src->npush < 16)
                src->pushback[src->npush++] = ' ';
            continue;
        }
        return EOF;     /* end of a string source */
    }
}

void pungetc(int c)
{
    Src *s = PB;
    if (c == EOF)
        return;
    if (s->npush < 16)
        s->pushback[s->npush++] = c;
}

int input_at_eof(void)
{
    return !src || (src->type == S_FD && src->eof && src->pos >= src->len && !src->npush);
}

/* give back read-ahead on a seekable standard input */
void input_sync(void)
{
    Src *s = src;
    if (s && s->type == S_FD && s->fd == 0 && s->seekable && !s->tty) {
        off_t back = (off_t)(s->len - s->pos) + s->npush;
        if (back > 0 && lseek(0, -back, SEEK_CUR) >= 0) {
            s->pos = s->len = 0;
            s->npush = 0;
        }
    }
}

void input_discard_line(void)
{
    while (src && (src->type == S_ALIAS || src->type == S_CONT))
        pop_input();
    if (src) {
        src->npush = 0;
        if (src->type == S_FD && src->tty)
            src->pos = src->len;
    }
}

/* the string source just pushed continues into the enclosing input */
void mark_continuation_source(void)
{
    if (src && src->type == S_STR)
        src->type = S_CONT;
}

void input_reset_eof(void)
{
    Src *s = src;
    while (s && s->type != S_FD) s = s->prev;
    if (s) s->eof = 0;
}

/* MAIL, MAILPATH, MAILCHECK (User Portability Utilities option) */
#include <time.h>
void mail_check(void)
{
    static time_t last_check;
    static struct { char *path; time_t mtime; } seen[16];
    static int nseen;
    const char *mc = var_get("MAILCHECK"), *mp = var_get("MAILPATH"), *m = var_get("MAIL");
    long interval = 600;
    time_t now = time(NULL);
    char *list, *p;
    int ok;
    if (mc) {
        interval = parse_long(mc, &ok);
        if (!ok || interval < 0) interval = 600;
    }
    if (!mp && !m)
        return;
    if (last_check && now - last_check < interval)
        return;
    last_check = now;
    list = xstrdup(mp ? mp : m);
    for (p = list; p && *p; ) {
        char *e = mp ? strchr(p, ':') : NULL;
        char *msg = NULL, *q;
        struct stat st;
        int i;
        if (e) *e = 0;
        /* path%message, with \% a literal percent */
        for (q = p; mp && *q; q++) {
            if (*q == '\\' && q[1] == '%') { memmove(q, q + 1, strlen(q)); continue; }
            if (*q == '%') { *q = 0; msg = q + 1; break; }
        }
        if (stat(p, &st) == 0) {
            for (i = 0; i < nseen; i++)
                if (strcmp(seen[i].path, p) == 0)
                    break;
            if (i == nseen && nseen < 16) {
                seen[nseen].path = xstrdup(p);
                seen[nseen].mtime = st.st_mtime;
                nseen++;
            } else if (i < nseen && st.st_mtime != seen[i].mtime) {
                seen[i].mtime = st.st_mtime;
                if (st.st_size > 0) {
                    if (msg) {
                        StackMark sm;
                        setstackmark(&sm);
                        outfmt(&out2, "%s\n", expand_prompt_params(msg));
                        popstackmark(&sm);
                    } else
                        outfmt(&out2, "you have mail in %s\n", p);
                    outflush(&out2);
                }
            }
        }
        p = e ? e + 1 : NULL;
    }
    free(list);
}
