/*
 * util.c - memory, strings, argument quoting, filename expansion,
 * running commands, the pager, addresses and encoded header words.
 */
#include "mailx.h"

int exit_status;

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "mailx: out of memory\n");
        exit(1);
    }
    return p;
}

void *xcalloc(size_t n, size_t m)
{
    void *p = calloc(n ? n : 1, m ? m : 1);
    if (!p) {
        fprintf(stderr, "mailx: out of memory\n");
        exit(1);
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        fprintf(stderr, "mailx: out of memory\n");
        exit(1);
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

void err(const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);
    fputs("mailx: ", stderr);
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

void buf_fmt(Buf *b, const char *fmt, ...)
{
    va_list ap;
    char tmp[512];
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < (int)sizeof tmp) {
        buf_add(b, tmp, (size_t)n);
        return;
    }
    {
        char *p = xmalloc((size_t)n + 1);
        va_start(ap, fmt);
        vsnprintf(p, (size_t)n + 1, fmt, ap);
        va_end(ap);
        buf_add(b, p, (size_t)n);
        free(p);
    }
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

void words_add(Words *w, const char *s)
{
    if (w->n + 2 > w->cap) {
        w->cap = w->cap ? w->cap * 2 : 8;
        w->v = xrealloc(w->v, (size_t)w->cap * sizeof(char *));
    }
    w->v[w->n++] = xstrdup(s);
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

int words_has(Words *w, const char *s, int icase)
{
    for (int i = 0; i < w->n; i++)
        if (icase ? strcasecmp(w->v[i], s) == 0 : strcmp(w->v[i], s) == 0)
            return 1;
    return 0;
}

/* ---------- argument quoting ---------- */

int split_args(const char *s, Words *w)
{
    while (*s) {
        Buf b = { 0 };
        int any = 0;
        while (*s == ' ' || *s == '\t' || *s == '\n')
            s++;
        if (!*s)
            break;
        while (*s && *s != ' ' && *s != '\t' && *s != '\n') {
            if (*s == '"' || *s == '\'') {
                char q = *s++;
                while (*s && *s != q)
                    buf_addc(&b, *s++);
                if (*s != q) {
                    buf_free(&b);
                    return -1;
                }
                s++;
                any = 1;
                continue;
            }
            if (*s == '\\' && s[1]) {
                buf_addc(&b, s[1]);
                s += 2;
                any = 1;
                continue;
            }
            buf_addc(&b, *s++);
            any = 1;
        }
        if (any) {
            words_add(w, b.s ? b.s : "");
            buf_free(&b);
        }
    }
    return 0;
}

/* ---------- paths ---------- */

const char *home_dir(void)
{
    const char *h = getenv("HOME");
    if (h && *h)
        return h;
    {
        struct passwd *pw = getpwuid(getuid());
        if (pw)
            return pw->pw_dir;
    }
    return "/";
}

const char *login_name(void)
{
    static char *name;
    if (!name) {
        struct passwd *pw = getpwuid(getuid());
        const char *l = pw ? pw->pw_name : getenv("LOGNAME");
        if (!l)
            l = getenv("USER");
        name = xstrdup(l ? l : "unknown");
    }
    return name;
}

static char *join_path(const char *a, const char *b)
{
    Buf p = { 0 };
    buf_adds(&p, a);
    if (p.len && p.s[p.len - 1] != '/')
        buf_addc(&p, '/');
    buf_adds(&p, b);
    return buf_take(&p);
}

char *system_mailbox(const char *user)
{
    struct stat st;
    if (!user) {
        const char *m = getenv("MAIL");
        if (m && *m)
            return xstrdup(m);
        user = login_name();
    }
    if (stat("/var/mail", &st) == 0 && S_ISDIR(st.st_mode))
        return join_path("/var/mail", user);
    return join_path("/var/spool/mail", user);
}

char *mbox_path(void)
{
    const char *m = var_get("MBOX");
    if (m && *m)
        return xstrdup(m);
    return join_path(home_dir(), "mbox");
}

char *dead_path(void)
{
    const char *d = var_get("DEAD");
    if (d && *d)
        return xstrdup(d);
    return join_path(home_dir(), "dead.letter");
}

char *folder_dir(void)
{
    const char *f = var_get("folder");
    if (!f || !*f)
        return NULL;
    if (f[0] == '/')
        return xstrdup(f);
    return join_path(home_dir(), f);
}

char *expand_filename(const char *name)
{
    char *s;
    wordexp_t we;
    int r;
    if (name[0] == '+') {
        char *fd = folder_dir();
        if (fd) {
            s = join_path(fd, name + 1);
            free(fd);
        } else
            s = xstrdup(name);
    } else if (strcmp(name, "%") == 0)
        s = system_mailbox(NULL);
    else if (name[0] == '%')
        s = system_mailbox(name + 1);
    else if (strcmp(name, "#") == 0) {
        if (!prev_file) {
            err("No previous file");
            return NULL;
        }
        s = xstrdup(prev_file);
    } else if (strcmp(name, "&") == 0)
        s = mbox_path();
    else
        s = xstrdup(name);
    if (!strpbrk(s, "~$`*?[{\\\"'"))
        return s;
    r = wordexp(s, &we, 0);
    if (r == WRDE_BADCHAR || r == WRDE_SYNTAX)
        return s;    /* not something the shell would expand */
    if (r != 0) {
        err("%s: cannot expand", s);
        free(s);
        return NULL;
    }
    if (we.we_wordc != 1) {
        err("%s: %s", s, we.we_wordc ? "Ambiguous" : "No match");
        wordfree(&we);
        free(s);
        return NULL;
    }
    free(s);
    s = xstrdup(we.we_wordv[0]);
    wordfree(&we);
    return s;
}

/* ---------- commands ---------- */

static const char *shell_name(void)
{
    const char *s = var_get("SHELL");
    return s && *s ? s : "sh";
}

int run_shell_cmd(const char *cmd, int in_fd, int out_fd)
{
    pid_t pid;
    int status = -1;
    struct sigaction ign, oint, oquit;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    fflush(stdout);
    sigaction(SIGINT, &ign, &oint);
    sigaction(SIGQUIT, &ign, &oquit);
    pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        if (in_fd >= 0 && in_fd != 0) {
            dup2(in_fd, 0);
            close(in_fd);
        }
        if (out_fd >= 0 && out_fd != 1) {
            dup2(out_fd, 1);
            close(out_fd);
        }
        execlp(shell_name(), shell_name(), "-c", "--", cmd, (char *)NULL);
        fprintf(stderr, "mailx: %s: %s\n", shell_name(), strerror(errno));
        _exit(127);
    }
    if (pid > 0)
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
    sigaction(SIGINT, &oint, NULL);
    sigaction(SIGQUIT, &oquit, NULL);
    return status;
}

static void add_quoted(Buf *b, const char *s)
{
    buf_addc(b, '\'');
    for (; *s; s++) {
        if (*s == '\'')
            buf_adds(b, "'\\''");
        else
            buf_addc(b, *s);
    }
    buf_addc(b, '\'');
}

int run_interactive(const char *prog, const char *arg)
{
    Buf c = { 0 };
    int st;
    buf_adds(&c, prog);
    if (arg) {
        buf_addc(&c, ' ');
        add_quoted(&c, arg);
    }
    st = run_shell_cmd(c.s, -1, -1);
    buf_free(&c);
    return st;
}

int is_tty_in(void)
{
    return isatty(0);
}

int is_tty_out(void)
{
    return isatty(1);
}

int screen_rows(void)
{
    struct winsize ws;
    const char *l;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return ws.ws_row;
    l = getenv("LINES");
    if (l && atoi(l) > 0)
        return atoi(l);
    return 24;
}

FILE *open_pager(int lines, pid_t *pidp)
{
    const char *crt = var_get("crt");
    int limit, pfd[2];
    pid_t pid;
    *pidp = 0;
    if (!crt || !is_tty_out())
        return stdout;
    limit = *crt ? atoi(crt) : screen_rows();
    if (lines <= limit)
        return stdout;
    fflush(stdout);
    if (pipe(pfd) < 0)
        return stdout;
    pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return stdout;
    }
    if (pid == 0) {
        const char *pg = var_get("PAGER");
        signal(SIGINT, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        dup2(pfd[0], 0);
        close(pfd[0]);
        close(pfd[1]);
        execl("/bin/sh", "sh", "-c", pg && *pg ? pg : "more", (char *)NULL);
        _exit(127);
    }
    close(pfd[0]);
    *pidp = pid;
    signal(SIGPIPE, SIG_IGN);
    return fdopen(pfd[1], "w");
}

void close_pager(FILE *fp, pid_t pid)
{
    if (fp == stdout) {
        fflush(stdout);
        return;
    }
    fclose(fp);
    {
        struct sigaction ign, oint;
        memset(&ign, 0, sizeof ign);
        ign.sa_handler = SIG_IGN;
        sigaction(SIGINT, &ign, &oint);
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
            ;
        sigaction(SIGINT, &oint, NULL);
    }
}

char *make_temp(int *fd)
{
    const char *t = getenv("TMPDIR");
    char *p = join_path(t && *t ? t : "/tmp", "mailxXXXXXX");
    *fd = mkstemp(p);
    if (*fd < 0) {
        err("%s: %s", p, strerror(errno));
        free(p);
        return NULL;
    }
    return p;
}

/* ---------- addresses ---------- */

void split_addresses(const char *s, Words *out)
{
    int structured = strpbrk(s, ",<\"(") != NULL;
    Buf b = { 0 };
    int angle = 0, paren = 0, quote = 0;
    for (const char *p = s;; p++) {
        int end = !*p;
        int sep = end || (!quote && !angle && !paren &&
                          (*p == ',' || (!structured && (*p == ' ' || *p == '\t' || *p == '\n'))));
        if (sep) {
            char *t = b.s ? b.s : "";
            size_t l;
            while (*t == ' ' || *t == '\t' || *t == '\n')
                t++;
            l = strlen(t);
            while (l && (t[l - 1] == ' ' || t[l - 1] == '\t' || t[l - 1] == '\n'))
                l--;
            if (l) {
                char *a = xstrndup(t, l);
                words_add(out, a);
                free(a);
            }
            buf_free(&b);
            if (end)
                break;
            continue;
        }
        if (*p == '"' && (p == s || p[-1] != '\\'))
            quote = !quote;
        else if (!quote) {
            if (*p == '<')
                angle++;
            else if (*p == '>' && angle)
                angle--;
            else if (*p == '(')
                paren++;
            else if (*p == ')' && paren)
                paren--;
        }
        buf_addc(&b, *p);
    }
}

char *addr_spec(const char *a)
{
    const char *lt = strrchr(a, '<'), *gt;
    Buf b = { 0 };
    int paren = 0, quote = 0;
    if (lt && (gt = strchr(lt, '>')) != NULL)
        return xstrndup(lt + 1, (size_t)(gt - lt - 1));
    for (const char *p = a; *p; p++) {
        if (*p == '"')
            quote = !quote;
        if (!quote && *p == '(') {
            paren++;
            continue;
        }
        if (!quote && *p == ')' && paren) {
            paren--;
            continue;
        }
        if (paren)
            continue;
        if (!quote && (*p == ' ' || *p == '\t'))
            continue;
        buf_addc(&b, *p);
    }
    return buf_take(&b);
}

char *addr_login(const char *a)
{
    char *s = addr_spec(a), *at, *bang, *r;
    at = strchr(s, '@');
    if (at)
        *at = 0;
    bang = strrchr(s, '!');
    r = xstrdup(bang ? bang + 1 : s);
    {
        char *pct = strchr(r, '%');
        if (pct)
            *pct = 0;
    }
    free(s);
    return r;
}

int addr_is_me(const char *a)
{
    char *s = addr_spec(a), *l = addr_login(a);
    int me = 0, allnet = var_isset("allnet");
    char host[256];
    const char *login = login_name();
    if (strcasecmp(s, login) == 0)
        me = 1;
    else if (allnet && strcasecmp(l, login) == 0)
        me = 1;
    else if (gethostname(host, sizeof host) == 0) {
        Buf b = { 0 };
        host[sizeof host - 1] = 0;
        buf_fmt(&b, "%s@%s", login, host);
        if (strcasecmp(s, b.s) == 0)
            me = 1;
        buf_free(&b);
    }
    for (int i = 0; i < alternates.n && !me; i++) {
        char *al = addr_login(alternates.v[i]);
        if (strcasecmp(s, alternates.v[i]) == 0 || (allnet && strcasecmp(l, al) == 0))
            me = 1;
        free(al);
    }
    free(s);
    free(l);
    return me;
}

/* ---------- RFC 2047 encoded words ---------- */

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static void decode_word(Buf *out, const char *cs, int enc, const char *t, size_t tl)
{
    Buf raw = { 0 };
    if (enc == 'B' || enc == 'b') {
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        unsigned acc = 0;
        int bits = 0;
        for (size_t i = 0; i < tl; i++) {
            const char *p = strchr(b64, t[i]);
            if (!p || !t[i])
                continue;
            acc = (acc << 6) | (unsigned)(p - b64);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                buf_addc(&raw, (int)((acc >> bits) & 0xff));
            }
        }
    } else {
        for (size_t i = 0; i < tl; i++) {
            if (t[i] == '_')
                buf_addc(&raw, ' ');
            else if (t[i] == '=' && i + 2 < tl && hexval(t[i + 1]) >= 0 && hexval(t[i + 2]) >= 0) {
                buf_addc(&raw, hexval(t[i + 1]) * 16 + hexval(t[i + 2]));
                i += 2;
            } else
                buf_addc(&raw, t[i]);
        }
    }
    if (!raw.s) {
        return;
    }
    {
        const char *to = nl_langinfo(CODESET);
        iconv_t cd = iconv_open(to, cs);
        if (cd == (iconv_t)-1) {
            buf_add(out, raw.s, raw.len);
        } else {
            char *in = raw.s;
            size_t inl = raw.len;
            while (inl) {
                char ob[256], *op = ob;
                size_t ol = sizeof ob;
                size_t r = iconv(cd, &in, &inl, &op, &ol);
                buf_add(out, ob, (size_t)(op - ob));
                if (r == (size_t)-1 && errno != E2BIG) {
                    buf_addc(out, '?');
                    in++;
                    inl--;
                }
            }
            iconv_close(cd);
        }
    }
    buf_free(&raw);
}

char *decode_header(const char *s)
{
    Buf out = { 0 };
    const char *p = s, *pending_ws = NULL;
    int last_encoded = 0;
    while (*p) {
        const char *q, *e1, *e2, *end;
        if (p[0] == '=' && p[1] == '?' && (q = strchr(p + 2, '?')) && q[1] && q[2] == '?' &&
            (end = strstr(q + 3, "?=")) != NULL && !memchr(p, ' ', (size_t)(end - p))) {
            char *cs = xstrndup(p + 2, (size_t)(q - p - 2));
            char *star = strchr(cs, '*');
            if (star)
                *star = 0;
            e1 = q + 1;
            e2 = q + 3;
            if (pending_ws && !last_encoded)
                buf_add(&out, pending_ws, (size_t)(p - pending_ws));
            decode_word(&out, cs, *e1, e2, (size_t)(end - e2));
            free(cs);
            p = end + 2;
            pending_ws = NULL;
            last_encoded = 1;
            continue;
        }
        if (*p == ' ' || *p == '\t') {
            if (!pending_ws)
                pending_ws = p;
            p++;
            continue;
        }
        if (pending_ws) {
            buf_add(&out, pending_ws, (size_t)(p - pending_ws));
            pending_ws = NULL;
        }
        last_encoded = 0;
        buf_addc(&out, *p++);
    }
    if (pending_ws)
        buf_adds(&out, pending_ws);
    return buf_take(&out);
}
